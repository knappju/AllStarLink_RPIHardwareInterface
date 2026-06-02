/**
 * @file main.c
 * @brief Application entry point: initializes all subsystems, then runs the
 *        main event loop that consumes listener actions and drives LED state.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <sys/queue.h>
#include "hardwareManager.h"
#include "listener.h"
#include "rb.h"
#include "ASLNode.h"
#include "globalDefines.h"
#include "HAL.h"

/* Forward declarations */
static int  checkFileExists(const char *filename);
static void cleanUp(int signal_number);
static void findAndUpdateNodeForAction(rbtree *nodeTree, Listener *lMem, LogAction *action);

/* All application heap memory lives in one allocation so a single free()
 * releases everything except the rbtree (which has its own allocator). */
typedef struct {
    Hardware hardware;
    Listener listener;
    rbtree  *nodeTree;
} AppMemory;

/* Convenience struct of pointers into AppMemory; avoids carrying AppMemory*
 * everywhere and makes ownership clear at each call site. */
typedef struct {
    Hardware *hardware;
    Listener *listener;
    rbtree   *nodeTree;
} App;

/* Set by the signal handler; checked by the main loop and child threads. */
volatile sig_atomic_t shutdownFlag = FALSE;

int main(void)
{
    int initializationResult = 0;

    /* Install the same graceful-shutdown handler for the three termination
     * signals we care about. */
    struct sigaction sigterm_action = {
        .sa_handler = cleanUp
    };
    sigemptyset(&sigterm_action.sa_mask);

    if (sigaction(SIGINT,  &sigterm_action, NULL) == -1 ||
        sigaction(SIGTERM, &sigterm_action, NULL) == -1 ||
        sigaction(SIGHUP,  &sigterm_action, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Verify all required config files are present before touching hardware. */
    if (checkFileExists(HARDWARE_DEFINITIONS_FILE_PATH) == -1) {
        fprintf(stderr, "Error: Hardware definitions file not found or corrupted.\n");
        initializationResult |= HARDWARE_DEFINITION_FILE_PATH_INIT_ERROR;
    }
    if (checkFileExists(APPCONFIG_FILE_PATH) == -1) {
        fprintf(stderr, "Error: AppConfig file not found or corrupted.\n");
        initializationResult |= APPCONFIG_FILE_PATH_INIT_ERROR;
    }
    if (checkFileExists(NODES_FILE_PATH) == -1) {
        fprintf(stderr, "Error: Nodes file not found or corrupted.\n");
        initializationResult |= NODES_FILE_PATH_INIT_ERROR;
    }
    if (checkFileExists(RPT_CONF_FILE_PATH) == -1) {
        fprintf(stderr, "Error: RPT config file not found or corrupted.\n");
        initializationResult |= RPT_CONF_FILE_PATH_INIT_ERROR;
    }

    AppMemory *mem = calloc(1, sizeof(AppMemory));
    if (!mem) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return initializationResult | APP_MEMORY_ALLOCATION_INIT_ERROR;
    }

    mem->nodeTree = rb_create(compareASLNode, destroyASLNode);
    if (!mem->nodeTree) {
        free(mem);
        fprintf(stderr, "Error: RB tree allocation failed.\n");
        return initializationResult | RB_TREE_ALLOCATION_INIT_ERROR;
    }

    App app;
    app.hardware  = &mem->hardware;
    app.listener  = &mem->listener;
    app.nodeTree  = mem->nodeTree;

    /* The "MAIN" node represents the local repeater itself. */
    ASLNode *mainNode = makeASLNode("MAIN");
    rb_insert(app.nodeTree, mainNode);

    HAL_t *hal = initHAL();
    if (!hal) {
        initializationResult |= HARDWARE_DEFINITION_FILE_PATH_INIT_ERROR;
    }

    HALLoadConfig(hal, HARDWARE_DEFINITIONS_FILE_PATH);

    // printf("Finished HAL initialization.\n");

    // printf("Number of buttons: %d\n", hal->numButtons);
    // for (int i = 0; i < hal->numButtons; i++) {
    //     printf("  Button %d: %s\n", i, hal->buttons[i].logicalName);
    // }
    // printf("Number of LEDs: %d\n", hal->numLeds);
    // for (int i = 0; i < hal->numLeds; i++) {
    //     printf("  LED %d: %s\n", i, hal->leds[i].logicalName);
    // }

    //initHardware(app.hardware);

    if (initListener(app.listener) == -1) {
        //app.hardware->halt = TRUE;
        //pthread_join(app.hardware->id, NULL);
        rb_destroy(app.nodeTree);
        free(mem);
        return initializationResult | LISTENER_THREAD_INIT_ERROR;
    }

    if (initializationResult != 0) {
        printf("Initialization completed with errors.\n");
        cleanUp(0);
    }

    /* ── Main event loop ──────────────────────────────────────────────── */
    //testing led via direct mem access
    for(int led = 0; led < hal->numLeds; led++){
        hal->leds[led].setConstant(hal->leds[led].impl, HAL_LED_MODE_ON);
        usleep(10000); //2 second delay to observe the blink
        hal->leds[led].setConstant(hal->leds[led].impl, HAL_LED_MODE_OFF);
        usleep(10000); //2 second delay to observe the blink
        hal->leds[led].setOneShot(hal->leds[led].impl, 50);
        usleep(60000); //1 second delay to allow one-shot to complete
        hal->leds[led].setBlink(hal->leds[led].impl, 100, 100);
    }

    // HALLedSetConstant(hal, HALFindLedByName(hal, "led1"), HAL_LED_MODE_ON); //via HAL API
    // usleep(100000); //1 second delay
    // HALLedSetConstant(hal, HALFindLedByName(hal, "led1"), HAL_LED_MODE_OFF);
    // usleep(100000); //1 second delay
    // HALLedSetOneShot(hal, HALFindLedByName(hal, "led1"), 200);
    // usleep(300000); //3 second delay to allow one-shot to complete
    // HALLedSetBlink(hal, HALFindLedByName(hal, "led1"), 500, 500); 

    HALButtonRegisterCB(hal, HALFindButtonByName(hal, "button1"), buttonCallbackTest);
    HALButtonEnableCB(hal, HALFindButtonByName(hal, "button1"));


    while (!shutdownFlag) {
        usleep(500000); /* 500 ms idle delay to avoid busy-waiting */
        
        // if (app.listener->recentActions.tqh_first == NULL) {
        //     usleep(5000); /* 5 ms idle delay to avoid busy-waiting */
        //     continue;
        // }

        /* Drain all pending listener actions into the node tree. */
        // while (app.listener->recentActions.tqh_first != NULL) {
        //     findAndUpdateNodeForAction(app.nodeTree, app.listener,
        //                                app.listener->recentActions.tqh_first);
        // }

        /* Drive LED state from the current node states.
         * TODO: migrate to HAL LED API once the config-driven mapping is
         * implemented. The direct hardware[] assignments below are temporary. */

        //rbnode *node;

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
    }

    /* ── Shutdown sequence ────────────────────────────────────────────── */

    deinitHAL(hal);

    //app.hardware->halt = TRUE;
    app.listener->halt = TRUE;

    //pthread_join(app.hardware->id, NULL);
    pthread_join(app.listener->id, NULL);

    rb_destroy(app.nodeTree);
    free(mem);

    return 0;
}

/**
 * @brief Test whether a file exists and can be opened for reading.
 * @return 0 if the file exists, -1 otherwise.
 */
static int checkFileExists(const char *filename)
{
    FILE *file = fopen(filename, "r");
    if (file) {
        fclose(file);
        return 0;
    }
    return -1;
}

/**
 * @brief Signal handler for SIGINT, SIGTERM, and SIGHUP.
 *
 * Sets the global shutdownFlag so the main loop and all threads can exit
 * cleanly rather than being killed mid-operation with hardware in an
 * unknown state.
 */
static void cleanUp(int signal_number)
{
    (void)signal_number; /* signal number is not used */
    shutdownFlag = TRUE;
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

    if (node != NULL) {
        updateASLNode(node->data, action->LastUpdate, action->action);
    } else {
        ASLNode *newNode = makeASLNode(action->name);
        updateASLNode(newNode, action->LastUpdate, action->action);
        rb_insert(nodeTree, newNode);
    }

    pthread_mutex_lock(&lMem->listenerLock);
    TAILQ_REMOVE(&lMem->recentActions, action, entries);
    lMem->queueSize--;
    pthread_mutex_unlock(&lMem->listenerLock);
    free(action);
}
