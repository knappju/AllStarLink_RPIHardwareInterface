/**
 * @file gpioLed.c
 * @brief GPIO LED driver implementation using POSIX interval timers.
 *
 * One-shot and blink modes use a SIGEV_THREAD timer so no separate thread is
 * required. The timer callback (gpioLedWritePin) runs in an OS-created thread
 * and holds the per-LED mutex while it modifies mode state and drives the pin.
 * The `valid` flag is cleared before free() so any in-flight timer callback
 * returns immediately rather than accessing freed memory.
 */

#include "gpioLed.h"

static void gpioLedWritePin(union sigval sv);

gpioLedMemory_t *gpioLedInit(int pin)
{
    if (pin < 0 || pin > 30) {
        return NULL; /* invalid pin number */
    }

    gpioLedMemory_t *ledMemory = malloc(sizeof(gpioLedMemory_t));
    if (ledMemory == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&ledMemory->lock, NULL) != 0) {
        free(ledMemory);
        return NULL;
    }

    /* Configure the sigevent so the timer fires gpioLedWritePin in a new
     * thread, passing ledMemory as the argument via sival_ptr. */
    memset(&ledMemory->signalEvent, 0, sizeof(struct sigevent));
    ledMemory->signalEvent.sigev_notify                = SIGEV_THREAD;
    ledMemory->signalEvent.sigev_notify_function       = gpioLedWritePin;
    ledMemory->signalEvent.sigev_value.sival_ptr       = ledMemory;
    ledMemory->signalEvent.sigev_notify_attributes     = NULL;

    ledMemory->timerId = 0;
    memset(&ledMemory->timerTrigger,  0, sizeof(struct itimerspec));
    memset(&ledMemory->primaryTime,   0, sizeof(struct timespec));
    memset(&ledMemory->secondaryTime, 0, sizeof(struct timespec));

    ledMemory->pin   = pin;
    ledMemory->valid = true;
    ledMemory->mode  = GPIO_LED_OFF;

    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);

    return ledMemory;
}

gpioLedStatus_t gpioLedDeinit(void *ledMemory)
{
    if (ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    gpioLedMemory_t *ledMem = (gpioLedMemory_t *)ledMemory;

    //delete any timers if they exist
    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
    }

    /* Signal the timer callback to stop before destroying the memory it uses. */
    pthread_mutex_lock(&ledMem->lock);
    ledMem->valid = false;
    
    //reset the pin to an input.
    digitalWrite(ledMem->pin, LOW);
    pinMode(ledMem->pin, INPUT);

    pthread_mutex_unlock(&ledMem->lock);

    free(ledMem);
    return GPIO_LED_SUCCESS;
}

gpioLedStatus_t gpioLedSetConstant(void *ledMemory, gpioLedMode_t mode)
{
    if (ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    gpioLedMemory_t *ledMem = (gpioLedMemory_t *)ledMemory;

    /* Cancel any active timer before changing the pin state. */
    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = mode;
    switch (mode) {
        case GPIO_LED_OFF:
            digitalWrite(ledMem->pin, LOW);
            break;
        case GPIO_LED_ON:
            digitalWrite(ledMem->pin, HIGH);
            break;
        default:
            pthread_mutex_unlock(&ledMem->lock);
            return GPIO_LED_ERROR_INVALID_MODE;
    }
    pthread_mutex_unlock(&ledMem->lock);

    return GPIO_LED_SUCCESS;
}

gpioLedStatus_t gpioLedSetOneShot(void *ledMemory, unsigned long durationMs)
{
    if (ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    gpioLedMemory_t *ledMem = (gpioLedMemory_t *)ledMemory;

    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    /* it_interval is left zero so the timer fires exactly once. */
    ledMem->timerTrigger.it_value.tv_sec  = durationMs / 1000;
    ledMem->timerTrigger.it_value.tv_nsec = (durationMs % 1000) * 1000000;

    if (timer_create(CLOCK_REALTIME, &ledMem->signalEvent, &ledMem->timerId) != 0) {
        return GPIO_LED_UNDEFINED_ERROR;
    }

    if (timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL) != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
        return GPIO_LED_UNDEFINED_ERROR;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = GPIO_LED_ONESHOT;
    digitalWrite(ledMem->pin, HIGH);
    pthread_mutex_unlock(&ledMem->lock);

    return GPIO_LED_SUCCESS;
}

gpioLedStatus_t gpioLedSetBlink(void *ledMemory, unsigned long onDurationMs, unsigned long offDurationMs)
{
    if (ledMemory == NULL) {
        return GPIO_LED_ERROR_NULL_POINTER;
    }

    gpioLedMemory_t *ledMem = (gpioLedMemory_t *)ledMemory;

    if (ledMem->timerId != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
    }

    ledMem->primaryTime.tv_sec    = onDurationMs / 1000;
    ledMem->primaryTime.tv_nsec   = (onDurationMs  % 1000) * 1000000;
    ledMem->secondaryTime.tv_sec  = offDurationMs / 1000;
    ledMem->secondaryTime.tv_nsec = (offDurationMs % 1000) * 1000000;

    /* Start with the on-duration; the callback manually toggles between
     * primaryTime and secondaryTime rather than using it_interval, so that
     * asymmetric on/off durations are supported. */
    ledMem->timerTrigger.it_value             = ledMem->primaryTime;
    ledMem->timerTrigger.it_interval.tv_sec   = 0;
    ledMem->timerTrigger.it_interval.tv_nsec  = 0;

    if (timer_create(CLOCK_REALTIME, &ledMem->signalEvent, &ledMem->timerId) != 0) {
        return GPIO_LED_UNDEFINED_ERROR;
    }

    if (timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL) != 0) {
        timer_delete(ledMem->timerId);
        ledMem->timerId = 0;
        return GPIO_LED_UNDEFINED_ERROR;
    }

    pthread_mutex_lock(&ledMem->lock);
    ledMem->mode = GPIO_LED_BLINK_ON;
    digitalWrite(ledMem->pin, HIGH);
    pthread_mutex_unlock(&ledMem->lock);

    return GPIO_LED_SUCCESS;
}

/* Timer callback: called from a SIGEV_THREAD-created OS thread.
 * Drives the pin and reschedules the timer for the next blink phase. */
static void gpioLedWritePin(union sigval sv)
{
    gpioLedMemory_t *ledMem = (gpioLedMemory_t *)sv.sival_ptr;

    pthread_mutex_lock(&ledMem->lock);

    if (!ledMem->valid) {
        /* The LED is being destroyed; stop touching shared state. */
        pthread_mutex_unlock(&ledMem->lock);
        return;
    }

    switch (ledMem->mode) {
        case GPIO_LED_ONESHOT:
            digitalWrite(ledMem->pin, LOW);
            ledMem->mode = GPIO_LED_OFF;
            break;

        case GPIO_LED_BLINK_ON:
            digitalWrite(ledMem->pin, LOW);
            ledMem->mode = GPIO_LED_BLINK_OFF;
            ledMem->timerTrigger.it_value = ledMem->secondaryTime;
            timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL);
            break;

        case GPIO_LED_BLINK_OFF:
            digitalWrite(ledMem->pin, HIGH);
            ledMem->mode = GPIO_LED_BLINK_ON;
            ledMem->timerTrigger.it_value = ledMem->primaryTime;
            timer_settime(ledMem->timerId, 0, &ledMem->timerTrigger, NULL);
            break;

        default:
            /* Constant modes do not use the timer; should not reach here. */
            break;
    }

    pthread_mutex_unlock(&ledMem->lock);
}
