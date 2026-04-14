#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include "hardwareManager.h"
#include "Listener.h"
#include "ASLNode.h"

//function defs
void cleanUp(int signal_number);

// structure for app memory
typedef struct{
	Hardware hardware;
	Listener listener;
	ASLNode *nodes;
} AppMemory;

// structure for quick referencing pointers.
typedef struct {
	Hardware *hardware;
	Listener *listener;
	ASLNode *nodes;
} App;

// Global Vars
volatile sig_atomic_t shutdownFlag = FALSE; //flag used by signal handler to indicate shutdown

/* FUNCTION: main
 * DESC: Allocate memory and manage the threads in this program.
 *
 * INPUTS:
 * OUTPUTS:*/
int main()
{
	//set up signal handler to allow for graceful shutdown of the program.
	struct sigaction sigterm_action = { 
    	.sa_handler = cleanUp 
	};
	sigemptyset(&sigterm_action.sa_mask);

	if (sigaction(SIGINT, &sigterm_action, NULL) == -1 ||
    	sigaction(SIGTERM, &sigterm_action, NULL) == -1 ||
    	sigaction(SIGHUP, &sigterm_action, NULL) == -1) 
	{
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

	//create memory
	AppMemory *mem = calloc(1, sizeof(AppMemory));
	if (!mem) 
	{
		return -1; //memory allocation failed.
	}

	//connect memory to quick reference struct.
	App app;
	app.hardware = &mem->hardware;
	app.listener = &mem->listener;
	app.nodes = mem->nodes;
	
	//set up and kick off the hardware manager
	initHardware(app.hardware);

	//set up and kick off the listener
	initListener(app.listener);

	//testing for now, this keeps the program running for 24 hours.
	struct timeval endTime;
	struct timeval currentTime;

	gettimeofday(&endTime, NULL);
	gettimeofday(&currentTime, NULL);

	endTime.tv_sec += 86400; //add 24 hours to the current time for the end time.

	printf("ENDING TIME: %ld\n", endTime.tv_sec);

	while((currentTime.tv_sec <= endTime.tv_sec) && !shutdownFlag)
	{
		gettimeofday(&currentTime, NULL);
		pthread_mutex_lock(&app.hardware->hardwareLock);
		app.hardware->leds[0].state = !app.hardware->leds[0].state;
		pthread_mutex_unlock(&app.hardware->hardwareLock);
		delay(2000);
	}

	app.hardware->halt = TRUE;
	app.listener->halt = TRUE;

	//wait for the hardware manager to finish cleaning.
	pthread_join(app.hardware->id, NULL);
	pthread_join(app.listener->id, NULL);

	free(mem);

	return 0;
}

/* FUNCTION: cleanUp
 * DESC: This function is called by the signal handler when a SIGINT, SIGTERM or SIGHUP is sent to the program. It sets the global flag shutdownFlag to true, 
 * which should cause all threads to stop and return. This allows for a graceful shutdown of the program instead of just killing 
 * it and leaving hardware in an unknown state.
 * INPUTS: signal_number - the signal that was sent to the program. 
 * OUTPUTS: N/A*/
void cleanUp(int signal_number)
{
	shutdownFlag = TRUE;
}