/**
 * @file listener.h
 * @brief Asterisk node-activity log file watcher.
 *
 * The listener thread tails the daily log file written by Asterisk's
 * app_rpt module, parses lines that match ACTION_FILTERS, and enqueues
 * LogAction items for the main loop to consume. The log path is
 * constructed from LOG_FILE_PREFIX_PATH, the node number read from
 * rpt.conf, and the current date.
 */

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
#include <sys/stat.h>
#include <ctype.h>
#include "globalDefines.h"

/* Polling interval used at the end of each read pass (ms). */
#define LISTENER_THREAD_TIME_MS 250

/* When first opening a log file, seek back this many lines so recent
 * history is available immediately rather than waiting for new events. */
#define FILE_FIRST_READ_STARTING_LINE_OFFSET 25

/* Maximum bytes per log line; lines longer than this are truncated. */
#define FILE_LINE_CHAR_SIZE_MAX 48

TAILQ_HEAD(logHead, LogAction);

typedef struct LogAction {
    char name[17];       /* node number string (max 16 chars + NUL) */
    long LastUpdate;     /* Unix timestamp parsed from the log line */
    char action[33];     /* action string, e.g. "RXKEY" (max 32 chars + NUL) */
    TAILQ_ENTRY(LogAction) entries;
} LogAction;

typedef struct {
    char            *NodeNumber;     /* heap-allocated; read from rpt.conf at init */
    pthread_t        id;
    pthread_mutex_t  listenerLock;   /* guards recentActions and queueSize */
    bool             halt;           /* set true to stop the listener thread */
    bool             initialized;
    struct logHead   recentActions;  /* TAILQ of unprocessed LogAction items */
    int              queueSize;      /* current depth of recentActions */
} Listener;

/**
 * @brief Thread entry point for the log listener. Do not call directly.
 * @param args Pointer to a Listener struct (passed through pthread_create).
 */
void *listener(void *args);

/**
 * @brief Initialize a Listener, read the node number from rpt.conf, and
 *        spawn the listener thread.
 * @param lMem Caller-allocated Listener struct.
 * @return 0 on success, -1 on error (node number not found, thread failure).
 */
int initListener(Listener *lMem);

/**
 * @brief Count the number of pending actions in the queue.
 * @note Not thread-safe; caller must hold listenerLock if needed.
 */
int countListenerActions(Listener *lMem);

/** @brief Print all pending actions to stdout (for debugging). */
void printListenerActions(Listener *lMem);

#endif /* LISTENER_H */
