#ifndef GPIO_BUTTON_H
#define GPIO_BUTTON_H

#include <wiringPi.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

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

#define BUTTON_ISR_LIST \
    X(1) \
    X(2) \
    X(3) \
    X(4) \
    X(5) \
    X(6) \
    X(7) \
    X(8) \
    X(9) \
    X(10) \
    X(11) \
    X(12) \
    X(13) \
    X(14) \
    X(15) \
    X(16) \
    X(17) \
    X(18) \
    X(19) \
    X(20) \
    X(21) \
    X(22) \
    X(23) \
    X(24) \
    X(25) \
    X(26) \
    X(27) \
    X(28) \
    X(29) \
    X(30) \
    X(31) \
    X(32) \
    X(33) \
    X(34) \
    X(35) \
    X(36) \
    X(37) \
    X(38) \
    X(39) \
    X(40)


#endif /* GPIO_BUTTON_H */