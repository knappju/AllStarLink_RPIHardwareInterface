/**
 * @file hardwareManager.h
 * @brief Legacy direct-GPIO hardware manager (buttons and LEDs via wiringPi).
 *
 * @deprecated This module will be removed once the HAL is fully operational.
 *             New code should use HAL.h instead.
 */

#ifndef HARDWAREMANAGER_H
#define HARDWAREMANAGER_H

#include <stdlib.h>
#include <wiringPi.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include "globalDefines.h"

/* Period of the hardware polling thread in milliseconds. */
#define HARDWARE_THREAD_TIME_MS 5

/* Button state values stored in Button.state. */
#define BUTTON_STATE_UNDEFINED 0  /* initial state before first debounce completes */
#define BUTTON_STATE_RELEASED  1
#define BUTTON_STATE_PRESSED   2

typedef struct {
    /* Fields below are guarded by Hardware.hardwareLock. */
    int     pin;
    uint8_t debounceVals;   /* shift register: 0x00=released, 0xFF=pressed */
    bool    debouncing;     /* true while debounceVals is between 0x00 and 0xFF */
    uint8_t state;          /* BUTTON_STATE_* value after debouncing */
} Button;

typedef struct {
    /* Fields below are guarded by Hardware.hardwareLock. */
    int  pin;
    bool state;   /* true = HIGH/on */
} Led;

typedef struct {
    pthread_t       id;
    pthread_mutex_t hardwareLock;
    long            lastReadMs;
    bool            halt;           /* set true to stop the hardwareManager thread */
    /* Fields below are guarded by hardwareLock. */
    Button buttons[NUM_OF_BUTTONS];
    Led    leds[NUM_OF_LEDS];
} Hardware;

/**
 * @brief Initialize all GPIO pins, run the LED test sequence, and start the
 *        hardware polling thread.
 * @param hwMem Caller-allocated Hardware struct to initialize.
 * @return 0 on success, -1 if hwMem is NULL.
 */
int initHardware(Hardware *hwMem);

/**
 * @brief Cycle each LED off→on→off to verify hardware connectivity at startup.
 * @param hwMem Initialized Hardware struct.
 * @return 0 on success, -1 if hwMem is NULL.
 */
int testLeds(Hardware *hwMem);

#endif /* HARDWAREMANAGER_H */
