/**
 * @file gpioButton.c
 * @brief GPIO button driver implementation.
 *
 * wiringPiISR requires each registered pin to have a unique zero-argument
 * function. The X-macro pattern generates one stub (button_isr_N) per pin,
 * and button_isr_table[] maps from (pin - 1) to its stub so gpioButtonInit()
 * can register the correct ISR without a long switch statement.
 */

#include "gpioButton.h"

static void buttonInterupt(int pin);
static void debounceTimerCb(union sigval sv);

/* Generate one ISR stub per pin (button_isr_1 .. button_isr_40).
 * Each stub calls the shared handler with its pin number. */
#define X(n) \
    static void button_isr_##n(void) { buttonInterupt(n); }
BUTTON_ISR_LIST
#undef X

/* Lookup table: button_isr_table[pin - 1] → ISR for that pin. */
static void (*button_isr_table[])(void) = {
#define X(n) button_isr_##n,
    BUTTON_ISR_LIST
#undef X
};

/* Maps wiringPi pin number (0-indexed) to its gpioButtonMemory_t; populated by
 * gpioButtonInit() and consulted by the shared ISR handler. */
static gpioButtonMemory_t *buttonMemLookUp[40] = {NULL};

gpioButtonMemory_t *gpioButtonInit(int pin, int pull, int debounceTimeMs, int interruptEdge)
{
    if (pin < 0 || pin > 39) {
        return NULL; /* invalid pin number */
    }
    if (pull != PUD_OFF && pull != PUD_UP && pull != PUD_DOWN) {
        return NULL; /* invalid pull resistor configuration */
    }
    if (debounceTimeMs < 0) {
        return NULL; /* debounce time must be non-negative */
    }
    if (interruptEdge != INT_EDGE_FALLING &&
        interruptEdge != INT_EDGE_RISING  &&
        interruptEdge != INT_EDGE_BOTH)
    {
        return NULL; /* invalid interrupt edge */
    }
 
    gpioButtonMemory_t *buttonMemory = malloc(sizeof(gpioButtonMemory_t));
    if (buttonMemory == NULL) {
        return NULL;
    }
 
    buttonMemLookUp[pin] = buttonMemory;
 
    buttonMemory->pin             = pin;
    buttonMemory->pull            = pull;
    buttonMemory->debounceTimeMs  = debounceTimeMs;
    buttonMemory->interruptEdge   = interruptEdge;
    buttonMemory->cb              = NULL;
    buttonMemory->cbEnabled       = false;
    buttonMemory->lastInterruptTime = millis();
 
    pinMode(pin, INPUT);
    pullUpDnControl(pin, pull);
    buttonMemory->state = digitalRead(pin);
 
    /* Create a one-shot per-button POSIX timer.
     * SIGEV_THREAD spins up a new thread for the callback, so the handler
     * is not constrained by ISR rules and can call digitalRead / the user cb
     * directly. sival_ptr carries the btnMem pointer into the callback. */
    struct sigevent sev = {
        .sigev_notify            = SIGEV_THREAD,
        .sigev_notify_function   = debounceTimerCb,
        .sigev_value.sival_ptr   = buttonMemory,
    };
    if (timer_create(CLOCK_MONOTONIC, &sev, &buttonMemory->debounceTimer) != 0) {
        free(buttonMemory);
        buttonMemLookUp[pin - 1] = NULL;
        return NULL;
    }
 
    wiringPiISR(pin, interruptEdge, button_isr_table[pin]);
 
    return buttonMemory;
}

gpioButtonStatus_t gpioButtonDeinit(void *buttonMemory)
{
    if (buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;

    if (btnMem->cbEnabled) {
        gpioButtonDisableCB(btnMem);
    }

    /* Cancel any pending debounce timer and release its resources before
     * freeing the struct it points into. */
    timer_delete(btnMem->debounceTimer);
 
    /* Detach the ISR — wiringPi provides no unregister call, but resetting
     * the pin mode to INPUT stops edges from being detected. */
    pinMode(btnMem->pin, INPUT);
 
    buttonMemLookUp[btnMem->pin] = NULL;

    free(btnMem);
    return GPIO_BUTTON_SUCCESS;
}

gpioButtonStatus_t gpioButtonRead(void *buttonMemory, uint8_t *state)
{
    if (buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;
    *state = btnMem->state;
    return GPIO_BUTTON_SUCCESS;
}

gpioButtonStatus_t gpioButtonGetTimeInState(void *buttonMemory, unsigned long *timeInState)
{
    if (buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;
    *timeInState = millis() - btnMem->lastInterruptTime;
    return GPIO_BUTTON_SUCCESS;
}

gpioButtonStatus_t gpioButtonRegisterCB(void *buttonMemory, void (*cb)(uint8_t state))
{
    if (buttonMemory == NULL || cb == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;
    if (btnMem->cb != NULL) {
        return GPIO_BUTTON_ERROR_ALREADY_REGISTERED;
    }

    btnMem->cb = cb;
    return GPIO_BUTTON_SUCCESS;
}

gpioButtonStatus_t gpioButtonUnregisterCB(void *buttonMemory)
{
    if (buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;
    if (btnMem->cb == NULL) {
        return GPIO_BUTTON_ERROR_NOT_REGISTERED;
    }

    btnMem->cb = NULL;
    return GPIO_BUTTON_SUCCESS;
}

gpioButtonStatus_t gpioButtonEnableCB(void *buttonMemory)
{
    if (buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;
    if (btnMem->cb == NULL) {
        return GPIO_BUTTON_ERROR_NOT_REGISTERED;
    }

    btnMem->cbEnabled = true;
    return GPIO_BUTTON_SUCCESS;
}

gpioButtonStatus_t gpioButtonDisableCB(void *buttonMemory)
{
    if (buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)buttonMemory;
    if (btnMem->cb == NULL) {
        return GPIO_BUTTON_ERROR_NOT_REGISTERED;
    }

    btnMem->cbEnabled = false;
    return GPIO_BUTTON_SUCCESS;
}


/* Called by the POSIX timer once debounceTimeMs of silence has elapsed.
 * Runs in a fresh thread (SIGEV_THREAD) so digitalRead and the user callback
 * are safe to call here. Only invokes the callback when the pin state has
 * actually changed since the last confirmed edge. */
static void debounceTimerCb(union sigval sv)
{
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *)sv.sival_ptr;
 
    uint8_t stableState = digitalRead(btnMem->pin);
 
    if (stableState != btnMem->state) {
        btnMem->state            = stableState;
        btnMem->lastInterruptTime = millis();
 
        if (btnMem->cb != NULL && btnMem->cbEnabled) {
            btnMem->cb(btnMem->state);
        }
    }
}

/* Shared ISR handler called by all pin-specific stubs.
 * Re-arms the debounce timer on every edge, pushing its expiry out by
 * debounceTimeMs. The timer only fires once the line has been quiet for
 * the full window. */
static void buttonInterupt(int pin)
{
    if (buttonMemLookUp[pin] == NULL) return;

    gpioButtonMemory_t *btnMem = buttonMemLookUp[pin];
 
    struct itimerspec ts = {
        .it_value = {
            .tv_sec  = btnMem->debounceTimeMs / 1000,
            .tv_nsec = (btnMem->debounceTimeMs % 1000) * 1000000L,
        },
        .it_interval = { 0, 0 },  /* one-shot */
    };
 
    timer_settime(btnMem->debounceTimer, 0, &ts, NULL);
}
