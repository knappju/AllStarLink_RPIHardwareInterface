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

#define LISTENER_THREAD_TIME_MS 250

typedef struct{
    uint32_t number;
    bool rxKey;
    bool txKey;
    uint8_t mode;
} Node;

typedef struct{
    pthread_mutex_t listenerLock;
    pthread_t id;
	long lastReadMs;
	bool halt;
    Node nodes[10]; ///TODO: make this dynamic or at least a #define for the max number of nodes we can track.
} Listener;

// Public Functions
void* listener(void* args);
int initListener(Listener *lMem);

//sudo asterisk -rx "lstats 443249" great way to get connected nodes and direction.
//sudo asterisk -rx "rpt stats 443240" great way to get the connected nodes...
// watch -n 0.5 'sudo asterisk -rx "rpt show variables 443240"; sudo asterisk -rx "rpt show channels 443240"'
//tail -f /var/log/asterisk/node_activity/443240/$(date +%Y%m%d).txt | grep -E "RXKEY|TXKEY|RXUNKEY|TXUNKEY"
// For this one, Make sure you clean up or delete unneeded logs...
#endif // LISTENER_H