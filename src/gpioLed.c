#include "gpioLed.h"

//forward declarations of private functions
void gpioLedWritePin(union sigval sv);

gpioLedMemory_t * gpioLedInit(int pin)
{
    //check the validity of input parameters...
    if (pin < 0 || pin > 40) {
        return NULL; //invalid pin number.
    }

    //allocate memory for the led
    gpioLedMemory_t *ledMemory = (gpioLedMemory_t *) malloc(sizeof(gpioLedMemory_t));
    if (ledMemory == NULL) {
        return NULL; //memory allocation failed.
    }

    if (pthread_mutex_init(&ledMemory->lock, NULL) != 0) {
        free(ledMemory);
        return NULL; //mutex initialization failed.
    }

    //set up the signalevent struct for the timer.
    memset(&ledMemory->signalEvent, 0, sizeof(struct sigevent));
    ledMemory->signalEvent.sigev_notify = SIGEV_THREAD;
    ledMemory->signalEvent.sigev_notify_function = gpioLedWritePin;
    ledMemory->signalEvent.sigev_value.sival_ptr = ledMemory;
    ledMemory->signalEvent.sigev_notify_attributes = NULL;

    ledMemory->timerId = 0;
    memset(&ledMemory->timerTrigger, 0, sizeof(struct itimerspec));
    memset(&ledMemory->primaryTime, 0, sizeof(struct timespec));
    memset(&ledMemory->secondaryTime, 0, sizeof(struct timespec));
    
    //initialize the led memory with the provided parameters and default values.
    ledMemory->pin = pin;
    ledMemory->valid = true;
    ledMemory->mode = GPIO_LED_OFF;
    

    //set the pin mode for the led pin.
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    
    return ledMemory;
}

gpioLedStatus_t gpioLedDeinit(void* ledMemory)
{
    gpioLedStatus_t result = GPIO_LED_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioLedMemory_t *ledMem = (gpioLedMemory_t *) ledMemory;

    //mark the led memory as invalid so that the timer callback stops doing work.
    pthread_mutex_lock(&ledMem->lock);
    ledMem->valid = false;
    pthread_mutex_unlock(&ledMem->lock);

    //delete the timer if it exists.
    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
    }

    //free the allocated memory for the led.
    free(ledMem);
    result = GPIO_LED_SUCCESS;

    return result;
}

gpioLedStatus_t gpioLedSetConstant(void* ledMemory, gpioLedMode_t mode)
{
    gpioLedStatus_t result = GPIO_LED_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioLedMemory_t *ledMem = (gpioLedMemory_t *) ledMemory;

    //delete the timer if it exists.
    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    //set the led pin to the correct state based on the provided mode.
    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = mode;
    switch(mode) {
        case GPIO_LED_OFF:
            digitalWrite(ledMem->pin, LOW);
            break;
        case GPIO_LED_ON:
            digitalWrite(ledMem->pin, HIGH);
            break;
        default:
            result = GPIO_LED_ERROR_INVALID_MODE;
            break;
    }
    pthread_mutex_unlock(&ledMem->lock);

    if (result != GPIO_LED_UNDEFINED_ERROR) {
        return result; //invalid mode provided.
    }

    return GPIO_LED_SUCCESS;
}

gpioLedStatus_t gpioLedSetOneShot(void* ledMemory, unsigned long durationMs)
{
    gpioLedStatus_t result = GPIO_LED_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioLedMemory_t *ledMem = (gpioLedMemory_t *) ledMemory;

    //delete the timer if it exists.
    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    //set up the timer trigger for a one shot based on the provided duration.
    ledMem->timerTrigger.it_value.tv_sec = durationMs / 1000;
    ledMem->timerTrigger.it_value.tv_nsec = (durationMs % 1000) * 1000000;

    //create the timer.
    if (timer_create(CLOCK_REALTIME, &ledMem->signalEvent, &ledMem->timerId) != 0) {
        return GPIO_LED_UNDEFINED_ERROR; //timer creation failed.
    }

    //start the timer.
    if (timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL) != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
        return GPIO_LED_UNDEFINED_ERROR; //timer start failed.
    }

    //set the mode to one shot so that the timer callback knows how to handle it.
    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = GPIO_LED_ONESHOT;
    digitalWrite(ledMem->pin, HIGH); //turn on the led for the one shot duration.
    pthread_mutex_unlock(&ledMem->lock);

    return GPIO_LED_SUCCESS;
}

gpioLedStatus_t gpioLedSetBlink(void* ledMemory, unsigned long onDurationMs, unsigned long offDurationMs)
{
    gpioLedStatus_t result = GPIO_LED_UNDEFINED_ERROR;

    //check if the provided pointer is valid.
    if(ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    // cast to the correct type
    gpioLedMemory_t *ledMem = (gpioLedMemory_t *) ledMemory;

    //delete the timer if it exists.
    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    //set up the timer trigger for blinking based on the provided durations.
    ledMem->primaryTime.tv_sec = onDurationMs / 1000;
    ledMem->primaryTime.tv_nsec = (onDurationMs % 1000) * 1000000;
    ledMem->secondaryTime.tv_sec = offDurationMs / 1000;
    ledMem->secondaryTime.tv_nsec = (offDurationMs % 1000) * 1000000;

    ledMem->timerTrigger.it_value = ledMem->primaryTime; //start with the on duration.
    ledMem->timerTrigger.it_interval.tv_sec = 0; //we will manually toggle between on and off durations in the callback.
    ledMem->timerTrigger.it_interval.tv_nsec = 0;

    //create the timer.
    if (timer_create(CLOCK_REALTIME, &ledMem->signalEvent, &ledMem->timerId) != 0) {
        return GPIO_LED_UNDEFINED_ERROR; //timer creation failed.
    }

    //start the timer.
    if (timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL) != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
        return GPIO_LED_UNDEFINED_ERROR; //timer start failed.
    }

    //set the mode to blink so that the timer callback knows how to handle it.
    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = GPIO_LED_BLINK_ON;
    digitalWrite(ledMem->pin, HIGH); //turn on the led to start the blinking cycle.
    pthread_mutex_unlock(&ledMem->lock);

    return GPIO_LED_SUCCESS;
}

void gpioLedWritePin(union sigval sv){
    gpioLedMemory_t *ledMem = (gpioLedMemory_t *) sv.sival_ptr;

    pthread_mutex_lock(&ledMem->lock);
    if (!ledMem->valid) {
        pthread_mutex_unlock(&ledMem->lock);
        return; //led memory is no longer valid, exit the callback.
    }

    switch(ledMem->mode) {
        case GPIO_LED_ONESHOT:
            digitalWrite(ledMem->pin, LOW); //turn off the led after the one shot duration.
            ledMem->mode = GPIO_LED_OFF; //update mode to off after one shot completes.
            break;
        case GPIO_LED_BLINK_ON:
            digitalWrite(ledMem->pin, LOW); //turn off the led for the off duration.
            ledMem->mode = GPIO_LED_BLINK_OFF; //update mode to indicate we are now in the off part of the blink cycle.
            ledMem->timerTrigger.it_value = ledMem->secondaryTime; //set timer for off duration.
            timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL); //restart timer with new duration.
            break;
        case GPIO_LED_BLINK_OFF:
            digitalWrite(ledMem->pin, HIGH); //turn on the led for the on duration.
            ledMem->mode = GPIO_LED_BLINK_ON; //update mode to indicate we are now in the on part of the blink cycle.
            ledMem->timerTrigger.it_value = ledMem->primaryTime; //set timer for on duration.
            timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL); //restart timer with new duration.
            break;
        default:
            break; //should not reach here for constant modes since they do not use the timer.
    }
    pthread_mutex_unlock(&ledMem->lock);
}