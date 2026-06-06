/**
 * @file MCP23017Button.h
 * @brief MCP23017 button driver using chip interrupts with software debouncing.
 *
 * Mirrors the gpioButton driver interface. The interrupt flow differs: instead
 * of one wiringPiISR per button, the MCP23017's INTA/INTB output (wired to a
 * real RPi GPIO) fires a shared ISR that reads the chip's INTF register and
 * dispatches to each button's individual debounce timer.
 *
 * The MCP23017 only supports internal pull-ups (no pull-downs). Pass
 * enablePullup=true to enable the chip's pull-up on the button pin.
 */

#ifndef MCP23017_BUTTON_H
#define MCP23017_BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include "MCP23017Device.h"

typedef enum {
    MCP23017_BUTTON_SUCCESS                =  0,
    MCP23017_BUTTON_UNDEFINED_ERROR        = -1,
    MCP23017_BUTTON_ERROR_NULL             = -2,
    MCP23017_BUTTON_ERROR_ALREADY_REGISTERED = -3,
    MCP23017_BUTTON_ERROR_NOT_REGISTERED   = -4,
} MCP23017ButtonStatus_t;

typedef struct {
    MCP23017Device_t *device;
    uint8_t           port;             /* 0 = port A, 1 = port B */
    uint8_t           pin;              /* pin within port (0-7) */
    int               debounceTimeMs;
    void            (*cb)(uint8_t state);
    bool              cbEnabled;
    uint8_t           state;            /* most recently confirmed pin level */
    unsigned long     lastChangeTimeMs; /* millis() timestamp of last state change */
    timer_t           debounceTimer;
} MCP23017ButtonMemory_t;

/**
 * @brief Allocate and configure an MCP23017 button pin.
 *
 * Configures the pin as input, optionally enables the chip pull-up, enables
 * interrupt-on-change (compare-to-previous-value mode), registers the button
 * in the device's interrupt dispatch table, and registers the wiringPiISR for
 * the port's interrupt GPIO pin (first button on a port triggers the ISR
 * registration; subsequent buttons on the same port reuse it).
 *
 * @param device        Shared device returned by mcp23017DeviceCreate().
 * @param port          0 = port A, 1 = port B.
 * @param pin           Pin within the port (0–7).
 * @param enablePullup  true to enable the MCP23017 internal pull-up resistor.
 * @param debounceTimeMs Minimum milliseconds of pin stability before a state
 *                       change is confirmed.
 * @param intGpioPin    wiringPi pin number of the RPi GPIO wired to INTA (port A)
 *                      or INTB (port B). Pass -1 to skip ISR registration
 *                      (button will never receive interrupt-driven updates).
 * @return Pointer to allocated memory on success, NULL on failure.
 */
MCP23017ButtonMemory_t *mcp23017ButtonInit(MCP23017Device_t *device,
                                            uint8_t port, uint8_t pin,
                                            bool enablePullup,
                                            int debounceTimeMs,
                                            int intGpioPin);

/**
 * @brief Cancel the debounce timer, unregister from the device dispatch table,
 *        and free all resources.
 */
MCP23017ButtonStatus_t mcp23017ButtonDeinit(void *buttonMemory);

/** @brief Return the most recently confirmed pin level. */
MCP23017ButtonStatus_t mcp23017ButtonRead(void *buttonMemory, uint8_t *state);

/** @brief Return how long (ms) the button has been in its current state. */
MCP23017ButtonStatus_t mcp23017ButtonGetTimeInState(void *buttonMemory,
                                                     unsigned long *timeInState);

/** @brief Register a state-change callback. Only one callback per button. */
MCP23017ButtonStatus_t mcp23017ButtonRegisterCB(void *buttonMemory,
                                                  void (*cb)(uint8_t state));

/** @brief Remove a previously registered callback. */
MCP23017ButtonStatus_t mcp23017ButtonUnregisterCB(void *buttonMemory);

/** @brief Allow the registered callback to fire. */
MCP23017ButtonStatus_t mcp23017ButtonEnableCB(void *buttonMemory);

/** @brief Suppress the registered callback without unregistering it. */
MCP23017ButtonStatus_t mcp23017ButtonDisableCB(void *buttonMemory);

/**
 * @brief Arm the debounce timer for a button.
 *
 * Called by the MCP23017Device ISR dispatch via the stored function pointer.
 * Public so the device layer can store a pointer to it without including
 * button internals.
 */
void mcp23017ButtonArmDebounce(void *buttonMemory);

#endif /* MCP23017_BUTTON_H */
