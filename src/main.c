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
#include <wiringPi.h>
#include "listener.h"
#include "rb.h"
#include "ASLNode.h"
#include "globalDefines.h"
#include "HAL.h"
#include "AppConfig.h"

#define LONG_PRESS_MS        500   /* ms hold duration that qualifies as a long press */
#define MONITOR_TO_TRX_MS    250   /* ms to wait between disconnect and reconnect as TRX */

/* Tracks the debounced press state for one channel button across loop iterations. */
typedef struct {
    uint8_t       lastState;    /* last read pin level (1 = released, 0 = pressed) */
    unsigned long pressStartMs; /* millis() timestamp when the press began */
    bool          pressActive;  /* true while the button is being held */
} ButtonPressState;

/* All application heap memory in one struct so lifecycle is managed in one place.
 * The rbtree is heap-allocated separately and has its own allocator. */
typedef struct {
    HAL              hal;
    Listener         listener;
    rbtree          *nodeTree;
    AppConfig        appConfig;
    ButtonPressState *buttonStates; /* heap-allocated, one entry per appConfig channel */
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
static void processButtons(AppMemory *mem);
static void onButtonEvent(AppMemory *mem, int channelIdx, bool isLongPress);
static void sendNodeCommand(int modeCmd, const char *localNode, const char *remoteNode);
static void updateLedsForNode(AppConfig *cfg, HAL *hal, ASLNode *node);
static void findAndUpdateNodeForAction(rbtree *nodeTree, Listener *lMem, LogAction *action, AppConfig *cfg, HAL *hal);
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

    testLeds(&mem->hal); /* optional: confirm LEDs are working before entering the main loop */

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
 * @brief Load hardware definitions and app config, then seed the node tree.
 *
 * HALLoadConfig must run before loadAppConfig so that logical names can be
 * resolved to HAL indices.
 *
 * @return 0 on success, -1 on any config failure.
 */
static int loadConfig(AppMemory *mem)
{
    HALStatus_t status = HALLoadConfig(&mem->hal, HARDWARE_DEFINITIONS_FILE_PATH);
    if (status != HAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to load hardware config (status %d).\n", status);
        return -1;
    }

    if (loadAppConfig(&mem->appConfig, APPCONFIG_FILE_PATH, &mem->hal) != 0) {
        fprintf(stderr, "Error: Failed to load app config.\n");
        return -1;
    }
    /* Allocate per-channel button tracking state and seed each entry with the
     * current pin level so the first read doesn't look like a transition. */
    int nCh = mem->appConfig.numChannels;
    mem->buttonStates = calloc(nCh, sizeof(ButtonPressState));
    if (!mem->buttonStates) {
        fprintf(stderr, "Error: Failed to allocate button states.\n");
        return -1;
    }
    for (int i = 0; i < nCh; i++) {
        int btnIdx = mem->appConfig.channels[i].buttonIdx;
        uint8_t initState = 1; /* assume released */
        if (btnIdx >= 0)
            HALButtonRead(&mem->hal, btnIdx, &initState);
        mem->buttonStates[i].lastState   = initState;
        mem->buttonStates[i].pressActive = false;
    }

    ASLNode *mainNode = makeASLNode("MAIN");
    if (!mainNode) {
        fprintf(stderr, "Error: Failed to allocate main node.\n");
        return -1;
    }
    if (!rb_insert(mem->nodeTree, mainNode)) {
        fprintf(stderr, "Error: Failed to insert main node into tree.\n");
        free(mainNode);
        return -1;
    }

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

    free(mem->buttonStates);
    deinitAppConfig(&mem->appConfig);

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
        processButtons(mem);

        if (TAILQ_EMPTY(&mem->listener.recentActions)) {
            usleep(5000); /* 5 ms idle poll — avoids busy-waiting */
            continue;
        }

        while (!TAILQ_EMPTY(&mem->listener.recentActions)) {
            findAndUpdateNodeForAction(mem->nodeTree, &mem->listener,
                                       TAILQ_FIRST(&mem->listener.recentActions),
                                       &mem->appConfig, &mem->hal);
        }
    }
}

/* ── Button handling ──────────────────────────────────────────────────────── */

/**
 * @brief Send an rpt fun command to Asterisk to change a node's connection state.
 *
 * Commands: 1 = disconnect, 2 = connect monitor, 3 = connect TRX.
 * Uses popen() rather than system() to avoid fork/thread interactions with the
 * wiringPi debounce timer threads.
 *
 * @param modeCmd    Command digit (1, 2, or 3).
 * @param localNode  This repeater's node number string (from rpt.conf).
 * @param remoteNode The remote node number string to act on.
 */
static void sendNodeCommand(int modeCmd, const char *localNode, const char *remoteNode)
{
    if (!localNode || !remoteNode) {
        fprintf(stderr, "sendNodeCommand: NULL node string\n");
        return;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "asterisk -rx \"rpt fun %s *%d%s\"", localNode, modeCmd, remoteNode);

    FILE *p = popen(cmd, "r");
    if (p) pclose(p);
    else fprintf(stderr, "sendNodeCommand: popen failed\n");
}

/**
 * @brief Execute the Asterisk command(s) for a button press event.
 *
 * Mode commands: 1 = disconnect, 2 = connect monitor, 3 = connect TRX.
 * MONITOR → LINKTRX disconnects first, then reconnects as TRX after a short
 * delay so Asterisk can process the disconnect before the new connection.
 */
static void onButtonEvent(AppMemory *mem, int channelIdx, bool isLongPress)
{
    ChannelMapping *ch      = &mem->appConfig.channels[channelIdx];
    const char     *local   = mem->listener.NodeNumber;
    const char     *remote  = ch->node;

    rbnode  *node    = rb_find(mem->nodeTree, (void *)remote);
    ASLNode *aslNode = node ? node->data : NULL;
    uint8_t  mode    = aslNode ? aslNode->mode : 0;

    switch (mode) {
        case 0: /* OFF */
            sendNodeCommand(isLongPress ? 3 : 2, local, remote);
            break;
        case 1: /* LINKTRX — both press types disconnect */
            sendNodeCommand(1, local, remote);
            break;
        case 2: /* MONITOR */
            if (isLongPress) {
                sendNodeCommand(1, local, remote);           /* disconnect first */
                usleep(MONITOR_TO_TRX_MS * 1000);
                sendNodeCommand(3, local, remote);           /* reconnect as TRX */
            } else {
                sendNodeCommand(1, local, remote);           /* disconnect */
            }
            break;
    }
}

/**
 * @brief Poll all channel buttons, detect press/release transitions, and
 *        classify each completed press as short or long before dispatching.
 */
static void processButtons(AppMemory *mem)
{
    for (int i = 0; i < mem->appConfig.numChannels; i++) {
        int btnIdx = mem->appConfig.channels[i].buttonIdx;
        if (btnIdx < 0) continue;

        uint8_t state;
        if (HALButtonRead(&mem->hal, btnIdx, &state) != HAL_SUCCESS) continue;

        ButtonPressState *bs = &mem->buttonStates[i];
        if (state == bs->lastState) continue;
        bs->lastState = state;

        if (state == 0) { /* pressed (active low) */
            bs->pressActive  = true;
            bs->pressStartMs = millis();
        } else { /* released */
            if (bs->pressActive) {
                bool isLong = (millis() - bs->pressStartMs) >= LONG_PRESS_MS;
                onButtonEvent(mem, i, isLong);
                bs->pressActive = false;
            }
        }
    }
}

/**
 * @brief Drive LEDs assigned to node's channel based on its current state.
 *
 * con1/con2 are a complementary pair: con1=HIGH con2=LOW for LINKTRX,
 * con1=LOW con2=HIGH for LINKMONITOR, both LOW when the node is idle/disconnected.
 * txLed and rxLed mirror txKey and rxKey directly. Unmapped indices (-1)
 * are silently skipped.
 */
static void updateLedsForNode(AppConfig *cfg, HAL *hal, ASLNode *node)
{
    int ch = findChannelByNode(cfg, node->name);
    if (ch == -1) return;

    ChannelMapping *map = &cfg->channels[ch];

    if (map->con1LedIdx >= 0)
        HALLedSetConstant(hal, map->con1LedIdx,
                          node->mode == 1 ? HAL_LED_MODE_ON : HAL_LED_MODE_OFF);

    if (map->con2LedIdx >= 0)
        HALLedSetConstant(hal, map->con2LedIdx,
                          node->mode == 2 ? HAL_LED_MODE_ON : HAL_LED_MODE_OFF);

    if (map->txLedIdx >= 0)
        HALLedSetConstant(hal, map->txLedIdx,
                          node->txKey ? HAL_LED_MODE_ON : HAL_LED_MODE_OFF);

    if (map->rxLedIdx >= 0)
        HALLedSetConstant(hal, map->rxLedIdx,
                          node->rxKey ? HAL_LED_MODE_ON : HAL_LED_MODE_OFF);
}

/**
 * @brief Look up the node named in action, update its state, drive its LEDs,
 *        then free the action and remove it from the listener queue.
 *
 * If the node does not yet exist in the tree it is created on the fly so that
 * connected nodes are tracked automatically without pre-configuration.
 */
static void findAndUpdateNodeForAction(rbtree *nodeTree, Listener *lMem, LogAction *action,
                                       AppConfig *cfg, HAL *hal)
{
    rbnode  *node    = rb_find(nodeTree, action->name);
    ASLNode *aslNode;

    /* Node tree reads and writes are lock-free: only the main thread ever
     * touches nodeTree, so no synchronization is needed here. */
    if (node != NULL) {
        aslNode = node->data;
    } else {
        aslNode = makeASLNode(action->name);
        if (aslNode && !rb_insert(nodeTree, aslNode)) {
            /* rb_insert OOM: node not in tree, free it to avoid a leak. */
            destroyASLNode(aslNode);
            aslNode = NULL;
        }
    }

    if (aslNode) {
        updateASLNode(aslNode, action->LastUpdate, action->action);
        updateLedsForNode(cfg, hal, aslNode);
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