/**
 * @file main.c
 * @brief Application entry point: initializes all subsystems, then runs the
 *        main event loop that consumes listener actions and drives LED state.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/queue.h>
#include "listener.h"
#include "rb.h"
#include "ASLNode.h"
#include "globalDefines.h"
#include "HAL.h"

/* All application heap memory lives in one allocation so a single free()
 * releases everything except the rbtree (which has its own allocator). */
typedef struct {
    HAL      hal;
    Listener listener;
    rbtree  *nodeTree;
} AppMemory;

/* Forward declarations */
/* -- Signal handling -- */
static void onShutdownSignal(int signal_number);
static int  initSignals(void);
/* -- Lifecycle -- */
static int  initApp(AppMemory **mem);
static int  loadConfig(AppMemory *mem);
static void deinitApp(AppMemory *mem);
/* -- Event loop -- */
static void runApp(AppMemory *mem);
static void findAndUpdateNodeForAction(rbtree *nodeTree, Listener *lMem, LogAction *action);
/* -- Utilities -- */
static int  checkFileExists(const char *filename);
/* -- Debug -- */
static void testLeds(HAL *hal);

/* Set by the signal handler; checked by the main loop and child threads. */
volatile sig_atomic_t shutdownFlag = false;

int main(void)
{
    AppMemory *mem = NULL;

    /* Register signal handlers before initApp so the process can be
     * interrupted cleanly even if startup takes time. */
    if (initSignals() == -1)
        exit(EXIT_FAILURE);

    int initResult = initApp(&mem);
    if (initResult != 0) {
        deinitApp(mem);
        return initResult;
    }

    if (loadConfig(mem) != 0) {
        deinitApp(mem);
        return -1;
    }

    runApp(mem);
    deinitApp(mem);

    return 0;
}

/* ── Signal handling ──────────────────────────────────────────────────────── */

/**
 * @brief Async-signal-safe handler for SIGINT, SIGTERM, and SIGHUP.
 *
 * Only assigns to a volatile sig_atomic_t — the only operation guaranteed
 * safe inside a signal handler. The main loop and threads poll shutdownFlag
 * and perform their own cleanup before exiting.
 */
static void onShutdownSignal(int signal_number)
{
    (void)signal_number;
    shutdownFlag = true;
}

/**
 * @brief Register onShutdownSignal for SIGINT, SIGTERM, and SIGHUP.
 * @return 0 on success, -1 on failure.
 */
static int initSignals(void)
{
    struct sigaction sa = { .sa_handler = onShutdownSignal };
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT,  &sa, NULL) == -1 ||
        sigaction(SIGTERM, &sa, NULL) == -1 ||
        sigaction(SIGHUP,  &sa, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }

    return 0;
}

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * @brief Initialize all application subsystems.
 *
 * Verifies config files, allocates AppMemory, initializes the HAL and
 * Listener. Does not perform any cleanup on failure — the caller must call
 * deinitApp() regardless of the return value.
 *
 * @param mem  Set to the allocated AppMemory block, or NULL if allocation failed.
 * @return     Accumulated error bitmask (0 = fully successful).
 */
static int initApp(AppMemory **mem)
{
    int result = 0;

    /* Check all config files before allocating anything so every missing file
     * is reported in one run rather than failing on the first one found. */
    if (checkFileExists(HARDWARE_DEFINITIONS_FILE_PATH) == -1) {
        fprintf(stderr, "Error: Hardware definitions file not found or corrupted.\n");
        result |= HARDWARE_DEFINITION_FILE_PATH_INIT_ERROR;
    }
    if (checkFileExists(APPCONFIG_FILE_PATH) == -1) {
        fprintf(stderr, "Error: AppConfig file not found or corrupted.\n");
        result |= APPCONFIG_FILE_PATH_INIT_ERROR;
    }
    if (checkFileExists(NODES_FILE_PATH) == -1) {
        fprintf(stderr, "Error: Nodes file not found or corrupted.\n");
        result |= NODES_FILE_PATH_INIT_ERROR;
    }
    if (checkFileExists(RPT_CONF_FILE_PATH) == -1) {
        fprintf(stderr, "Error: RPT config file not found or corrupted.\n");
        result |= RPT_CONF_FILE_PATH_INIT_ERROR;
    }

    *mem = calloc(1, sizeof(AppMemory));
    if (!*mem) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return result | APP_MEMORY_ALLOCATION_INIT_ERROR;
    }

    AppMemory *m = *mem;

    m->nodeTree = rb_create(compareASLNode, destroyASLNode);
    if (!m->nodeTree) {
        fprintf(stderr, "Error: RB tree allocation failed.\n");
        return result | RB_TREE_ALLOCATION_INIT_ERROR;
    }

    if (initHAL(&m->hal) == -1) {
        fprintf(stderr, "Error: HAL initialization failed.\n");
        return result | HAL_INIT_ERROR;
    }
    m->hal.initialized = true;

    if (initListener(&m->listener) == -1) {
        fprintf(stderr, "Error: Listener initialization failed.\n");
        return result | LISTENER_THREAD_INIT_ERROR;
    }
    m->listener.initialized = true;

    return result;
}

/**
 * @brief Load hardware definitions and seed the node tree with the local node.
 * @return 0 on success, -1 if the hardware config file could not be parsed.
 */
static int loadConfig(AppMemory *mem)
{
    HALStatus_t status = HALLoadConfig(&mem->hal, HARDWARE_DEFINITIONS_FILE_PATH);
    if (status != HAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to load hardware config (status %d).\n", status);
        return -1;
    }

    ASLNode *mainNode = makeASLNode("MAIN");
    rb_insert(mem->nodeTree, mainNode);

    return 0;
}

/**
 * @brief Tear down all initialized subsystems and free AppMemory.
 *
 * Safe to call at any point after initApp() — checks flags before touching
 * each subsystem so partial initialization is handled correctly.
 */
static void deinitApp(AppMemory *mem)
{
    if (!mem)
        return;

    if (mem->hal.initialized)
        deinitHAL(&mem->hal);

    if (mem->listener.initialized) {
        mem->listener.halt = true;          /* signal the thread to exit its loop */
        pthread_join(mem->listener.id, NULL);
        pthread_mutex_destroy(&mem->listener.listenerLock); /* safe only after join */
    }

    if (mem->nodeTree)
        rb_destroy(mem->nodeTree);

    free(mem);
}

/* ── Event loop ───────────────────────────────────────────────────────────── */

/**
 * @brief Run the main event loop until a shutdown signal is received.
 *
 * Polls the listener's action queue and dispatches each entry to
 * findAndUpdateNodeForAction(). Sleeps briefly when the queue is empty
 * to avoid spinning the CPU.
 *
 * @param mem  Fully initialized application memory.
 */
static void runApp(AppMemory *mem)
{
    while (!shutdownFlag) {
        if (TAILQ_EMPTY(&mem->listener.recentActions)) {
            usleep(5000); /* 5 ms idle poll — avoids busy-waiting */
            continue;
        }

        while (!TAILQ_EMPTY(&mem->listener.recentActions)) {
            findAndUpdateNodeForAction(mem->nodeTree, &mem->listener,
                                       TAILQ_FIRST(&mem->listener.recentActions));
        }
    }
}

/**
 * @brief Look up the node named in action, update its state, then free the
 *        action and remove it from the listener queue.
 *
 * If the node does not yet exist in the tree it is created on the fly so that
 * connected nodes are tracked automatically without pre-configuration.
 */
static void findAndUpdateNodeForAction(rbtree *nodeTree, Listener *lMem, LogAction *action)
{
    rbnode *node = rb_find(nodeTree, action->name);

    /* Node tree reads and writes are lock-free: only the main thread ever
     * touches nodeTree, so no synchronization is needed here. */
    if (node != NULL) {
        updateASLNode(node->data, action->LastUpdate, action->action);
    } else {
        ASLNode *newNode = makeASLNode(action->name);
        updateASLNode(newNode, action->LastUpdate, action->action);
        rb_insert(nodeTree, newNode);
    }

    /* Lock only for queue removal — the listener thread may enqueue new
     * actions concurrently, so the shared queue requires protection. */
    pthread_mutex_lock(&lMem->listenerLock);
    TAILQ_REMOVE(&lMem->recentActions, action, entries);
    lMem->queueSize--;
    pthread_mutex_unlock(&lMem->listenerLock);

    free(action);
}

/* ── Utilities ────────────────────────────────────────────────────────────── */

/**
 * @brief Test whether a file exists and can be opened for reading.
 * @return 0 if the file exists, -1 otherwise.
 */
static int checkFileExists(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
        return -1;
    fclose(f);
    return 0;
}

/* ── Debug ────────────────────────────────────────────────────────────────── */

/**
 * @brief Cycle every configured LED through on, off, one-shot, and blink modes.
 *
 * Used during hardware bring-up to confirm each LED is wired and responding
 * correctly. Not called in normal operation.
 *
 * @param hal  Fully initialized and configured HAL instance.
 */
static void testLeds(HAL *hal)
{
    if (!hal->leds || hal->numLeds == 0) {
        printf("HAL not initialized or no LEDs configured.\n");
        return;
    }

    for (int i = 0; i < hal->numLeds; i++) {
        HAL_Led_t *led = &hal->leds[i];
        led->setConstant(led->impl, HAL_LED_MODE_ON);
        usleep(10000);                          /* 10 ms on */
        led->setConstant(led->impl, HAL_LED_MODE_OFF);
        usleep(10000);                          /* 10 ms off */
        led->setOneShot(led->impl, 50);
        usleep(60000);                          /* 60 ms — allow one-shot to complete */
        led->setBlink(led->impl, 100, 100);
        usleep(420000);                         /* 420 ms — observe blink */
        led->setConstant(led->impl, HAL_LED_MODE_OFF);
    }
}


/************************
 *CODE SNIPPET GRAVEYARD
 ************************/

// node = rb_find(app.nodeTree, "MAIN");
// if (node != NULL) {
//     /* MAIN node RX/TX state reserved for future LED assignment. */
//     (void)node;
// }

// node = rb_find(app.nodeTree, "2324"); /* K8SN */
// if (node != NULL) {
//     /* TODO: assign LEDs for K8SN node. */
//     (void)node;
// }

// node = rb_find(app.nodeTree, "2462"); /* Seattle */
// if (node != NULL) {
//     ASLNode *n = node->data;
//     pthread_mutex_lock(&app.hardware->hardwareLock);
//     switch (n->mode) {
//         case 1:
//             app.hardware->leds[0].state = TRUE;
//             app.hardware->leds[1].state = FALSE;
//             break;
//         case 2:
//             app.hardware->leds[0].state = FALSE;
//             app.hardware->leds[1].state = TRUE;
//             break;
//         default:
//             app.hardware->leds[0].state = FALSE;
//             app.hardware->leds[1].state = FALSE;
//             break;
//     }
//     app.hardware->leds[2].state = n->rxKey;
//     app.hardware->leds[3].state = n->txKey;
//     pthread_mutex_unlock(&app.hardware->hardwareLock);
// }

// node = rb_find(app.nodeTree, "472440"); /* W8IRA */
// if (node != NULL) {
//     ASLNode *n = node->data;
//     pthread_mutex_lock(&app.hardware->hardwareLock);
//     switch (n->mode) {
//         case 1:
//             app.hardware->leds[4].state = TRUE;
//             app.hardware->leds[5].state = FALSE;
//             break;
//         case 2:
//             app.hardware->leds[4].state = FALSE;
//             app.hardware->leds[5].state = TRUE;
//             break;
//         default:
//             app.hardware->leds[4].state = FALSE;
//             app.hardware->leds[5].state = FALSE;
//             break;
//     }
//     app.hardware->leds[6].state = n->rxKey;
//     app.hardware->leds[7].state = n->txKey;
//     pthread_mutex_unlock(&app.hardware->hardwareLock);
// }

// node = rb_find(app.nodeTree, "27339"); /* East Coast Reflector */
// if (node != NULL) {
//     ASLNode *n = node->data;
//     pthread_mutex_lock(&app.hardware->hardwareLock);
//     switch (n->mode) {
//         case 1:
//             app.hardware->leds[8].state  = TRUE;
//             app.hardware->leds[9].state  = FALSE;
//             break;
//         case 2:
//             app.hardware->leds[8].state  = FALSE;
//             app.hardware->leds[9].state  = TRUE;
//             break;
//         default:
//             app.hardware->leds[8].state  = FALSE;
//             app.hardware->leds[9].state  = FALSE;
//             break;
//     }
//     app.hardware->leds[10].state = n->rxKey;
//     app.hardware->leds[11].state = n->txKey;
//     pthread_mutex_unlock(&app.hardware->hardwareLock);
// }
