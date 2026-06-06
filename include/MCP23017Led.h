/**
 * @file MCP23017Led.h
 * @brief MCP23017 LED driver supporting constant, one-shot, and blinking modes.
 *
 * Mirrors the gpioLed driver interface exactly. The only difference from
 * gpioLed is that pin state is written via mcp23017SetOutputPin() (I2C) rather
 * than digitalWrite(). Blink and one-shot timing use the same POSIX
 * CLOCK_REALTIME interval timers with SIGEV_THREAD delivery.
 *
 * Lock ordering: LED lock → device i2cLock (never the reverse).
 */

#ifndef MCP23017_LED_H
#define MCP23017_LED_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include "MCP23017Device.h"

typedef enum {
    MCP23017_LED_SUCCESS           =  0,
    MCP23017_LED_UNDEFINED_ERROR   = -1,
    MCP23017_LED_ERROR_NULL        = -2,
    MCP23017_LED_ERROR_INVALID_MODE = -3,
} MCP23017LedStatus_t;

typedef enum {
    MCP23017_LED_OFF      = 0,
    MCP23017_LED_ON       = 1,
    MCP23017_LED_ONESHOT,     /* LED is on; timer will turn it off once */
    MCP23017_LED_BLINK_ON,    /* LED is on; timer will switch to BLINK_OFF */
    MCP23017_LED_BLINK_OFF,   /* LED is off; timer will switch to BLINK_ON */
} MCP23017LedMode_t;

typedef struct {
    MCP23017Device_t *device;
    uint8_t           port;            /* 0 = port A, 1 = port B */
    uint8_t           pin;             /* pin within the port (0-7) */
    pthread_mutex_t   lock;
    bool              valid;           /* cleared before free() to stop in-flight callbacks */
    MCP23017LedMode_t mode;
    timer_t           timerId;
    struct sigevent   signalEvent;
    struct itimerspec timerTrigger;
    struct timespec   primaryTime;     /* on-duration for blink */
    struct timespec   secondaryTime;   /* off-duration for blink */
} MCP23017LedMemory_t;

/**
 * @brief Allocate and configure an MCP23017 LED pin as an output driven low.
 * @param device  Shared device returned by mcp23017DeviceCreate().
 * @param port    0 = port A, 1 = port B.
 * @param pin     Pin within the port (0–7).
 * @return Pointer to allocated memory on success, NULL on failure.
 */
MCP23017LedMemory_t *mcp23017LedInit(MCP23017Device_t *device, uint8_t port, uint8_t pin);

/**
 * @brief Cancel any active timer, drive the pin low, reconfigure as input,
 *        and free all resources.
 */
MCP23017LedStatus_t mcp23017LedDeinit(void *ledMemory);

/**
 * @brief Set the LED to a steady state (MCP23017_LED_ON or MCP23017_LED_OFF).
 *        Cancels any active timer.
 */
MCP23017LedStatus_t mcp23017LedSetConstant(void *ledMemory, MCP23017LedMode_t mode);

/**
 * @brief Turn the LED on for durationMs milliseconds, then turn it off.
 */
MCP23017LedStatus_t mcp23017LedSetOneShot(void *ledMemory, unsigned long durationMs);

/**
 * @brief Blink the LED continuously with independent on/off durations.
 */
MCP23017LedStatus_t mcp23017LedSetBlink(void *ledMemory,
                                          unsigned long onDurationMs,
                                          unsigned long offDurationMs);

#endif /* MCP23017_LED_H */
