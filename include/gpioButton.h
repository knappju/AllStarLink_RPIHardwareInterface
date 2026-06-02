/**
 * @file gpioButton.h
 * @brief GPIO button driver using wiringPi ISRs with software debouncing.
 *
 * Each button is identified by its physical RPi pin number (1–40). Interrupts
 * are dispatched through a statically-generated ISR table so wiringPiISR can
 * receive a unique, zero-argument function per pin. A lookup table maps pin
 * numbers back to their gpioButtonMemory_t instances at ISR time.
 */

#ifndef GPIO_BUTTON_H
#define GPIO_BUTTON_H

#include <wiringPi.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

typedef enum {
    GPIO_BUTTON_SUCCESS = 0,
    GPIO_BUTTON_UNDEFINED_ERROR = -1,
    GPIO_BUTTON_ERROR_NULL_POINTER,        /* a required pointer argument was NULL */
    GPIO_BUTTON_ERROR_INVALID_STATE,
    GPIO_BUTTON_ERROR_ALREADY_REGISTERED,  /* a callback is already attached */
    GPIO_BUTTON_ERROR_NOT_REGISTERED,      /* no callback is attached */
} gpioButtonStatus_t;

typedef struct {
    int pin;                          /* physical RPi pin number (1-40) */
    int pull;                         /* wiringPi pull resistor: PUD_UP/DOWN/OFF */
    int debounceTimeMs;               /* minimum ms between accepted interrupts */
    int interruptEdge;                /* INT_EDGE_RISING / FALLING / BOTH */
    void (*cb)(uint8_t state);        /* optional user callback, called on state change */
    bool cbEnabled;                   /* when false, cb is suppressed even if registered */
    int state;                        /* most recently debounced pin level */
    unsigned long lastInterruptTime;  /* millis() timestamp of last accepted ISR */
    timer_t debounceTimer;
} gpioButtonMemory_t;

/**
 * @brief Allocate and configure a GPIO button.
 * @param pin           Physical RPi pin number (1-40).
 * @param pull          Pull resistor: PUD_UP, PUD_DOWN, or PUD_OFF.
 * @param debounceTimeMs Minimum milliseconds between accepted interrupts.
 * @param interruptEdge  INT_EDGE_RISING, INT_EDGE_FALLING, or INT_EDGE_BOTH.
 * @return Pointer to allocated memory on success, NULL on failure.
 */
gpioButtonMemory_t *gpioButtonInit(int pin, int pull, int debounceTimeMs, int interruptEdge);

/**
 * @brief Release all resources for a button and detach its ISR.
 * @param buttonMemory Pointer returned by gpioButtonInit().
 */
gpioButtonStatus_t gpioButtonDeinit(void *buttonMemory);

/**
 * @brief Read the last debounced state of the button.
 * @param buttonMemory Pointer returned by gpioButtonInit().
 * @param state        Output: current pin level.
 */
gpioButtonStatus_t gpioButtonRead(void *buttonMemory, uint8_t *state);

/**
 * @brief Return how long (ms) the button has been in its current state.
 * @param buttonMemory Pointer returned by gpioButtonInit().
 * @param timeInState  Output: elapsed milliseconds since last state change.
 */
gpioButtonStatus_t gpioButtonGetTimeInState(void *buttonMemory, unsigned long *timeInState);

/**
 * @brief Register a state-change callback. Only one callback per button.
 * @param buttonMemory Pointer returned by gpioButtonInit().
 * @param cb           Function called with the new pin level on each state change.
 */
gpioButtonStatus_t gpioButtonRegisterCB(void *buttonMemory, void (*cb)(uint8_t state));

/** @brief Remove a previously registered callback. */
gpioButtonStatus_t gpioButtonUnregisterCB(void *buttonMemory);

/** @brief Allow the registered callback to fire. */
gpioButtonStatus_t gpioButtonEnableCB(void *buttonMemory);

/** @brief Suppress the registered callback without unregistering it. */
gpioButtonStatus_t gpioButtonDisableCB(void *buttonMemory);

/*
 * X-macro list of pin numbers (1-40) used to generate one ISR stub per pin.
 * wiringPiISR requires a distinct zero-argument function for each pin; these
 * stubs delegate immediately to the shared buttonInterupt() handler.
 */
#define BUTTON_ISR_LIST \
    X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)  X(10) \
    X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) \
    X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) \
    X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40)

#endif /* GPIO_BUTTON_H */
