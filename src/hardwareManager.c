/**
 * @file hardwareManager.c
 * @brief Legacy direct-GPIO hardware manager implementation.
 *
 * @deprecated Will be removed once the HAL is fully operational.
 *
 * Runs a polling thread (hardwareManager) at HARDWARE_THREAD_TIME_MS intervals.
 * Button debouncing works by shift-registering the raw GPIO value into a uint8:
 * when all 8 samples read 0x00 the button is confirmed released; 0xFF confirmed
 * pressed. Any intermediate value means the button is still bouncing.
 *
 * Button actions invoke Asterisk via system("asterisk -rx ...") with hard-coded
 * node numbers. Each button cycles through four states: monitor→link→disconnect
 * (with the disconnect state shared between index 1 and 3 in the cycle).
 *
 * TODO: replace system() calls with an action queue consumed by the main thread.
 */

#include "hardwareManager.h"

/*****************************************************************************
 * DEPRECATION NOTICE – remove this file when the HAL is fully operational.
 *****************************************************************************/

/* Hard-coded pin assignments.
 * TODO: load from AppConfig.json so hardware layout can change without recompiling. */
static int defineLeds[NUM_OF_LEDS]       = {1, 4, 5, 6, 26, 27, 28, 29, 21, 22, 23, 24};
static int defineButtons[NUM_OF_BUTTONS] = {7, 0, 2, 3};

/* Private function declarations */
static void *hardwareManager(void *args);
static int   cleanHardware(Hardware *hwMem);
static int   buttonAction(int buttonIndex, uint8_t state);
static long  currentMillis(void);

/*
 * Hardware polling thread. Runs until hwMem->halt is set TRUE.
 *
 * Each iteration reads all button pins into a shift register for debouncing
 * and writes the current LED state to the corresponding GPIO output.
 */
static void *hardwareManager(void *args)
{
    if (args == NULL) {
        pthread_exit(NULL);
    }

    Hardware *hwMem = (Hardware *)args;

    while (!hwMem->halt) {
        /* Small sleep to yield CPU; actual period is governed by lastReadMs. */
        delay(1);
        long currentTimeMs = currentMillis();

        if ((currentTimeMs - hwMem->lastReadMs) < HARDWARE_THREAD_TIME_MS) {
            continue;
        }

        hwMem->lastReadMs = currentTimeMs;
        pthread_mutex_lock(&hwMem->hardwareLock);

        /* TODO: consider interrupt-driven button detection to reduce CPU usage. */
        for (int buttonIndex = 0; buttonIndex < NUM_OF_BUTTONS; buttonIndex++) {
            /* Shift in the current GPIO level. */
            hwMem->buttons[buttonIndex].debounceVals =
                (uint8_t)(hwMem->buttons[buttonIndex].debounceVals << 1) +
                digitalRead(hwMem->buttons[buttonIndex].pin);

            switch (hwMem->buttons[buttonIndex].debounceVals) {
                case 0x00: /* all samples LOW → button confirmed released */
                    if (hwMem->buttons[buttonIndex].state == BUTTON_STATE_PRESSED) {
                        buttonAction(buttonIndex, hwMem->buttons[buttonIndex].state);
                    }
                    hwMem->buttons[buttonIndex].debouncing = FALSE;
                    hwMem->buttons[buttonIndex].state = BUTTON_STATE_RELEASED;
                    break;

                case 0xFF: /* all samples HIGH → button confirmed pressed */
                    if (hwMem->buttons[buttonIndex].state == BUTTON_STATE_RELEASED) {
                        buttonAction(buttonIndex, hwMem->buttons[buttonIndex].state);
                    }
                    hwMem->buttons[buttonIndex].debouncing = FALSE;
                    hwMem->buttons[buttonIndex].state = BUTTON_STATE_PRESSED;
                    break;

                default: /* mixed samples → still bouncing */
                    hwMem->buttons[buttonIndex].debouncing = TRUE;
                    break;
            }
        }

        for (int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++) {
            digitalWrite(hwMem->leds[ledIndex].pin, hwMem->leds[ledIndex].state);
        }

        pthread_mutex_unlock(&hwMem->hardwareLock);
    }

    cleanHardware(hwMem);
    pthread_exit(NULL);
}

int initHardware(Hardware *hwMem)
{
    if (hwMem == NULL) return -1;

    wiringPiSetup();

    for (int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++) {
        hwMem->leds[ledIndex] = (Led){ .pin = defineLeds[ledIndex], .state = FALSE };
        pinMode(hwMem->leds[ledIndex].pin, OUTPUT);
        digitalWrite(hwMem->leds[ledIndex].pin, LOW);
    }
    testLeds(hwMem);

    for (int buttonIndex = 0; buttonIndex < NUM_OF_BUTTONS; buttonIndex++) {
        hwMem->buttons[buttonIndex] = (Button){
            .pin          = defineButtons[buttonIndex],
            .debouncing   = TRUE,
            .debounceVals = 55,           /* arbitrary mid-range value → treated as bouncing */
            .state        = BUTTON_STATE_UNDEFINED
        };
        pinMode(hwMem->buttons[buttonIndex].pin, INPUT);
        pullUpDnControl(hwMem->buttons[buttonIndex].pin, PUD_OFF);
    }

    hwMem->halt      = FALSE;
    hwMem->lastReadMs = currentMillis();
    pthread_mutex_init(&hwMem->hardwareLock, NULL);
    pthread_create(&hwMem->id, NULL, hardwareManager, hwMem);

    return 0;
}

/* Drive all LED pins LOW and reconfigure them as inputs to ensure the
 * hardware is in a safe state when the thread exits. */
static int cleanHardware(Hardware *hwMem)
{
    if (hwMem == NULL) return -1;
    for (int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++) {
        digitalWrite(hwMem->leds[ledIndex].pin, LOW);
        pinMode(hwMem->leds[ledIndex].pin, INPUT);
    }
    return 0;
}

int testLeds(Hardware *hwMem)
{
    if (hwMem == NULL) return -1;
    for (int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++) {
        digitalWrite(hwMem->leds[ledIndex].pin, LOW);
        delay(50);
        digitalWrite(hwMem->leds[ledIndex].pin, HIGH);
        delay(50);
        digitalWrite(hwMem->leds[ledIndex].pin, LOW);
    }
    return 0;
}

/*
 * Dispatch an Asterisk rpt command when a button is pressed.
 *
 * Each button cycles through a 4-state sequence:
 *   state 0 → *2 (monitor/link)
 *   state 1 → *1 (unlink)
 *   state 2 → *3 (disconnect)
 *   state 3 → *1 (unlink again before re-linking)
 *
 * The static toggle counters persist between calls so the cycle survives
 * across multiple button presses.
 */
static int buttonAction(int buttonIndex, uint8_t state)
{
    static uint8_t toggle[NUM_OF_BUTTONS] = {0};

    /* Actions are only triggered on the press event, not release. */
    if (state != BUTTON_STATE_PRESSED) {
        return 0;
    }

    /* Node numbers and rpt commands are hard-coded for the current deployment.
     * TODO: load from config so this function does not need recompilation for
     * a different node layout. */
    static const char *cmds[NUM_OF_BUTTONS][3] = {
        /* K8SN (2324) */
        { "asterisk -rx \"rpt fun 443240 *22324\"",
          "asterisk -rx \"rpt fun 443240 *12324\"",
          "asterisk -rx \"rpt fun 443240 *32324\"" },
        /* Seattle (2462) */
        { "asterisk -rx \"rpt fun 443240 *22462\"",
          "asterisk -rx \"rpt fun 443240 *12462\"",
          "asterisk -rx \"rpt fun 443240 *32462\"" },
        /* W8IRA (472440) */
        { "asterisk -rx \"rpt fun 443240 *2472440\"",
          "asterisk -rx \"rpt fun 443240 *1472440\"",
          "asterisk -rx \"rpt fun 443240 *3472440\"" },
        /* East Coast Reflector (27339) */
        { "asterisk -rx \"rpt fun 443240 *227339\"",
          "asterisk -rx \"rpt fun 443240 *127339\"",
          "asterisk -rx \"rpt fun 443240 *327339\"" },
    };

    if (buttonIndex < 0 || buttonIndex >= NUM_OF_BUTTONS) {
        return -1;
    }

    /* Cycle: 0→link, 1→unlink, 2→disconnect, 3→unlink (same cmd as 1) */
    int cmdIdx;
    switch (toggle[buttonIndex]) {
        case 0:  cmdIdx = 0; break; /* monitor/link */
        case 1:  cmdIdx = 1; break; /* unlink */
        case 2:  cmdIdx = 2; break; /* disconnect */
        case 3:  cmdIdx = 1; break; /* unlink again */
        default: cmdIdx = 0; break;
    }

    system(cmds[buttonIndex][cmdIdx]);
    toggle[buttonIndex] = (toggle[buttonIndex] + 1) % 4;

    return 0;
}

/* Return the current system time in milliseconds. */
static long currentMillis(void)
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}
