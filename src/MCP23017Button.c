/**
 * @file MCP23017Button.c
 * @brief MCP23017 button driver implementation.
 *
 * Debounce model mirrors gpioButton.c:
 *   1. Chip interrupt fires → device ISR calls mcp23017ButtonArmDebounce().
 *   2. The POSIX debounce timer is (re-)armed; each new edge resets it.
 *   3. Once the timer expires, mcp23017ButtonDebounceCb() reads the stable
 *      pin level via mcp23017ReadInputPin() and fires the user callback if
 *      the state actually changed.
 */

#include <wiringPi.h>
#include "MCP23017Button.h"

static void mcp23017ButtonDebounceCb(union sigval sv);

MCP23017ButtonMemory_t *mcp23017ButtonInit(MCP23017Device_t *device,
                                            uint8_t port, uint8_t pin,
                                            bool enablePullup,
                                            int debounceTimeMs,
                                            int intGpioPin)
{
    if (!device || pin > 7 || debounceTimeMs < 0) return NULL;

    MCP23017ButtonMemory_t *btnMem = calloc(1, sizeof(MCP23017ButtonMemory_t));
    if (!btnMem) return NULL;

    btnMem->device         = device;
    btnMem->port           = port;
    btnMem->pin            = pin;
    btnMem->debounceTimeMs = debounceTimeMs;
    btnMem->cb             = NULL;
    btnMem->cbEnabled      = false;
    btnMem->lastChangeTimeMs = millis();

    /* Configure chip registers for this pin (all read-modify-write, under lock). */
    uint8_t iodirReg   = (port == 0) ? MCP23017_IODIRA   : MCP23017_IODIRB;
    uint8_t gppuReg    = (port == 0) ? MCP23017_GPPUA    : MCP23017_GPPUB;
    uint8_t gpintenReg = (port == 0) ? MCP23017_GPINTENA : MCP23017_GPINTENB;
    uint8_t intconReg  = (port == 0) ? MCP23017_INTCONA  : MCP23017_INTCONB;

    pthread_mutex_lock(&device->i2cLock);

    uint8_t val = 0;

    /* Set pin as input (set the IODIR bit). */
    if (mcp23017ReadReg(device, iodirReg, &val) != MCP23017_DEVICE_SUCCESS) {
        pthread_mutex_unlock(&device->i2cLock);
        free(btnMem);
        fprintf(stderr, "mcp23017ButtonInit: failed to read IODIR%c "
                        "(addr=0x%02X port=%c pin=%u)\n",
                        port == 0 ? 'A' : 'B', device->i2cAddress,
                        port == 0 ? 'A' : 'B', pin);
        return NULL;
    }
    val |= (uint8_t)(1u << pin);
    mcp23017WriteReg(device, iodirReg, val);

    /* Optionally enable the internal pull-up. */
    mcp23017ReadReg(device, gppuReg, &val);
    if (enablePullup) val |=  (uint8_t)(1u << pin);
    else              val &= ~(uint8_t)(1u << pin);
    mcp23017WriteReg(device, gppuReg, val);

    /* Enable interrupt-on-change for this pin. */
    mcp23017ReadReg(device, gpintenReg, &val);
    val |= (uint8_t)(1u << pin);
    mcp23017WriteReg(device, gpintenReg, val);

    /* Use compare-to-previous-value mode (clear INTCON bit) so any edge fires. */
    mcp23017ReadReg(device, intconReg, &val);
    val &= ~(uint8_t)(1u << pin);
    mcp23017WriteReg(device, intconReg, val);

    /* Sample the initial pin state so the first debounce callback can compare. */
    uint8_t gpioReg = (port == 0) ? MCP23017_GPIOA : MCP23017_GPIOB;
    mcp23017ReadReg(device, gpioReg, &val);

    pthread_mutex_unlock(&device->i2cLock);

    btnMem->state = (val >> pin) & 0x01u;

    /* Create the POSIX debounce timer.  SIGEV_THREAD delivers the callback in
     * a new thread so mcp23017ReadInputPin (which locks i2cLock) is safe to
     * call there.  sival_ptr carries btnMem into the callback. */
    struct sigevent sev = {
        .sigev_notify          = SIGEV_THREAD,
        .sigev_notify_function = mcp23017ButtonDebounceCb,
        .sigev_value.sival_ptr = btnMem,
    };
    if (timer_create(CLOCK_MONOTONIC, &sev, &btnMem->debounceTimer) != 0) {
        free(btnMem);
        return NULL;
    }

    /* Register with the device's interrupt dispatch table. */
    mcp23017RegisterButtonDispatch(device, port, pin, btnMem, mcp23017ButtonArmDebounce);

    /* Register the port's interrupt GPIO ISR if not already done. */
    if (intGpioPin >= 0)
        mcp23017RegisterPortISR(device, port, intGpioPin);

    return btnMem;
}

MCP23017ButtonStatus_t mcp23017ButtonDeinit(void *buttonMemory)
{
    if (!buttonMemory) return MCP23017_BUTTON_ERROR_NULL;

    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;

    /* Cancel any pending debounce before clearing the dispatch entry so the
     * timer callback cannot fire after the struct is freed. */
    timer_delete(btnMem->debounceTimer);

    mcp23017UnregisterButtonDispatch(btnMem->device, btnMem->port, btnMem->pin);

    free(btnMem);
    return MCP23017_BUTTON_SUCCESS;
}

MCP23017ButtonStatus_t mcp23017ButtonRead(void *buttonMemory, uint8_t *state)
{
    if (!buttonMemory || !state) return MCP23017_BUTTON_ERROR_NULL;
    *state = ((MCP23017ButtonMemory_t *)buttonMemory)->state;
    return MCP23017_BUTTON_SUCCESS;
}

MCP23017ButtonStatus_t mcp23017ButtonGetTimeInState(void *buttonMemory,
                                                     unsigned long *timeInState)
{
    if (!buttonMemory || !timeInState) return MCP23017_BUTTON_ERROR_NULL;
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;
    *timeInState = millis() - btnMem->lastChangeTimeMs;
    return MCP23017_BUTTON_SUCCESS;
}

MCP23017ButtonStatus_t mcp23017ButtonRegisterCB(void *buttonMemory,
                                                  void (*cb)(uint8_t state))
{
    if (!buttonMemory || !cb) return MCP23017_BUTTON_ERROR_NULL;
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;
    if (btnMem->cb != NULL) return MCP23017_BUTTON_ERROR_ALREADY_REGISTERED;
    btnMem->cb = cb;
    return MCP23017_BUTTON_SUCCESS;
}

MCP23017ButtonStatus_t mcp23017ButtonUnregisterCB(void *buttonMemory)
{
    if (!buttonMemory) return MCP23017_BUTTON_ERROR_NULL;
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;
    if (!btnMem->cb) return MCP23017_BUTTON_ERROR_NOT_REGISTERED;
    btnMem->cb = NULL;
    return MCP23017_BUTTON_SUCCESS;
}

MCP23017ButtonStatus_t mcp23017ButtonEnableCB(void *buttonMemory)
{
    if (!buttonMemory) return MCP23017_BUTTON_ERROR_NULL;
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;
    if (!btnMem->cb) return MCP23017_BUTTON_ERROR_NOT_REGISTERED;
    btnMem->cbEnabled = true;
    return MCP23017_BUTTON_SUCCESS;
}

MCP23017ButtonStatus_t mcp23017ButtonDisableCB(void *buttonMemory)
{
    if (!buttonMemory) return MCP23017_BUTTON_ERROR_NULL;
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;
    if (!btnMem->cb) return MCP23017_BUTTON_ERROR_NOT_REGISTERED;
    btnMem->cbEnabled = false;
    return MCP23017_BUTTON_SUCCESS;
}

/* Called by the device ISR dispatch via the stored function pointer.
 * Re-arms the debounce timer on every edge; the timer only fires once the
 * pin has been quiet for the full debounce window. */
void mcp23017ButtonArmDebounce(void *buttonMemory)
{
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)buttonMemory;

    struct itimerspec ts = {
        .it_value = {
            .tv_sec  = btnMem->debounceTimeMs / 1000,
            .tv_nsec = (long)(btnMem->debounceTimeMs % 1000) * 1000000L,
        },
        .it_interval = { 0, 0 },
    };
    timer_settime(btnMem->debounceTimer, 0, &ts, NULL);
}

/* Debounce timer callback: runs in a SIGEV_THREAD-created OS thread.
 * Reads the stable pin level and fires the user callback if the state changed. */
static void mcp23017ButtonDebounceCb(union sigval sv)
{
    MCP23017ButtonMemory_t *btnMem = (MCP23017ButtonMemory_t *)sv.sival_ptr;

    uint8_t stableState = 0;
    mcp23017ReadInputPin(btnMem->device, btnMem->port, btnMem->pin, &stableState);

    if (stableState != btnMem->state) {
        btnMem->state            = stableState;
        btnMem->lastChangeTimeMs = millis();

        if (btnMem->cb != NULL && btnMem->cbEnabled)
            btnMem->cb(btnMem->state);
    }
}
