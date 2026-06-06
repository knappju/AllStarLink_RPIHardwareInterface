/**
 * @file MCP23017Device.c
 * @brief MCP23017 shared device: I2C lifecycle, register I/O, ISR dispatch.
 *
 * The ISR dispatch flow:
 *   1. INTA/INTB fires on a real RPi GPIO → zero-arg ISR stub → mcp23017ChipInterrupt()
 *   2. Read INTFA/B to see which pins triggered.
 *   3. Read INTCAPA/B to clear the interrupt (value discarded; the debounce
 *      timer callback reads GPIOA/B for the confirmed stable state).
 *   4. For each set bit, call dispatch[pin].armFn(dispatch[pin].data) to arm
 *      the button's debounce timer.
 *
 * The i2cLock is held across the I2C reads AND the dispatch loop so that
 * button deinit (which clears dispatch slots under the same lock) cannot race
 * with an in-progress dispatch.
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <wiringPi.h>
#include "MCP23017Device.h"

/* ── ISR infrastructure ─────────────────────────────────────────────────── */

static void mcp23017ChipInterrupt(int gpioPin);

/* Maps a real RPi GPIO pin to the device+port it services. */
typedef struct {
    MCP23017Device_t *device;
    uint8_t           port;   /* 0 = port A, 1 = port B */
} MCP23017IntSlot_t;

static MCP23017IntSlot_t intSlotLookup[40] = {0};

/* Generate one zero-arg ISR stub per wiringPi pin (0-39). Each stub calls
 * the shared handler with its pin number baked in at compile time. */
#define X(n) static void mcp23017_int_isr_##n(void) { mcp23017ChipInterrupt(n); }
MCP23017_INT_ISR_LIST
#undef X

static void (*mcp23017_int_isr_table[])(void) = {
#define X(n) mcp23017_int_isr_##n,
    MCP23017_INT_ISR_LIST
#undef X
};

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

MCP23017Device_t *mcp23017DeviceCreate(int i2cBus, uint8_t i2cAddress)
{
    char path[16];
    snprintf(path, sizeof(path), "/dev/i2c-%d", i2cBus);

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "mcp23017DeviceCreate: failed to open %s\n", path);
        return NULL;
    }

    if (ioctl(fd, I2C_SLAVE, i2cAddress) < 0) {
        fprintf(stderr, "mcp23017DeviceCreate: ioctl I2C_SLAVE failed "
                        "for address 0x%02X\n", i2cAddress);
        close(fd);
        return NULL;
    }

    MCP23017Device_t *dev = calloc(1, sizeof(MCP23017Device_t));
    if (!dev) {
        close(fd);
        return NULL;
    }

    if (pthread_mutex_init(&dev->i2cLock, NULL) != 0) {
        free(dev);
        close(fd);
        return NULL;
    }

    dev->fd          = fd;
    dev->i2cAddress  = i2cAddress;
    dev->intGpioPinA = -1;
    dev->intGpioPinB = -1;

    pthread_mutex_lock(&dev->i2cLock);

    /* Probe the chip — if nothing is at this address the read will fail with
     * ENXIO rather than silently returning garbage. */
    uint8_t probe = 0;
    if (mcp23017ReadReg(dev, MCP23017_IODIRA, &probe) != MCP23017_DEVICE_SUCCESS) {
        fprintf(stderr, "mcp23017DeviceCreate: no response from device at "
                        "0x%02X on /dev/i2c-%d — is the chip wired and powered?\n",
                        i2cAddress, i2cBus);
        pthread_mutex_unlock(&dev->i2cLock);
        pthread_mutex_destroy(&dev->i2cLock);
        free(dev);
        close(fd);
        return NULL;
    }

    /* Seed the output latch cache from the chip so the first LED write does
     * not inadvertently toggle pins that were already in a known state. */
    if (mcp23017ReadReg(dev, MCP23017_OLATA, &dev->outputLatchA) != MCP23017_DEVICE_SUCCESS ||
        mcp23017ReadReg(dev, MCP23017_OLATB, &dev->outputLatchB) != MCP23017_DEVICE_SUCCESS) {
        fprintf(stderr, "mcp23017DeviceCreate: failed to read output latches "
                        "from device at 0x%02X\n", i2cAddress);
        pthread_mutex_unlock(&dev->i2cLock);
        pthread_mutex_destroy(&dev->i2cLock);
        free(dev);
        close(fd);
        return NULL;
    }

    pthread_mutex_unlock(&dev->i2cLock);

    return dev;
}

void mcp23017DeviceDestroy(MCP23017Device_t *device)
{
    if (!device) return;
    close(device->fd);
    pthread_mutex_destroy(&device->i2cLock);
    free(device);
}

/* ── Register I/O (caller must hold i2cLock) ────────────────────────────── */

MCP23017DeviceStatus_t mcp23017WriteReg(MCP23017Device_t *device, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (write(device->fd, buf, 2) != 2)
        return MCP23017_DEVICE_ERROR_I2C_WRITE;
    return MCP23017_DEVICE_SUCCESS;
}

MCP23017DeviceStatus_t mcp23017ReadReg(MCP23017Device_t *device, uint8_t reg, uint8_t *val)
{
    if (write(device->fd, &reg, 1) != 1)
        return MCP23017_DEVICE_ERROR_I2C_WRITE;
    if (read(device->fd, val, 1) != 1)
        return MCP23017_DEVICE_ERROR_I2C_READ;
    return MCP23017_DEVICE_SUCCESS;
}

/* ── Pin I/O ────────────────────────────────────────────────────────────── */

MCP23017DeviceStatus_t mcp23017SetOutputPin(MCP23017Device_t *device,
                                             uint8_t port, uint8_t pin, bool high)
{
    if (!device || pin > 7) return MCP23017_DEVICE_ERROR_NULL;

    pthread_mutex_lock(&device->i2cLock);

    if (port == 0) {
        if (high) device->outputLatchA |=  (uint8_t)(1u << pin);
        else      device->outputLatchA &= ~(uint8_t)(1u << pin);
        mcp23017WriteReg(device, MCP23017_OLATA, device->outputLatchA);
    } else {
        if (high) device->outputLatchB |=  (uint8_t)(1u << pin);
        else      device->outputLatchB &= ~(uint8_t)(1u << pin);
        mcp23017WriteReg(device, MCP23017_OLATB, device->outputLatchB);
    }

    pthread_mutex_unlock(&device->i2cLock);
    return MCP23017_DEVICE_SUCCESS;
}

MCP23017DeviceStatus_t mcp23017ReadInputPin(MCP23017Device_t *device,
                                             uint8_t port, uint8_t pin, uint8_t *val)
{
    if (!device || !val || pin > 7) return MCP23017_DEVICE_ERROR_NULL;

    uint8_t gpioVal = 0;
    pthread_mutex_lock(&device->i2cLock);
    mcp23017ReadReg(device, port == 0 ? MCP23017_GPIOA : MCP23017_GPIOB, &gpioVal);
    pthread_mutex_unlock(&device->i2cLock);

    *val = (gpioVal >> pin) & 0x01u;
    return MCP23017_DEVICE_SUCCESS;
}

/* ── Dispatch table management ──────────────────────────────────────────── */

void mcp23017RegisterButtonDispatch(MCP23017Device_t *device, uint8_t port, uint8_t pin,
                                     void *buttonMem, void (*armFn)(void *))
{
    if (!device || pin > 7) return;

    MCP23017DispatchSlot_t *slots = (port == 0) ? device->buttonDispatchA
                                                 : device->buttonDispatchB;
    pthread_mutex_lock(&device->i2cLock);
    slots[pin].data  = buttonMem;
    slots[pin].armFn = armFn;
    pthread_mutex_unlock(&device->i2cLock);
}

void mcp23017UnregisterButtonDispatch(MCP23017Device_t *device, uint8_t port, uint8_t pin)
{
    if (!device || pin > 7) return;

    MCP23017DispatchSlot_t *slots = (port == 0) ? device->buttonDispatchA
                                                 : device->buttonDispatchB;
    pthread_mutex_lock(&device->i2cLock);
    slots[pin].armFn = NULL;
    slots[pin].data  = NULL;
    pthread_mutex_unlock(&device->i2cLock);
}

/* ── ISR registration ───────────────────────────────────────────────────── */

void mcp23017RegisterPortISR(MCP23017Device_t *device, uint8_t port, int intGpioPin)
{
    if (!device || intGpioPin < 0 || intGpioPin > 39) return;

    bool *registered = (port == 0) ? &device->intARegistered : &device->intBRegistered;
    if (*registered) return;

    intSlotLookup[intGpioPin].device = device;
    intSlotLookup[intGpioPin].port   = port;

    if (port == 0) device->intGpioPinA = intGpioPin;
    else           device->intGpioPinB = intGpioPin;

    wiringPiISR(intGpioPin, INT_EDGE_FALLING, mcp23017_int_isr_table[intGpioPin]);
    *registered = true;
}

/* ── ISR handler ────────────────────────────────────────────────────────── */

static void mcp23017ChipInterrupt(int gpioPin)
{
    if (gpioPin < 0 || gpioPin > 39) return;

    MCP23017Device_t *dev  = intSlotLookup[gpioPin].device;
    uint8_t           port = intSlotLookup[gpioPin].port;
    if (!dev) return;

    /* Hold i2cLock across both the I2C reads and the dispatch loop so that
     * a concurrent mcp23017UnregisterButtonDispatch cannot clear a slot
     * between the INTF check and the armFn call. */
    pthread_mutex_lock(&dev->i2cLock);

    uint8_t intf = 0, dummy = 0;
    mcp23017ReadReg(dev, port == 0 ? MCP23017_INTFA  : MCP23017_INTFB,  &intf);
    /* Reading INTCAP clears the interrupt so the next edge can be captured. */
    mcp23017ReadReg(dev, port == 0 ? MCP23017_INTCAPA : MCP23017_INTCAPB, &dummy);

    MCP23017DispatchSlot_t *dispatch = (port == 0) ? dev->buttonDispatchA
                                                    : dev->buttonDispatchB;
    for (int pin = 0; pin < 8; pin++) {
        if ((intf & (1u << pin)) && dispatch[pin].armFn)
            dispatch[pin].armFn(dispatch[pin].data);
    }

    pthread_mutex_unlock(&dev->i2cLock);
}
