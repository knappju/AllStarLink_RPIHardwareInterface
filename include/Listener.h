#ifndef LISTENER_H
#define LISTENER_H

#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "globalDefines.h"

#define LISTENER_THREAD_TIME_MS 250
#define FILE_FIRST_READ_STARTING_LINE_OFFSET 25
#define FILE_LINE_CHAR_SIZE_MAX 48

typedef struct{
    pthread_mutex_t listenerLock;
    pthread_t id;
	bool halt;
} Listener;

// Public Functions
void* listener(void* args);
int initListener(Listener *lMem);

#endif // LISTENER_H