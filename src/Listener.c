#include "Listener.h"

// private variables
typedef struct{
    char name[17]; // 16 chars plus null terminator.
    long LastUpdate;
    char action[33]; // 32 chars plus null terminator.
} LogAction;

const char *logActionFilter[7] = {"RXKEY", "TXKEY", "RXUNKEY", "TXUNKEY", "LINKTRX", "LINKLOCALMONITOR", "LINKDISC"};

// private functions
void ParseLogAction(const char *logLine, LogAction *action);
void printLogAction(const LogAction *action);
long currentMillis_1();

void ParseLogAction(const char *logLine, LogAction *action) {
    sscanf(logLine, "%ld,%32[^,],%16s", &action->LastUpdate, action->action, action->name);
}

void printLogAction(const LogAction *action) {
    printf("Time: %ld, Action: %s, Name: %s\n", action->LastUpdate, action->action, action->name);
}    

void* listener(void* args)
{
    long fileLinesAtStart;
    long currentLocation;
    FILE *logFile;
    char buffer[1024];

    if(args == NULL)
    {
        pthread_exit(NULL);
    }

    Listener *lMem = (Listener *)args;
    
    //find a good spot to start reading.
    logFile = fopen(LOG_FILE_LOCATION, "r");
    if (!logFile)    {
        printf("Error opening log file: %s\n", LOG_FILE_LOCATION);
        pthread_exit(NULL);
    }

    fseek(logFile, 0, SEEK_END);
    fileLinesAtStart = ftell(logFile);
    currentLocation = fileLinesAtStart - FILE_FIRST_READ_STARTING_LINE_OFFSET * FILE_LINE_CHAR_SIZE_MAX;
    if(currentLocation < 0) currentLocation = 0;
    fgets(buffer, sizeof(buffer), logFile); // Clear the buffer for the first read.
    fclose(logFile);

    while(!lMem->halt)
    {
        //Open the log file and hold onto the handle.
        logFile = fopen(LOG_FILE_LOCATION, "r");
        if (!logFile)break;
        fseek(logFile, currentLocation, SEEK_SET);

        // Keep reading until we hit the end of the file
        while (fgets(buffer, sizeof(buffer), logFile) != NULL) {
            // search the line for the action types we care about. If we find one, parse it and add it to the list of recent actions.
            for(int i = 0; i < 7; i++)
            {
                if(strstr(buffer, logActionFilter[i]) != NULL)
                {                    
                    LogAction action;
                    ParseLogAction(buffer, &action);
                    printLogAction(&action);
                    break; // No need to check other filters if we found a match.
                }
            }
            currentLocation = ftell(logFile); // Update for the next cycle
        }
        
        fclose(logFile);
        usleep(250000); // sleep for 250ms
    }
    pthread_exit(NULL);
}

        //usleep(500000); // sleep for 5s
        // long currentTimeMs = currentMillis_1();
        // if((currentTimeMs - lMem->lastReadMs) >= LISTENER_THREAD_TIME_MS)
        // {
        //     lMem->lastReadMs = currentTimeMs;
        //     pthread_mutex_lock(&lMem->listenerLock);
        //     getLogLastXActions("/var/log/asterisk/node_activity/443240/20260413.txt", 1); // Example: Get the last 1 action from log.txt
        //     pthread_mutex_unlock(&lMem->listenerLock);
        // }

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
    lMem->lastReadMs = currentMillis_1();
    pthread_mutex_init(&lMem->listenerLock,NULL);
    pthread_create(&lMem->id,NULL,listener,lMem);

    return 0;
}

/* FUNCTION: currentMillis_1()
 * DESC: called to get the system time in ms.
 *
 * INPUTS: n/a
 * OUTPUTS: system time in ms  */
long currentMillis_1()
{
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}
