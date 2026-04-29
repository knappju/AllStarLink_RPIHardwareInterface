#include "gpioButton.h"

//forward declarations of private functions
void buttonInterupt(int pin);

#define X(n) \
    void button_isr_##n(void) { \
        buttonInterupt(n); \
    }
BUTTON_ISR_LIST
#undef X

void (*button_isr_table[])(void) = {
#define X(n) button_isr_##n,
    BUTTON_ISR_LIST
#undef X
};

//declare private variables
gpioButtonMemory_t *buttonMemLookUp[40] = {NULL};

gpioButtonMemory_t * gpioButtonInit( int pin, int pull, int debounceTimeMs, int interruptEdge)
{
    //check the validity of input parameters...
    if (pin < 0 || pin > 40) {
        return NULL; //invalid pin number.
    }
    if (pull != PUD_OFF && pull != PUD_UP && pull != PUD_DOWN) {
        return NULL; //invalid pull up/down configuration.
    }
    if (debounceTimeMs < 0) {
        return NULL; //invalid debounce time.
    }
    if (interruptEdge != INT_EDGE_FALLING && interruptEdge != INT_EDGE_RISING && interruptEdge != INT_EDGE_BOTH) {
        return NULL; //invalid interrupt edge.
    }

    //allocate memory for the button
    gpioButtonMemory_t *buttonMemory = (gpioButtonMemory_t *) malloc(sizeof(gpioButtonMemory_t));
    if (buttonMemory == NULL) {
        return NULL; //memory allocation failed.
    }

    //set the memory in the lookup table
    buttonMemLookUp[pin - 1] = buttonMemory;

    //initialize the button memory with the provided parameters and default values.
    buttonMemory->pin = pin;
    buttonMemory->pull = pull;
    buttonMemory->debounceTimeMs = debounceTimeMs;
    buttonMemory->interruptEdge = interruptEdge;
    buttonMemory->cb = NULL;
    buttonMemory->cbEnabled = false;
    buttonMemory->lastInterruptTime = millis();

    //set the pin mode and pull up/down resistors for the button pin.
    pinMode(pin, INPUT);
    pullUpDnControl(pin, pull);
    
    //read the initial state of the button and store it in the button memory.
    buttonMemory->state = digitalRead(pin);

    //attach the interupt to the button pin with the provided interrupt edge and the buttonInterupt function as the callback.
    wiringPiISR(pin, interruptEdge, button_isr_table[pin]);

    return buttonMemory;
}
gpioButtonStatus_t gpioButtonDeinit(void* buttonMemory)
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;

    //disable the callback if it is enabled.
    if (btnMem->cbEnabled) {
        gpioButtonDisableCB(btnMem);
    }

    //detach the interupt from the button pin.
    wiringPiISR(btnMem->pin, INT_EDGE_SETUP, NULL);

    //free the allocated memory for the button.
    free(btnMem);
    result = GPIO_BUTTON_SUCCESS;

    return result;
}
gpioButtonStatus_t gpioButtonRead(void* buttonMemory, uint8_t* state)
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;

    //read the current state of the button and store it in the provided buffer.
    *state = btnMem->state;
    result = GPIO_BUTTON_SUCCESS;

    return result;
}
gpioButtonStatus_t gpioButtonGetTimeInState(void* buttonMemory, unsigned long* timeInState )
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;

    //get the time spent in the current state.
    unsigned long currentTime = millis();
    *timeInState = currentTime - btnMem->lastInterruptTime;
    result = GPIO_BUTTON_SUCCESS;

    return result;
}
gpioButtonStatus_t gpioButtonRegisterCB(void* buttonMemory, void (*cb)(uint8_t state))
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;
    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }
    if(cb == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }
    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;
    //check if a callback is already registered for this button.
    if (btnMem->cb != NULL) {
        return GPIO_BUTTON_ERROR_ALREADY_REGISTERED;
    }
    //register the provided callback for this button.
    btnMem->cb = cb;
    result = GPIO_BUTTON_SUCCESS;

    return result;
}

gpioButtonStatus_t gpioButtonUnregisterCB(void* buttonMemory)
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;

    //check if a callback is registered for this button.
    if (btnMem->cb == NULL) {
        return GPIO_BUTTON_ERROR_NOT_REGISTERED;
    }

    //unregister the callback for this button.
    btnMem->cb = NULL;
    result = GPIO_BUTTON_SUCCESS;

    return result;
}

gpioButtonStatus_t gpioButtonEnableCB(void* buttonMemory)
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }
    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;
    //check if a callback is registered for this button.
    if (btnMem->cb == NULL) {
        return GPIO_BUTTON_ERROR_NOT_REGISTERED;
    }
    //enable the callback for this button.
    btnMem->cbEnabled = true;
    result = GPIO_BUTTON_SUCCESS;

    return result;
}
gpioButtonStatus_t gpioButtonDisableCB(void* buttonMemory)
{
    gpioButtonStatus_t result = GPIO_BUTTON_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(buttonMemory == NULL) {
        return GPIO_BUTTON_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioButtonMemory_t *btnMem = (gpioButtonMemory_t *) buttonMemory;

    //check if a callback is registered for this button.
    if (btnMem->cb == NULL) {
        return GPIO_BUTTON_ERROR_NOT_REGISTERED;
    }

    //disable the callback for this button.
    btnMem->cbEnabled = false;
    result = GPIO_BUTTON_SUCCESS;

    return result;
}

//private functions
void buttonInterupt(int pin)
{
    if (buttonMemLookUp[pin - 1] == NULL) {
        return; //invalid pointer.
    }

    gpioButtonMemory_t *btnMem = buttonMemLookUp[pin - 1];

    //get the current time
    unsigned long currentInterruptTime = millis();

    if (currentInterruptTime - btnMem->lastInterruptTime < btnMem->debounceTimeMs) {
        return; //debouncing: ignore this interrupt because it occurred too soon after the last one.
    }

    btnMem->lastInterruptTime = currentInterruptTime;

    //read the current state of the button and store it in the button memory.
    btnMem->state = digitalRead(btnMem->pin);

    //if a callback is registered and enabled for this button, call it with the new state.
    if (btnMem->cb != NULL && btnMem->cbEnabled) {
        btnMem->cb(btnMem->state);
    }
}