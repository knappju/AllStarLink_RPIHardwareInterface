/**
 * @file listener.c
 * @brief Asterisk node-activity log file watcher implementation.
 *
 * The listener thread tails the daily log file at:
 *   LOG_FILE_PREFIX_PATH/<nodeNumber>/YYYYMMDD.txt
 *
 * On startup it seeks to FILE_FIRST_READ_STARTING_LINE_OFFSET lines from the
 * end so recent history is immediately available. It handles log rotation
 * (path changes when the date rolls over) and truncation (file shrinks).
 *
 * Matching log lines are parsed by ParseLogAction() and queued in
 * lMem->recentActions under lMem->listenerLock. The main loop drains that
 * queue and updates the node tree accordingly.
 */

#include "listener.h"

#define MAX_QUEUE_SIZE 100

static const char *logActionFilter[] = ACTION_FILTERS;

/* Private function declarations */
static void ParseLogAction(const char *logLine, LogAction *action);
static void printLogAction(const LogAction *action);
static int  getNodeNumberFromRPTConfig(char **nodeNumberPtr);
static void makeLogFilePathAndName(char *output, size_t size, const char *nodeNumber);

int initListener(Listener *lMem)
{
    if (lMem == NULL) return -1;

    lMem->halt       = false;
    lMem->NodeNumber = NULL;

    if (getNodeNumberFromRPTConfig(&lMem->NodeNumber) == -1) {
        printf("Error getting node number from rpt.conf\n");
        return -1;
    }

    printf("Node Number: %s\n", lMem->NodeNumber);

    pthread_mutex_init(&lMem->listenerLock, NULL);

    if (pthread_create(&lMem->id, NULL, listener, lMem) != 0) {
        free(lMem->NodeNumber);
        lMem->NodeNumber = NULL;
        pthread_mutex_destroy(&lMem->listenerLock);
        return -1;
    }

    return 0;
}

void *listener(void *args)
{
    if (args == NULL) {
        pthread_exit(NULL);
    }

    Listener *lMem = (Listener *)args;
    FILE *logFile = NULL;
    char buffer[256];
    char currentPath[80] = {0};
    char newPath[80]     = {0};
    long currentLocation = 0;

    TAILQ_INIT(&lMem->recentActions);

    /* Open the log file and seek near the end so we catch recent events
     * without replaying the entire file history. */
    makeLogFilePathAndName(currentPath, sizeof(currentPath), lMem->NodeNumber);
    logFile = fopen(currentPath, "r");
    if (logFile) {
        fseek(logFile, 0, SEEK_END);
        long fileSize = ftell(logFile);
        currentLocation = fileSize - FILE_FIRST_READ_STARTING_LINE_OFFSET * FILE_LINE_CHAR_SIZE_MAX;
        if (currentLocation < 0) currentLocation = 0;
        fseek(logFile, currentLocation, SEEK_SET);
        fgets(buffer, sizeof(buffer), logFile); /* advance past any partial line */
        currentLocation = ftell(logFile);
    } else {
        printf("Log file not found at startup, will keep trying to find it.\n");
    }

    while (!lMem->halt) {
        /* Detect log rotation: path changes when the calendar date rolls over. */
        makeLogFilePathAndName(newPath, sizeof(newPath), lMem->NodeNumber);
        if (strcmp(currentPath, newPath) != 0) {
            printf("Log rotated: %s -> %s\n", currentPath, newPath);
            if (logFile) {
                fclose(logFile);
                logFile = NULL;
            }
            strcpy(currentPath, newPath);
            currentLocation = 0;
        }

        if (!logFile) {
            logFile = fopen(currentPath, "r");
            if (!logFile) {
                usleep(500000); /* 500 ms: wait for Asterisk to create the log */
                continue;
            }
        }

        /* Detect log truncation (e.g. logrotate with copytruncate). */
        struct stat st;
        if (stat(currentPath, &st) == 0 && st.st_size < currentLocation) {
            printf("Log truncated, resetting position\n");
            currentLocation = 0;
        }

        /* Read new lines into a local list, then transfer to the shared queue
         * under the lock in one shot to minimize lock hold time. */
        fseek(logFile, currentLocation, SEEK_SET);

        TAILQ_HEAD(tempHead, LogAction) tempList;
        TAILQ_INIT(&tempList);

        while (fgets(buffer, sizeof(buffer), logFile) != NULL && !lMem->halt) {
            int nFilters = (int)(sizeof(logActionFilter) / sizeof(logActionFilter[0]));
            for (int i = 0; i < nFilters; i++) {
                if (strstr(buffer, logActionFilter[i]) != NULL) {
                    LogAction *newAction = malloc(sizeof(LogAction));
                    if (!newAction) {
                        printf("Memory allocation failed.\n");
                        break;
                    }
                    ParseLogAction(buffer, newAction);
                    TAILQ_INSERT_TAIL(&tempList, newAction, entries);
                    break; /* stop checking filters once one matches */
                }
            }
            currentLocation = ftell(logFile);
        }

        if (!TAILQ_EMPTY(&tempList)) {
            pthread_mutex_lock(&lMem->listenerLock);

            LogAction *action;
            while (!TAILQ_EMPTY(&tempList)) {
                action = TAILQ_FIRST(&tempList);
                TAILQ_REMOVE(&tempList, action, entries);
                TAILQ_INSERT_TAIL(&lMem->recentActions, action, entries);
                lMem->queueSize++;

                /* Drop the oldest entry if the queue is full to prevent unbounded growth. */
                if (lMem->queueSize >= MAX_QUEUE_SIZE) {
                    LogAction *oldest = TAILQ_FIRST(&lMem->recentActions);
                    if (oldest) {
                        TAILQ_REMOVE(&lMem->recentActions, oldest, entries);
                        free(oldest);
                        lMem->queueSize--;
                    }
                }
            }

            pthread_mutex_unlock(&lMem->listenerLock);
        }

        if (feof(logFile)) {
            clearerr(logFile);
            usleep(100000); /* 100 ms: no new data, poll again shortly */
        }
    }

    /* ── Cleanup ──────────────────────────────────────────────────── */

    if (logFile) {
        fclose(logFile);
    }

    /* Drain any remaining queue items. */
    pthread_mutex_lock(&lMem->listenerLock);
    LogAction *action;
    while (!TAILQ_EMPTY(&lMem->recentActions)) {
        action = TAILQ_FIRST(&lMem->recentActions);
        TAILQ_REMOVE(&lMem->recentActions, action, entries);
        free(action);
    }
    pthread_mutex_unlock(&lMem->listenerLock);

    if (lMem->NodeNumber) {
        free(lMem->NodeNumber);
    }

    pthread_exit(NULL);
}

/* Parse a CSV-formatted log line: "timestamp,action,nodeName"
 * The sscanf format matches the Asterisk app_rpt activity log layout. */
static void ParseLogAction(const char *logLine, LogAction *action)
{
    sscanf(logLine, "%ld,%32[^,],%16s", &action->LastUpdate, action->action, action->name);
}

static void printLogAction(const LogAction *action)
{
    printf("Name: %s, Action: %s, Time: %ld\n", action->name, action->action, action->LastUpdate);
}

/* Build the full log file path for the current calendar day.
 * Format: LOG_FILE_PREFIX_PATH/<nodeNumber>/YYYYMMDD.txt */
static void makeLogFilePathAndName(char *output, size_t size, const char *nodeNumber)
{
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    snprintf(output, size, "%s/%s/%04d%02d%02d.txt",
             LOG_FILE_PREFIX_PATH,
             nodeNumber,
             tm_info.tm_year + 1900,
             tm_info.tm_mon + 1,
             tm_info.tm_mday);
}

/* Read rpt.conf and return the first all-digit section name that is 1-6
 * characters long (the local node number). Allocates *node_number_ptr on
 * success; caller must free(). Returns 0 on success, -1 on failure. */
static int getNodeNumberFromRPTConfig(char **node_number_ptr)
{
    if (!node_number_ptr) return -1;

    FILE *file = fopen(RPT_CONF_FILE_PATH, "r");
    if (!file) return -1;

    char line[512];
    int result = -1;

    while (fgets(line, sizeof(line), file)) {
        char *ptr = line;

        /* Skip leading whitespace and comment/blank lines. */
        while (isspace(*ptr)) ptr++;
        if (*ptr == ';' || *ptr == '\n' || *ptr == '\0') continue;

        /* Look for a section header [NNNNNN] where N is a digit. */
        if (*ptr == '[') {
            ptr++;
            char *end = strchr(ptr, ']');
            if (end) {
                int len = (int)(end - ptr);
                if (len > 0 && len <= 6) {
                    char *p = ptr;
                    while (p < end && isdigit((unsigned char)*p)) p++;
                    if (p == end) { /* all digits → this is the node number */
                        *node_number_ptr = malloc(len + 1);
                        if (*node_number_ptr) {
                            strncpy(*node_number_ptr, ptr, len);
                            (*node_number_ptr)[len] = '\0';
                            result = 0;
                            break;
                        }
                    }
                }
            }
        }
    }

    fclose(file);
    return result;
}

void printListenerActions(Listener *lMem)
{
    printf("Recent Actions:\n");
    LogAction *action;
    TAILQ_FOREACH(action, &lMem->recentActions, entries) {
        printLogAction(action);
    }
}

int countListenerActions(Listener *lMem)
{
    int count = 0;
    LogAction *action;
    TAILQ_FOREACH(action, &lMem->recentActions, entries) {
        count++;
    }
    return count;
}
