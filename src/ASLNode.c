/**
 * @file ASLNode.c
 * @brief AllStarLink node data model implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "ASLNode.h"

ASLNode *makeASLNode(char *name)
{
    ASLNode *p = malloc(sizeof(ASLNode));
    if (p != NULL) {
        strncpy(p->name, name, 16);
        p->name[16] = '\0';
        strncpy(p->friendlyName, "nofriendlyname", 32);
        p->friendlyName[32] = '\0';
        p->lastUpdate = 0;
        p->rxKey = false;
        p->txKey = false;
        p->mode = 0;
        p->desiredChannelNumber = 255;
        TAILQ_INIT(&p->schedule);
    }
    return p;
}

/* Comparator for the red-black tree: compares nodes by their name strings.
 * Also accepts a plain (char *) as either argument because the name field is
 * the first member of ASLNode, so a char* and an ASLNode* point to the same
 * bytes for strcmp purposes. */
int compareASLNode(const void *a, const void *b)
{
    const ASLNode *p1 = (const ASLNode *)a;
    const ASLNode *p2 = (const ASLNode *)b;

    assert(a != NULL);
    assert(b != NULL);

    return strncmp(p1->name, p2->name, 16);
}

void destroyASLNode(void *d)
{
    assert(d != NULL);

    ASLNode *node = (ASLNode *)d;
    emptyASLNodeSchedule(node);
    free(node);
}

void emptyASLNodeSchedule(void *d)
{
    assert(d != NULL);

    ASLNode *node = (ASLNode *)d;
    scheduleItem *item;
    while (!TAILQ_EMPTY(&node->schedule)) {
        item = TAILQ_FIRST(&node->schedule);
        TAILQ_REMOVE(&node->schedule, item, entries);
        free(item);
    }
}

void addScheduleItem(ASLNode *node, char *netName,
                     long startUTCWeekTimeInSeconds,
                     long endUTCWeekTimeInSeconds,
                     uint8_t mode, uint8_t priority)
{
    scheduleItem *p = malloc(sizeof(scheduleItem));
    if (p != NULL && node != NULL) {
        p->owner = node;
        strncpy(p->netName, netName, 32); /* leave room for null terminator */
        p->netName[32] = '\0';
        p->startUTCWeekTimeInSeconds = startUTCWeekTimeInSeconds;
        p->endUTCWeekTimeInSeconds   = endUTCWeekTimeInSeconds;
        p->mode     = mode;
        p->priority = priority;
        TAILQ_INSERT_TAIL(&node->schedule, p, entries);
    }
}

void printASLNode(void *d)
{
    assert(d != NULL);

    ASLNode *node = (ASLNode *)d;
    printf("-----------------------------------\n");
    printf("%-16s%16ld\n", node->name, node->lastUpdate);
    printf("RX: %s TX: %s Mode: %u\n",
           node->rxKey ? "1" : "0",
           node->txKey ? "1" : "0",
           node->mode);
    printf("-----------------------------------\n");
}

void updateASLNode(ASLNode *node, long lastUpdate, const char *action)
{
    node->lastUpdate = lastUpdate;

    if (strcmp(action, "RXKEY") == 0) {
        node->rxKey = true;
    } else if (strcmp(action, "RXUNKEY") == 0) {
        node->rxKey = false;
    } else if (strcmp(action, "TXKEY") == 0) {
        node->txKey = true;
    } else if (strcmp(action, "TXUNKEY") == 0) {
        node->txKey = false;
    } else if (strcmp(action, "LINKTRX") == 0) {
        node->mode = 1;
    } else if (strcmp(action, "LINKMONITOR") == 0) {
        node->mode = 2;
    } else if (strcmp(action, "LINKDISC") == 0) {
        /* On disconnect, clear all active state. */
        node->rxKey = false;
        node->txKey = false;
        node->mode  = 0;
    }
}
