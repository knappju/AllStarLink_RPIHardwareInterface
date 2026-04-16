
#ifndef ASLNODE_H
#define ASLNODE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Structure for ASLNode
typedef struct{
    char name[17]; // 16 chars plus null terminator.
    long lastUpdate;
    bool rxKey;
    bool txKey;
    uint8_t mode;
} ASLNode;

ASLNode *makeASLNode(char *name);
int compareASLNode(const void *a, const void *b);
void destroyASLNode(void *d);
void printASLNode(void *d);
void updateASLNode(ASLNode *node, long lastUpdate, const char* action);


#endif // ASLNODE_H