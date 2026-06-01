/**
 * @file ASLNode.h
 * @brief AllStarLink node data model.
 *
 * An ASLNode tracks the live state (RX/TX key, link mode) of one Asterisk
 * repeater node, plus an optional ordered schedule of time-based mode changes.
 * Nodes are stored in a red-black tree keyed on the node name string.
 */

#ifndef ASLNODE_H
#define ASLNODE_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/queue.h>

TAILQ_HEAD(scheduleHead, scheduleItem);

typedef struct {
    char name[17];                  /* node number string, e.g. "443240" (max 16 chars + NUL) */
    char friendlyName[33];          /* human-readable label (max 32 chars + NUL) */
    long lastUpdate;                /* Unix timestamp of the most recent log event */
    bool rxKey;                     /* true while the node is receiving (RXKEY) */
    bool txKey;                     /* true while the node is transmitting (TXKEY) */
    uint8_t mode;                   /* current link mode: 0=idle, 1=LINKTRX, 2=LINKMONITOR */
    uint8_t desiredChannelNumber;   /* 255 = no preference */
    struct scheduleHead schedule;   /* ordered list of scheduled mode changes */
} ASLNode;

typedef struct scheduleItem {
    ASLNode *owner;
    long startUTCWeekTimeInSeconds; /* seconds since Sunday 00:00:00 UTC */
    long endUTCWeekTimeInSeconds;
    uint8_t mode;                   /* mode to apply during this window */
    char netName[33];               /* name of the net/event (max 32 chars + NUL) */
    uint8_t priority;               /* higher value = higher priority */
    TAILQ_ENTRY(scheduleItem) entries;
} scheduleItem;

/**
 * @brief Allocate and zero-initialize an ASLNode with the given name.
 * @param name Node number string (max 16 chars).
 * @return Pointer to new node, or NULL on allocation failure.
 */
ASLNode *makeASLNode(char *name);

/**
 * @brief Red-black tree comparator: compares two ASLNode pointers by name.
 * @return Negative, zero, or positive per strncmp semantics.
 */
int compareASLNode(const void *a, const void *b);

/** @brief Free a heap-allocated ASLNode and its schedule. */
void destroyASLNode(void *d);

/** @brief Print a one-line summary of the node to stdout. */
void printASLNode(void *d);

/**
 * @brief Update node state from a parsed log action string.
 * @param node       Node to update.
 * @param lastUpdate Timestamp of the log event.
 * @param action     One of the ACTION_FILTERS strings (e.g. "RXKEY").
 */
void updateASLNode(ASLNode *node, long lastUpdate, const char *action);

/** @brief Free all schedule items without freeing the node itself. */
void emptyASLNodeSchedule(void *d);

/**
 * @brief Append a schedule item to a node's schedule list.
 * @param node                      Owning node.
 * @param netName                   Net/event name (max 32 chars).
 * @param startUTCWeekTimeInSeconds Start time (seconds since Sunday 00:00:00 UTC).
 * @param endUTCWeekTimeInSeconds   End time.
 * @param mode                      Mode to apply during the window.
 * @param priority                  Scheduling priority.
 */
void addScheduleItem(ASLNode *node, char *netName,
                     long startUTCWeekTimeInSeconds,
                     long endUTCWeekTimeInSeconds,
                     uint8_t mode, uint8_t priority);

#endif /* ASLNODE_H */
