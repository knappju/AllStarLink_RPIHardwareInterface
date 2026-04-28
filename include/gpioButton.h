#ifndef GPIO_BUTTON_H
#define GPIO_BUTTON_H

#include <wiringPi.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef enum {
    GPIO_BUTTON_SUCCESS = 0,
    GPIO_BUTTON_UNDEFINED_ERROR = -1,
    GPIO_BUTTON_ERROR_NULL_POINTER,
    GPIO_BUTTON_ERROR_INVALID_STATE,
    GPIO_BUTTON_ERROR_ALREADY_REGISTERED,
    GPIO_BUTTON_ERROR_NOT_REGISTERED,
} gpioButtonStatus_t;

typedef struct {
    int pin; 
    int pull; 
    int debounceTimeMs;
    int state;
    unsigned long lastInterruptTime;
    int interruptEdge;
    void (*cb)(uint8_t state);
    bool cbEnabled;
} gpioButtonMemory_t;

gpioButtonMemory_t * gpioButtonInit(int pin, int pull, int debounceTimeMs, int interruptEdge);
gpioButtonStatus_t gpioButtonDeinit(void* buttonMemory);
gpioButtonStatus_t gpioButtonRead(void* buttonMemory, uint8_t* state);
gpioButtonStatus_t gpioButtonGetTimeInState(void* buttonMemory, unsigned long* timeInState);
gpioButtonStatus_t gpioButtonRegisterCB(void* buttonMemory, void (*cb)(uint8_t state));
gpioButtonStatus_t gpioButtonUnregisterCB(void* buttonMemory);
gpioButtonStatus_t gpioButtonEnableCB(void* buttonMemory);
gpioButtonStatus_t gpioButtonDisableCB(void* buttonMemory);


#endif /* GPIO_BUTTON_H */