#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <sys/queue.h>
#include "hardwareManager.h"
#include "Listener.h"
#include "rb.h"
#include "ASLNode.h"
#include "globalDefines.h"


//function defs
void cleanUp(int signal_number);
void findAndUpdateNodeForAction( rbtree *nodeTree, Listener *lMem, LogAction *action);

// structure for app memory
typedef struct{
	Hardware hardware;
	Listener listener;
	rbtree *nodeTree;
} AppMemory;

// structure for quick referencing pointers.
typedef struct {
	Hardware *hardware;
	Listener *listener;
	rbtree *nodeTree;
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
	int counter = 0;
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

	// create rbtree for nodes
	if((mem->nodeTree = rb_create(compareASLNode, destroyASLNode)) == NULL)
	{
		free(mem);
		return -1; //memory allocation failed.
	}

	//connect memory to quick reference struct.
	App app;
	app.hardware = &mem->hardware;
	app.listener = &mem->listener;
	app.nodeTree = mem->nodeTree;

	//put MAIN Node in rbtree
	ASLNode *mainNode = makeASLNode("MAIN");
	rb_insert(app.nodeTree, mainNode);

	//init and kick off threads.
	initHardware(app.hardware);
	initListener(app.listener);

	while( !shutdownFlag)
	{
		if(app.listener->recentActions.tqh_first != NULL)
		{
			//print the  number of ListenerActions.
			//printf("Number of Listener Actions: %d\n", countListenerActions(app.listener));
			//print the most recent action and delete it from the list until there are no more actions.
			while(app.listener->recentActions.tqh_first != NULL)
			{
				findAndUpdateNodeForAction(app.nodeTree, app.listener, app.listener->recentActions.tqh_first);
				
			}
			rbnode *node = rb_find(app.nodeTree, "MAIN");
			if(node != NULL)
			{
				ASLNode *mainNode = node->data;
				pthread_mutex_lock(&app.hardware->hardwareLock);
				app.hardware->leds[0].state = mainNode->txKey;
				pthread_mutex_unlock(&app.hardware->hardwareLock);
			}
			node = rb_find(app.nodeTree, "2462");//Seattle
			if(node != NULL)
			{
				ASLNode *mainNode = node->data;
				pthread_mutex_lock(&app.hardware->hardwareLock);
				app.hardware->leds[1].state = mainNode->rxKey;
				pthread_mutex_unlock(&app.hardware->hardwareLock);
			}
			node = rb_find(app.nodeTree, "47185");//Detroit
			if(node != NULL)
			{
				ASLNode *mainNode = node->data;
				pthread_mutex_lock(&app.hardware->hardwareLock);
				app.hardware->leds[2].state = mainNode->rxKey;
				pthread_mutex_unlock(&app.hardware->hardwareLock);
			}
			node = rb_find(app.nodeTree, "47243");//East Coast Reflector
			if(node != NULL)
			{
				ASLNode *mainNode = node->data;
				pthread_mutex_lock(&app.hardware->hardwareLock);
				app.hardware->leds[3].state = mainNode->rxKey;
				pthread_mutex_unlock(&app.hardware->hardwareLock);
			}

		}
		counter++;
		if(counter >= 100){
			counter = 0;
			pthread_mutex_lock(&app.hardware->hardwareLock);
			app.hardware->leds[4].state = !app.hardware->leds[4].state;
			pthread_mutex_unlock(&app.hardware->hardwareLock);
		}
		app.hardware->leds[6].state = app.listener->heartbeat;
		
		delay(10); //delay to prevent busy waiting.
		
	}

	//start the shutdown process for the threads.
	app.hardware->halt = TRUE;
	app.listener->halt = TRUE;

	//wait for the threads to finish before exiting the program.
	pthread_join(app.hardware->id, NULL);
	pthread_join(app.listener->id, NULL);

	rb_destroy(app.nodeTree);
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

void findAndUpdateNodeForAction( rbtree *nodeTree, Listener *lMem,LogAction *action)
{
	rbnode *node = rb_find(nodeTree, action->name);
	
	if (node != NULL) {
		printf("Time: %lu Updating Node: %s, Action: %s\n", (unsigned long)time(NULL), action->name, action->action);
		updateASLNode(node->data, action->LastUpdate, action->action);
	} else {
		printf("Time: %lu Adding Node: %s, Action: %s\n", (unsigned long)time(NULL), action->name, action->action);
		ASLNode *newNode = makeASLNode(action->name);
		updateASLNode(newNode, action->LastUpdate, action->action);
		rb_insert(nodeTree, newNode);
	}
	pthread_mutex_lock(&lMem->listenerLock);
	TAILQ_REMOVE(&lMem->recentActions, action, entries);
	lMem->queueSize--;
	printf("Time: %lu Remaining Actions: %d\n", (unsigned long)time(NULL), lMem->queueSize);
	pthread_mutex_unlock(&lMem->listenerLock);
	free(action);
}