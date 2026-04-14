#include "Listener.h"

// private variables
typedef struct{
    char name[17]; // 16 chars plus null terminator.
    long LastUpdate;
    char action[33]; // 32 chars plus null terminator.
} LogAction;

const char *logActionFilter[] = ACTION_FILTERS;

// private functions
void ParseLogAction(const char *logLine, LogAction *action);
void printLogAction(const LogAction *action);   
char *makeLogFilePathAndName();

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
    long fileLinesAtStart;
    long currentLocation;
    FILE *logFile;
    char buffer[256];

    if(args == NULL)
    {
        pthread_exit(NULL);
    }

    Listener *lMem = (Listener *)args;

    //find a good spot to start reading.
    logFile = fopen(makeLogFilePathAndName(), "r");
    if (!logFile)    {
        printf("Error opening log file: %s\n", makeLogFilePathAndName());
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
        logFile = fopen(makeLogFilePathAndName() , "r");
        if (!logFile)break;
        fseek(logFile, currentLocation, SEEK_SET);

        // Keep reading until we hit the end of the file
        while ((fgets(buffer, sizeof(buffer), logFile) != NULL) && !lMem->halt) {
            // search the line for the action types we care about. If we find one, parse it and add it to the list of recent actions.
            for(int i = 0; i < sizeof(logActionFilter)/sizeof(logActionFilter[0]); i++)
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
        usleep(500000); // sleep for 500ms
    }
    pthread_exit(NULL);
}

void ParseLogAction(const char *logLine, LogAction *action) {
    sscanf(logLine, "%ld,%32[^,],%16s", &action->LastUpdate, action->action, action->name);
}

void printLogAction(const LogAction *action) {
    printf("Time: %ld, Action: %s, Name: %s\n", action->LastUpdate, action->action, action->name);
} 

char *makeLogFilePathAndName(){
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char *output = malloc(80);
    if (output == NULL) {
        return NULL; // Handle allocation failure
    }
    // Use snprintf to safely format the entire path
    int len = snprintf(output, 80, "%s/%04d%02d%02d.txt", 
                       LOG_FILE_PATH, 
                       tm_info->tm_year + 1900, 
                       tm_info->tm_mon + 1, 
                       tm_info->tm_mday);
    // Check if the buffer was large enough
    if (len < 0 || len >= 80) {
        free(output);
        return NULL;
    }
    return output;
}