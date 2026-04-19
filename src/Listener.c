#include "Listener.h"

#define MAX_QUEUE_SIZE 20
const char *logActionFilter[] = ACTION_FILTERS;

// private functions
void ParseLogAction(const char *logLine, LogAction *action);
void printLogAction(const LogAction *action);   
void makeLogFilePathAndName(char *output, size_t size);

/* FUNCTION: initListener
 * DESC: Used to initialize the listener thread and related variables.
 *
 * NOTE: this is currently just a placeholder as the listener thread is not fully implemented. It will likely need to be reworked as the listener thread is developed.
 *
 * INPUTS: Listener *lMem - Listener related memory addresses. This should be allocated by the caller and passed in.
 * OUTPUTS: -1 for error, 0 for no error.*/
int initListener(Listener *lMem)
{
    if(lMem == NULL) return -1;

    lMem->halt = false;
    pthread_mutex_init(&lMem->listenerLock,NULL);
    pthread_create(&lMem->id,NULL,listener,lMem);

    return 0;
}

void* listener(void* args)
{
    if (args == NULL) {
        pthread_exit(NULL);
    }

    Listener *lMem = (Listener *)args;
    FILE *logFile = NULL;
    char buffer[256];
    char currentPath[80] = {0};
    char newPath[80] = {0};
    long currentLocation = 0;

    TAILQ_INIT(&lMem->recentActions);

    makeLogFilePathAndName(currentPath, sizeof(currentPath));
    logFile = fopen(currentPath, "r");
    if (logFile) {
        fseek(logFile, 0, SEEK_END);
        long fileSize = ftell(logFile);
        currentLocation = fileSize - FILE_FIRST_READ_STARTING_LINE_OFFSET * FILE_LINE_CHAR_SIZE_MAX;
        if (currentLocation < 0) currentLocation = 0;
        fseek(logFile, currentLocation, SEEK_SET);
        fgets(buffer, sizeof(buffer), logFile);
        currentLocation = ftell(logFile);
    }

    while (!lMem->halt)
    {
        makeLogFilePathAndName(newPath, sizeof(newPath));
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
                usleep(500000); // wait 500ms
                continue;
            }
        }

        struct stat st;
        if (stat(currentPath, &st) == 0) {
            if (st.st_size < currentLocation) {
                printf("Log truncated, resetting position\n");
                currentLocation = 0;
            }
        }

        fseek(logFile, currentLocation, SEEK_SET);
        TAILQ_HEAD(tempHead, LogAction) tempList;
        TAILQ_INIT(&tempList);
        while (fgets(buffer, sizeof(buffer), logFile) != NULL && !lMem->halt)
        {
            for (int i = 0; i < sizeof(logActionFilter)/sizeof(logActionFilter[0]); i++)
            {
                if (strstr(buffer, logActionFilter[i]) != NULL)
                {
                    LogAction* newAction = malloc(sizeof(LogAction));
                    if (!newAction) {
                        printf("Memory allocation failed.\n");
                        break;
                    }

                    ParseLogAction(buffer, newAction);
                    TAILQ_INSERT_TAIL(&tempList, newAction, entries);
                    break;
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
            usleep(100000); // 100ms
        }
    }

    if (logFile) {
        fclose(logFile);
    }
    pthread_mutex_lock(&lMem->listenerLock);

    LogAction *action;
    while (!TAILQ_EMPTY(&lMem->recentActions)) {
        action = TAILQ_FIRST(&lMem->recentActions);
        TAILQ_REMOVE(&lMem->recentActions, action, entries);
        free(action);
    }
    pthread_mutex_unlock(&lMem->listenerLock);
    pthread_exit(NULL);
}

void ParseLogAction(const char *logLine, LogAction *action) {
    sscanf(logLine, "%ld,%32[^,],%16s", &action->LastUpdate, action->action, action->name);
}

void printLogAction(const LogAction *action) {
    printf("Name: %s, Action: %s, Time: %ld\n", action->name, action->action, action->LastUpdate);
}

void makeLogFilePathAndName(char *output, size_t size)
{
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    snprintf(output, size, "%s/%04d%02d%02d.txt",
             LOG_FILE_PATH,
             tm_info.tm_year + 1900,
             tm_info.tm_mon + 1,
             tm_info.tm_mday);
}

void printListenerActions(Listener *lMem) {
    LogAction *action;
    printf("Recent Actions:\n");
    TAILQ_FOREACH(action, &lMem->recentActions, entries) {
        printLogAction(action);
    }
}

int countListenerActions(Listener *lMem) {
    int count = 0;
    LogAction *action;
    TAILQ_FOREACH(action, &lMem->recentActions, entries) {
        count++;
    }
    return count;
}