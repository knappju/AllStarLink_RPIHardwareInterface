#ifndef ASLNODE_H
#define ASLNODE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Structure for ASLNode
typedef struct{
    char name[17]; // 16 chars plus null terminator.
    long LastUpdate;
    bool rxKey;
    bool txKey;
    uint8_t mode;
} ASLNode;

#endif // ASLNODE_H