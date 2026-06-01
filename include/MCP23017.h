/**
 * @file MCP23017.h
 * @brief Driver interface for the MCP23017 16-bit I2C I/O expander, used here
 *        to drive LEDs via an I2C bus rather than direct GPIO pins.
 *
 * @note This driver is a stub. Full implementation is pending.
 */

#ifndef MCP23017_H
#define MCP23017_H

typedef enum {
    MCP23017_SUCCESS = 0,
    MCP23017_UNDEFINED_ERROR = -1,
    MCP23017_ERROR_NULL_POINTER,
    MCP23017_ERROR_INVALID_MODE
} MCP23017Status_t;

typedef enum {
    MCP23017_LED_OFF = 0,
    MCP23017_LED_ON = 1,
    MCP23017_LED_ONESHOT,
    MCP23017_LED_BLINK_ON,
    MCP23017_LED_BLINK_OFF
} MCP23017LedMode_t;

typedef struct {
    /* TODO: populate with I2C address, port, and pin fields once the
     * hardware pin layout is finalized. */
} MCP23017Memory_t;

/* TODO: pin/address parameters TBD once hardware layout is finalized. */
MCP23017Memory_t *MCP23017Init(void);
MCP23017Status_t  MCP23017Deinit(void *ledMemory);
MCP23017Status_t  MCP23017SetConstant(void *ledMemory, MCP23017LedMode_t mode);
MCP23017Status_t  MCP23017SetOneShot(void *ledMemory, unsigned long durationMs);
MCP23017Status_t  MCP23017SetBlink(void *ledMemory, unsigned long onDurationMs, unsigned long offDurationMs);

#endif /* MCP23017_H */
