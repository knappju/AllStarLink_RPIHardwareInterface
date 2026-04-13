#include "Listener.h"

long currentMillis_1();

void* listener(void* args)
{
    if(args == NULL)
    {
        pthread_exit(NULL);
    }

    Listener *lMem = (Listener *)args;
    while(!lMem->halt)
    {
        usleep(50000); // sleep for 50ms
        long currentTimeMs = currentMillis_1();
        if((currentTimeMs - lMem->lastReadMs) >= LISTENER_THREAD_TIME_MS)
        {
            lMem->lastReadMs = currentTimeMs;
            pthread_mutex_lock(&lMem->listenerLock);

            pthread_mutex_unlock(&lMem->listenerLock);
        }
    }
    pthread_exit(NULL);
}

/* FUNCTION: initListener
 * DESC: Used to initialize the listener thread and related variables.
 *
 * NOTE: this is currently just a placeholder as the listener thread is not fully implemented. It will likely need to be reworked as the listener thread is developed.
 *
 * INPUTS: Listener *lMem - Listener related memory addresses. This should be allocated by the caller and passed in.
 * OUTPUTS: -1 for error, 0 for no error.*/
int initListener(Listener *lMem)
{
    if(lMem == NULL) return -1;

    lMem->halt = false;
    lMem->lastReadMs = currentMillis_1();
    pthread_mutex_init(&lMem->listenerLock,NULL);
    pthread_create(&lMem->id,NULL,listener,lMem);

    return 0;
}

/* FUNCTION: currentMillis_1()
 * DESC: called to get the system time in ms.
 *
 * INPUTS: n/a
 * OUTPUTS: system time in ms  */
long currentMillis_1()
{
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}
