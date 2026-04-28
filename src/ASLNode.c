#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "ASLNode.h"


/* FUNCTION: makeASLNode
 * DESC: A constructor that creates and returns an ASLNode. 
 *
 * INPUTS:
 * OUTPUTS:*/
ASLNode *makeASLNode(char *name)
{
	ASLNode *p;

	p = (ASLNode *) malloc(sizeof(ASLNode));
	if (p != NULL) {
		strncpy(p->name, name, 16);
		p->name[16] = '\0';
		strncpy(p->friendlyName, "nofriendlyname\0",33);
		p->lastUpdate = 0;
		p->rxKey = false;
		p->txKey = false;
		p->mode = 0;
		p->desiredChannelNumber = 255;
		TAILQ_INIT(&p->schedule);
	}
	return p;
}

int compareASLNode(const void *a, const void *b)
{
    ASLNode *p1, *p2;

    assert(a != NULL);
    assert(b != NULL);

    p1 = (ASLNode *) a;
    p2 = (ASLNode *) b;
    return strncmp(p1->name, p2->name, 16);
}

void destroyASLNode(void *d)
{
    ASLNode *node;
    
    assert(d != NULL);

    node = (ASLNode *) d;

	emptyASLNodeSchedule(node);

	free(node);
}

void emptyASLNodeSchedule(void *d){
	ASLNode *node;
    
    assert(d != NULL);

    node = (ASLNode *) d;
	
	scheduleItem *tempSchedule;
	while (!TAILQ_EMPTY(&node->schedule)){
        tempSchedule = TAILQ_FIRST(&node->schedule);
        TAILQ_REMOVE(&node->schedule, tempSchedule, entries);
        free(tempSchedule);
    }
}

void addScheduleItem(ASLNode *node, char* netName, long startUTCWeekTimeInSeconds, 
						long endUTCWeekTimeInSeconds, uint8_t mode, uint8_t priority){
	scheduleItem *p;

	p = (scheduleItem *) malloc(sizeof(scheduleItem));
	if (p != NULL && node != NULL) {
		p->owner = node;
		strncpy(p->netName, netName, 33);
		p->netName[33] = '\0';
		p->startUTCWeekTimeInSeconds = startUTCWeekTimeInSeconds;
		p->endUTCWeekTimeInSeconds = endUTCWeekTimeInSeconds;
		p->mode = mode;
		p->priority = priority;
		TAILQ_INSERT_TAIL(&node->schedule,p,entries);
	}
}

void printASLNode(void *d)
{
    ASLNode *node;

    assert(d != NULL);

    node = (ASLNode *) d;

	assert(node != NULL);
	printf("-----------------------------------\n");
	printf("%-16s%16ld\n", node->name, node->lastUpdate);
	printf("RX: %s TX: %s Mode: %u\n", node->rxKey ? "1" : "0", node->txKey ? "1" : "0", node->mode);
	printf("-----------------------------------\n");
}

void updateASLNode(ASLNode *node, long lastUpdate, const char*action)
{
	// print the node, and action
	
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
		node->mode = 1; // Example mode for LINKTRX
	} else if (strcmp(action, "LINKMONITOR") == 0) {
		node->mode = 2; // Example mode for LINKMONITOR
	} else if (strcmp(action, "LINKDISC") == 0) {
		node->rxKey = false;
		node->txKey = false;
		node->mode = 0; // Example mode for LINKDISC
	} 
}