
#ifndef ASLNODE_H
#define ASLNODE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/queue.h>

TAILQ_HEAD(scheduleHead, scheduleItem);

// Structure for ASLNode
typedef struct{
    char name[17]; // 16 chars plus null terminator.
    char friendlyName[33];
    long lastUpdate;
    bool rxKey;
    bool txKey;
    uint8_t mode;
    uint8_t desiredChannelNumber;
    struct scheduleHead schedule;
} ASLNode;

typedef struct scheduleItem{
    ASLNode * owner;
    long startUTCWeekTimeInSeconds;
    long endUTCWeekTimeInSeconds;
    uint8_t mode;
    char netName[33];
    uint8_t priority;
    TAILQ_ENTRY(scheduleItem) entries;
} scheduleItem;

ASLNode *makeASLNode(char *name);
int compareASLNode(const void *a, const void *b);
void destroyASLNode(void *d);
void printASLNode(void *d);
void updateASLNode(ASLNode *node, long lastUpdate, const char* action);


#endif // ASLNODE_H