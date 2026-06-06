/**
 * @file MCP23017Device.h
 * @brief Shared MCP23017 chip object: I2C access and interrupt dispatch.
 *
 * One MCP23017Device_t represents one physical chip on the I2C bus. Multiple
 * button and LED driver instances that share the same chip hold a pointer to
 * the same device. All I2C transactions are serialized by i2cLock.
 *
 * Interrupt dispatch: The MCP23017's INTA/INTB output pins are wired to real
 * RPi GPIO pins (wiringPi numbering). When an interrupt fires,
 * mcp23017ChipInterrupt() reads INTF to identify which chip pins changed and
 * arms the debounce timer of each registered button via a stored callback.
 *
 * Typical usage:
 *   MCP23017Device_t *dev = mcp23017DeviceCreate(1, 0x20);
 *   // pass dev to mcp23017LedInit / mcp23017ButtonInit
 *   mcp23017DeviceDestroy(dev);
 */

#ifndef MCP23017_DEVICE_H
#define MCP23017_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

/* ── MCP23017 register addresses (BANK=0, default after power-on) ──────── */

#define MCP23017_IODIRA    0x00  /* I/O direction A: 1=input, 0=output */
#define MCP23017_IODIRB    0x01
#define MCP23017_GPINTENA  0x04  /* Interrupt-on-change enable A */
#define MCP23017_GPINTENB  0x05
#define MCP23017_INTCONA   0x08  /* Interrupt control A: 0=compare prev, 1=compare DEFVAL */
#define MCP23017_INTCONB   0x09
#define MCP23017_IOCON     0x0A  /* Device configuration */
#define MCP23017_GPPUA     0x0C  /* Pull-up resistor enable A */
#define MCP23017_GPPUB     0x0D
#define MCP23017_INTFA     0x0E  /* Interrupt flag A: bitmask of pins that triggered */
#define MCP23017_INTFB     0x0F
#define MCP23017_INTCAPA   0x10  /* Interrupt capture A: pin state at interrupt time */
#define MCP23017_INTCAPB   0x11
#define MCP23017_GPIOA     0x12  /* GPIO port A: current pin levels */
#define MCP23017_GPIOB     0x13
#define MCP23017_OLATA     0x14  /* Output latch A */
#define MCP23017_OLATB     0x15

/* ── Interrupt dispatch slot ────────────────────────────────────────────── */

/* One slot per pin (0-7) per port. The button driver stores its own arm
 * function and memory pointer here; the ISR calls armFn(data) to start the
 * debounce timer without knowing anything about the button internals. */
typedef struct {
    void  *data;
    void (*armFn)(void *data);
} MCP23017DispatchSlot_t;

/* ── Device struct ──────────────────────────────────────────────────────── */

typedef struct {
    int             fd;           /* I2C file descriptor (/dev/i2c-N) */
    uint8_t         i2cAddress;
    pthread_mutex_t i2cLock;      /* serializes all register I/O and dispatch table access */

    /* Cached output latch values. OLATA/OLATB must be used for read-modify-write
     * on output pins; reading GPIOA/B reflects actual pin levels which may
     * differ when pins are externally driven. */
    uint8_t outputLatchA;
    uint8_t outputLatchB;

    /* Per-port interrupt dispatch tables (one slot per pin, NULL armFn = unused). */
    MCP23017DispatchSlot_t buttonDispatchA[8];
    MCP23017DispatchSlot_t buttonDispatchB[8];

    /* Real RPi GPIO pins (wiringPi numbering) wired to INTA/INTB. -1 = unused. */
    int  intGpioPinA;
    int  intGpioPinB;

    /* Whether wiringPiISR has been registered for each port's interrupt GPIO. */
    bool intARegistered;
    bool intBRegistered;
} MCP23017Device_t;

/* ── Status codes ───────────────────────────────────────────────────────── */

typedef enum {
    MCP23017_DEVICE_SUCCESS          =  0,
    MCP23017_DEVICE_ERROR_UNDEFINED  = -1,
    MCP23017_DEVICE_ERROR_NULL       = -2,
    MCP23017_DEVICE_ERROR_I2C_OPEN  = -3,
    MCP23017_DEVICE_ERROR_I2C_WRITE = -4,
    MCP23017_DEVICE_ERROR_I2C_READ  = -5,
} MCP23017DeviceStatus_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Open the I2C bus, set the slave address, read the initial output
 *        latch state, and return a heap-allocated device object.
 * @param i2cBus     Linux I2C bus number (e.g. 1 for /dev/i2c-1).
 * @param i2cAddress 7-bit I2C address (0x20–0x27 for MCP23017).
 * @return Pointer to device on success, NULL on failure.
 */
MCP23017Device_t *mcp23017DeviceCreate(int i2cBus, uint8_t i2cAddress);

/**
 * @brief Close the I2C fd, destroy the mutex, and free the device.
 */
void mcp23017DeviceDestroy(MCP23017Device_t *device);

/* ── Register I/O (caller must hold i2cLock) ────────────────────────────── */

/**
 * @brief Write one byte to a register.
 */
MCP23017DeviceStatus_t mcp23017WriteReg(MCP23017Device_t *device, uint8_t reg, uint8_t val);

/**
 * @brief Read one byte from a register.
 */
MCP23017DeviceStatus_t mcp23017ReadReg(MCP23017Device_t *device, uint8_t reg, uint8_t *val);

/* ── Pin I/O ────────────────────────────────────────────────────────────── */

/**
 * @brief Drive one output pin high or low, updating the cached output latch.
 *        Acquires i2cLock internally.
 * @param port  0 = port A, 1 = port B.
 * @param pin   Pin number within the port (0–7).
 * @param high  true = drive high, false = drive low.
 */
MCP23017DeviceStatus_t mcp23017SetOutputPin(MCP23017Device_t *device,
                                             uint8_t port, uint8_t pin, bool high);

/**
 * @brief Read the current stable state of one input pin from GPIOA/B.
 *        Acquires i2cLock internally.
 * @param val  Output: 1 if pin is high, 0 if low.
 */
MCP23017DeviceStatus_t mcp23017ReadInputPin(MCP23017Device_t *device,
                                             uint8_t port, uint8_t pin, uint8_t *val);

/* ── Interrupt infrastructure ───────────────────────────────────────────── */

/**
 * @brief Register a button's arm callback into the per-port dispatch table.
 *        Safe to call from the button driver's init function.
 *        Acquires i2cLock internally.
 */
void mcp23017RegisterButtonDispatch(MCP23017Device_t *device, uint8_t port, uint8_t pin,
                                     void *buttonMem, void (*armFn)(void *));

/**
 * @brief Remove a button from the dispatch table (called on button deinit).
 *        Acquires i2cLock internally.
 */
void mcp23017UnregisterButtonDispatch(MCP23017Device_t *device, uint8_t port, uint8_t pin);

/**
 * @brief Register the wiringPiISR for a port's interrupt GPIO pin.
 *        Safe to call multiple times; only the first call per port takes effect.
 *        Must be called after wiringPiSetup() (guaranteed when called from HALLoadConfig).
 * @param port       0 for port A (INTA), 1 for port B (INTB).
 * @param intGpioPin wiringPi pin number of the RPi GPIO wired to INTA or INTB.
 */
void mcp23017RegisterPortISR(MCP23017Device_t *device, uint8_t port, int intGpioPin);

/*
 * X-macro list of wiringPi pin numbers (0-39) used to generate one ISR stub
 * per possible interrupt GPIO pin. Same range as BUTTON_ISR_LIST in gpioButton.h.
 */
#define MCP23017_INT_ISR_LIST \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)  \
    X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) \
    X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) \
    X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39)

#endif /* MCP23017_DEVICE_H */
