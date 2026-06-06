/**
 * @file MCP23017Led.c
 * @brief MCP23017 LED driver implementation.
 *
 * Structure mirrors gpioLed.c. The key difference is that all pin writes go
 * through mcp23017SetOutputPin() which acquires device->i2cLock internally.
 * The LED's own lock guards mode state and the valid flag (same as gpioLed).
 *
 * Lock ordering: LED lock → device i2cLock. Never acquire in the reverse order.
 */

#include "MCP23017Led.h"

static void mcp23017LedTimerCb(union sigval sv);

MCP23017LedMemory_t *mcp23017LedInit(MCP23017Device_t *device, uint8_t port, uint8_t pin)
{
    if (!device || pin > 7) return NULL;

    MCP23017LedMemory_t *ledMem = calloc(1, sizeof(MCP23017LedMemory_t));
    if (!ledMem) return NULL;

    if (pthread_mutex_init(&ledMem->lock, NULL) != 0) {
        free(ledMem);
        return NULL;
    }

    ledMem->device = device;
    ledMem->port   = port;
    ledMem->pin    = pin;
    ledMem->valid  = true;
    ledMem->mode   = MCP23017_LED_OFF;

    /* Configure pin as output via IODIRA/B (clear the bit). */
    uint8_t iodirReg = (port == 0) ? MCP23017_IODIRA : MCP23017_IODIRB;
    uint8_t iodir    = 0;
    pthread_mutex_lock(&device->i2cLock);
    if (mcp23017ReadReg(device, iodirReg, &iodir) != MCP23017_DEVICE_SUCCESS) {
        pthread_mutex_unlock(&device->i2cLock);
        pthread_mutex_destroy(&ledMem->lock);
        free(ledMem);
        fprintf(stderr, "mcp23017LedInit: failed to read IODIR%c (addr=0x%02X port=%c pin=%u)\n",
                port == 0 ? 'A' : 'B', device->i2cAddress, port == 0 ? 'A' : 'B', pin);
        return NULL;
    }
    iodir &= ~(uint8_t)(1u << pin);
    if (mcp23017WriteReg(device, iodirReg, iodir) != MCP23017_DEVICE_SUCCESS) {
        pthread_mutex_unlock(&device->i2cLock);
        pthread_mutex_destroy(&ledMem->lock);
        free(ledMem);
        fprintf(stderr, "mcp23017LedInit: failed to write IODIR%c (addr=0x%02X port=%c pin=%u)\n",
                port == 0 ? 'A' : 'B', device->i2cAddress, port == 0 ? 'A' : 'B', pin);
        return NULL;
    }
    pthread_mutex_unlock(&device->i2cLock);

    /* Drive the pin low via the output latch. */
    mcp23017SetOutputPin(device, port, pin, false);

    /* Configure the POSIX timer to call mcp23017LedTimerCb in a new thread,
     * passing ledMem as the argument. Timer is not armed until setOneShot or
     * setBlink is called. */
    memset(&ledMem->signalEvent, 0, sizeof(struct sigevent));
    ledMem->signalEvent.sigev_notify              = SIGEV_THREAD;
    ledMem->signalEvent.sigev_notify_function     = mcp23017LedTimerCb;
    ledMem->signalEvent.sigev_value.sival_ptr     = ledMem;
    ledMem->signalEvent.sigev_notify_attributes   = NULL;

    return ledMem;
}

MCP23017LedStatus_t mcp23017LedDeinit(void *ledMemory)
{
    if (!ledMemory) return MCP23017_LED_ERROR_NULL;

    MCP23017LedMemory_t *ledMem = (MCP23017LedMemory_t *)ledMemory;

    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->valid = false;

    /* Drive pin low, then restore the pin to input so it is not left as a
     * floating driven output after the driver is torn down. */
    mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, false);

    uint8_t iodirReg = (ledMem->port == 0) ? MCP23017_IODIRA : MCP23017_IODIRB;
    uint8_t iodir    = 0;
    pthread_mutex_lock(&ledMem->device->i2cLock);
    mcp23017ReadReg(ledMem->device, iodirReg, &iodir);
    iodir |= (uint8_t)(1u << ledMem->pin);
    mcp23017WriteReg(ledMem->device, iodirReg, iodir);
    pthread_mutex_unlock(&ledMem->device->i2cLock);

    pthread_mutex_unlock(&ledMem->lock);

    free(ledMem);
    return MCP23017_LED_SUCCESS;
}

MCP23017LedStatus_t mcp23017LedSetConstant(void *ledMemory, MCP23017LedMode_t mode)
{
    if (!ledMemory) return MCP23017_LED_ERROR_NULL;

    MCP23017LedMemory_t *ledMem = (MCP23017LedMemory_t *)ledMemory;

    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = mode;
    switch (mode) {
        case MCP23017_LED_OFF:
            mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, false);
            break;
        case MCP23017_LED_ON:
            mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, true);
            break;
        default:
            pthread_mutex_unlock(&ledMem->lock);
            return MCP23017_LED_ERROR_INVALID_MODE;
    }
    pthread_mutex_unlock(&ledMem->lock);

    return MCP23017_LED_SUCCESS;
}

MCP23017LedStatus_t mcp23017LedSetOneShot(void *ledMemory, unsigned long durationMs)
{
    if (!ledMemory) return MCP23017_LED_ERROR_NULL;

    MCP23017LedMemory_t *ledMem = (MCP23017LedMemory_t *)ledMemory;

    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    ledMem->timerTrigger.it_value.tv_sec  = durationMs / 1000;
    ledMem->timerTrigger.it_value.tv_nsec = (long)(durationMs % 1000) * 1000000L;
    ledMem->timerTrigger.it_interval.tv_sec  = 0;
    ledMem->timerTrigger.it_interval.tv_nsec = 0;

    if (timer_create(CLOCK_REALTIME, &ledMem->signalEvent, &ledMem->timerId) != 0)
        return MCP23017_LED_UNDEFINED_ERROR;

    if (timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL) != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
        return MCP23017_LED_UNDEFINED_ERROR;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = MCP23017_LED_ONESHOT;
    mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, true);
    pthread_mutex_unlock(&ledMem->lock);

    return MCP23017_LED_SUCCESS;
}

MCP23017LedStatus_t mcp23017LedSetBlink(void *ledMemory,
                                          unsigned long onDurationMs,
                                          unsigned long offDurationMs)
{
    if (!ledMemory) return MCP23017_LED_ERROR_NULL;

    MCP23017LedMemory_t *ledMem = (MCP23017LedMemory_t *)ledMemory;

    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    ledMem->primaryTime.tv_sec    = onDurationMs / 1000;
    ledMem->primaryTime.tv_nsec   = (long)(onDurationMs  % 1000) * 1000000L;
    ledMem->secondaryTime.tv_sec  = offDurationMs / 1000;
    ledMem->secondaryTime.tv_nsec = (long)(offDurationMs % 1000) * 1000000L;

    /* Start the on-duration; the callback toggles between primary and secondary
     * rather than using it_interval so asymmetric on/off durations are supported. */
    ledMem->timerTrigger.it_value             = ledMem->primaryTime;
    ledMem->timerTrigger.it_interval.tv_sec   = 0;
    ledMem->timerTrigger.it_interval.tv_nsec  = 0;

    if (timer_create(CLOCK_REALTIME, &ledMem->signalEvent, &ledMem->timerId) != 0)
        return MCP23017_LED_UNDEFINED_ERROR;

    if (timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL) != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
        return MCP23017_LED_UNDEFINED_ERROR;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = MCP23017_LED_BLINK_ON;
    mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, true);
    pthread_mutex_unlock(&ledMem->lock);

    return MCP23017_LED_SUCCESS;
}

/* Timer callback: runs in an OS-created SIGEV_THREAD thread.
 * Drives the pin and reschedules the timer for the next blink phase. */
static void mcp23017LedTimerCb(union sigval sv)
{
    MCP23017LedMemory_t *ledMem = (MCP23017LedMemory_t *)sv.sival_ptr;

    pthread_mutex_lock(&ledMem->lock);

    if (!ledMem->valid) {
        pthread_mutex_unlock(&ledMem->lock);
        return;
    }

    switch (ledMem->mode) {
        case MCP23017_LED_ONESHOT:
            mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, false);
            ledMem->mode = MCP23017_LED_OFF;
            break;

        case MCP23017_LED_BLINK_ON:
            mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, false);
            ledMem->mode = MCP23017_LED_BLINK_OFF;
            ledMem->timerTrigger.it_value = ledMem->secondaryTime;
            timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL);
            break;

        case MCP23017_LED_BLINK_OFF:
            mcp23017SetOutputPin(ledMem->device, ledMem->port, ledMem->pin, true);
            ledMem->mode = MCP23017_LED_BLINK_ON;
            ledMem->timerTrigger.it_value = ledMem->primaryTime;
            timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL);
            break;

        default:
            break;
    }

    pthread_mutex_unlock(&ledMem->lock);
}
