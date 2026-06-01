/**
 * @file gpioLed.h
 * @brief GPIO LED driver supporting constant, one-shot, and blinking modes.
 *
 * Blinking and one-shot timing use POSIX CLOCK_REALTIME interval timers with
 * SIGEV_THREAD delivery so no dedicated thread is needed. All mode state that
 * the timer callback touches is protected by a per-LED mutex.
 */

#ifndef GPIO_LED_H
#define GPIO_LED_H

#include <wiringPi.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>

typedef enum {
    GPIO_LED_SUCCESS = 0,
    GPIO_LED_UNDEFINED_ERROR = -1,  /* catch-all; check errno for details */
    GPIO_LED_ERROR_NULL_POINTER,
    GPIO_LED_ERROR_INVALID_MODE     /* mode is not valid for the called function */
} gpioLedStatus_t;

typedef enum {
    GPIO_LED_OFF       = 0,
    GPIO_LED_ON        = 1,
    GPIO_LED_ONESHOT,    /* LED is on; timer will turn it off once */
    GPIO_LED_BLINK_ON,   /* LED is on; timer will switch to BLINK_OFF */
    GPIO_LED_BLINK_OFF   /* LED is off; timer will switch to BLINK_ON */
} gpioLedMode_t;

typedef struct {
    int pin;
    pthread_mutex_t lock;
    /* Fields below are protected by lock and also accessed from the timer
     * callback thread. */
    bool valid;                     /* set false before free() to stop the callback */
    gpioLedMode_t mode;
    timer_t timerId;
    struct sigevent signalEvent;    /* timer delivery config (set once at init) */
    struct itimerspec timerTrigger; /* reused each timer_settime() call */
    struct timespec primaryTime;    /* on-duration for blink mode */
    struct timespec secondaryTime;  /* off-duration for blink mode */
} gpioLedMemory_t;

/**
 * @brief Allocate and configure a GPIO LED, setting the pin to OUTPUT/LOW.
 * @param pin Physical RPi pin number (1-40).
 * @return Pointer to allocated memory on success, NULL on failure.
 */
gpioLedMemory_t *gpioLedInit(int pin);

/**
 * @brief Cancel any active timer, drive the pin LOW, and free all resources.
 * @param ledMemory Pointer returned by gpioLedInit().
 */
gpioLedStatus_t gpioLedDeinit(void *ledMemory);

/**
 * @brief Set the LED to a steady state (GPIO_LED_ON or GPIO_LED_OFF).
 *        Cancels any active one-shot or blink timer.
 * @param ledMemory Pointer returned by gpioLedInit().
 * @param mode      GPIO_LED_ON or GPIO_LED_OFF.
 */
gpioLedStatus_t gpioLedSetConstant(void *ledMemory, gpioLedMode_t mode);

/**
 * @brief Turn the LED on for durationMs milliseconds, then turn it off.
 * @param ledMemory  Pointer returned by gpioLedInit().
 * @param durationMs On-time in milliseconds.
 */
gpioLedStatus_t gpioLedSetOneShot(void *ledMemory, unsigned long durationMs);

/**
 * @brief Blink the LED continuously with independent on/off durations.
 * @param ledMemory    Pointer returned by gpioLedInit().
 * @param onDurationMs  Time LED is on per cycle (ms).
 * @param offDurationMs Time LED is off per cycle (ms).
 */
gpioLedStatus_t gpioLedSetBlink(void *ledMemory, unsigned long onDurationMs, unsigned long offDurationMs);

#endif /* GPIO_LED_H */
