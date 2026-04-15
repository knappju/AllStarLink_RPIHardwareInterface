#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "ASLNode.h"

ASLNode *makeASLNode(char *name)
{
	ASLNode *p;

	p = (ASLNode *) malloc(sizeof(ASLNode));
	if (p != NULL) {
		strncpy(p->name, name, 16);
		p->name[16] = '\0';
		p->LastUpdate = 0;
		p->rxKey = false;
		p->txKey = false;
		p->mode = 0;
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
	free(node);
}

void printASLNode(void *d)
{
    ASLNode *node;

    assert(d != NULL);

    node = (ASLNode *) d;

	assert(node != NULL);
	printf("Name: %s\n", node->name);
	printf("Last Update: %ld\n", node->LastUpdate);
	printf("RX Key: %s\n", node->rxKey ? "true" : "false");
	printf("TX Key: %s\n", node->txKey ? "true" : "false");
	printf("Mode: %u\n", node->mode);
}