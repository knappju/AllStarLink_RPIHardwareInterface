#ifndef GPIO_LED_H
#define GPIO_LED_H

#include <wiringPi.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>

typedef enum {
    GPIO_LED_SUCCESS = 0,
    GPIO_LED_UNDEFINED_ERROR = -1,
    GPIO_LED_ERROR_NULL_POINTER,
    GPIO_LED_ERROR_INVALID_MODE
} gpioLedStatus_t;

typedef enum {
    GPIO_LED_OFF = 0,
    GPIO_LED_ON = 1,
    GPIO_LED_ONESHOT,
    GPIO_LED_BLINK_ON,
    GPIO_LED_BLINK_OFF
} gpioLedMode_t;

typedef struct {
    int pin;
    pthread_mutex_t lock;
    //values protected by gpioLedLock mutex
    bool valid;
    gpioLedMode_t mode;
    timer_t timerId;
    struct sigevent signalEvent;
    struct itimerspec timerTrigger;
    struct timespec primaryTime;
    struct timespec secondaryTime;
    //end values protected by gpioLedLock mutex
} gpioLedMemory_t;

gpioLedMemory_t * gpioLedInit(int pin);
gpioLedStatus_t gpioLedDeinit(void* ledMemory);
gpioLedStatus_t gpioLedSetConstant(void* ledMemory, gpioLedMode_t mode);
gpioLedStatus_t gpioLedSetOneShot(void* ledMemory, unsigned long durationMs);
gpioLedStatus_t gpioLedSetBlink(void* ledMemory, unsigned long onDurationMs, unsigned long offDurationMs);

#endif /* GPIO_LED_H */