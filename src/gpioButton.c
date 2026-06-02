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

/* Maps pin number (1-based) to its gpioButtonMemory_t; populated by
 * gpioButtonInit() and consulted by the shared ISR handler. */
static gpioButtonMemory_t *buttonMemLookUp[40] = {NULL};

gpioButtonMemory_t *gpioButtonInit(int pin, int pull, int debounceTimeMs, int interruptEdge)
{
    if (pin < 0 || pin > 30) {
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

    buttonMemLookUp[pin - 1] = buttonMemory;

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

    /* Register the pin-specific ISR stub. The table is 0-indexed while pin
     * numbers are 1-indexed, hence the (pin - 1) offset. */
    wiringPiISR(pin, interruptEdge, button_isr_table[pin - 1]);

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

    /* Detach the ISR by setting edge to INT_EDGE_SETUP (no-op). */
    wiringPiISR(btnMem->pin, INT_EDGE_SETUP, NULL);

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

/* Shared ISR handler called by all pin-specific stubs.
 * Performs software debounce: ignores interrupts that arrive within
 * debounceTimeMs of the last accepted interrupt. */
static void buttonInterupt(int pin)
{
    if (buttonMemLookUp[pin - 1] == NULL) {
        return;
    }

    gpioButtonMemory_t *btnMem = buttonMemLookUp[pin - 1];
    unsigned long now = millis();

    if (now - btnMem->lastInterruptTime < (unsigned long)btnMem->debounceTimeMs) {
        printf("Interrupt on pin %d ignored due to debounce (time since last: %lu ms)\n", pin, now - btnMem->lastInterruptTime);
        return; /* too soon — bounce, ignore */
    }

    btnMem->lastInterruptTime = now;
    btnMem->state = digitalRead(btnMem->pin);

    if (btnMem->cb != NULL && btnMem->cbEnabled) {
        btnMem->cb(btnMem->state);
    }
}
