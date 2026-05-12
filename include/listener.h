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
#include <sys/queue.h>
#include "globalDefines.h"
#include "sys/stat.h"
#include "ctype.h"

#define LISTENER_THREAD_TIME_MS 250
#define FILE_FIRST_READ_STARTING_LINE_OFFSET 25
#define FILE_LINE_CHAR_SIZE_MAX 48

TAILQ_HEAD(logHead, LogAction);

typedef struct LogAction{
    char name[17]; // 16 chars plus null terminator.
    long LastUpdate;
    char action[33]; // 32 chars plus null terminator.
    TAILQ_ENTRY(LogAction) entries; // For the linked list of recent actions.
} LogAction;

typedef struct{
    char * NodeNumber;
    pthread_t id;
    pthread_mutex_t listenerLock;
	bool halt;
    struct logHead recentActions;
    int queueSize;
} Listener;

// Public Functions
void* listener(void* args);
int initListener(Listener *lMem);
int countListenerActions(Listener *lMem);
void printListenerActions(Listener *lMem);

#endif // LISTENER_H