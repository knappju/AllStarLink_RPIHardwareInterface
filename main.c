#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wiringPi.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>


//function defs
void* hardwareManager(void* args);
int initMemory();
int initHardware();
int cleanHardware();
int testLeds();
long currentMillis();
int buttonAction(int buttonIndex, bool state);

//function pointers
//typedef int (*buttonActionCB_fptr)(int, bool); //int buttonIndex, bool state

//global vars - ///TODO: make a config.json or similar to easily pull out these values.
#define NUM_OF_LEDS 4
#define NUM_OF_BUTTONS 4
int defineLeds[NUM_OF_LEDS] = {1,4,5,6};
int defineButtons[NUM_OF_BUTTONS] = {7,0,2,3};
#define HARDWARE_THREAD_TIME_MS 5

//create memory structures
//structure for a button
typedef struct {
	//values protected by hardwareLock mutex
	int pin;
	uint8_t debounceVals;
	bool debouncing;
	bool state;
	//end values protected by hardwareLock mutex
} Button;

//structure for a led
typedef struct {
	//values protected by hardwareLock mutex
	int pin;
	bool state;
	//end values protected by hardwareLock mutex
} Led;

// structure for hardware
typedef struct {
	pthread_t id;
	pthread_mutex_t hardwareLock;
	long lastReadMs;
	bool halt;
	//values protected by hardwareLock mutex
	Button buttons[NUM_OF_BUTTONS];
	Led leds[NUM_OF_LEDS];
	//end values protected by hardwareLock mutex
} Hardware;

// structure for app memory
typedef struct{
	Hardware hardware;
} AppMemory;

// structure for quick referencing pointers.
typedef struct {
	Hardware *hardware;
} App;

/* FUNCTION: main
 * DESC: Allocate memory and manage the threads in this program.
 *
 * INPUTS:
 * OUTPUTS:*/
int main()
{
	//create memory
	AppMemory *mem = calloc(1, sizeof(AppMemory));
	if (!mem) {
		return -1; //memory allocation failed.
	}

	//create app pointer.
	App app;
	app.hardware = &mem->hardware;

	//set up and kick off the hardware manager
	initHardware(app.hardware);

	for(int i = 0; i < 300; i++){
		pthread_mutex_lock(&app.hardware->hardwareLock);
		app.hardware->leds[0].state = !app.hardware->leds[0].state;
		pthread_mutex_unlock(&app.hardware->hardwareLock);
		delay(100);
	}

	app.hardware->halt = TRUE;

	//wait for the hardware manager to finish cleaning.
	pthread_join(app.hardware->id, NULL);

	free(mem);

	return 0;
}

/* FUNCTION: hardwareManager
 * DESC: This is a thread. It should run until it is told to stop by setting the hardware memory halt bool to TRUE.
 *       This thread operates at the period of HARDWARE_THREAD_TIME_MS. it goes through and checks all of the buttons
 *       states, does debouncing, and sets the LEDs. The Debounce works by shifting in the current state of the gpio
 *       into a uint8 sized value. If the value becomes 0xFF or 0x00, that means the button is debounced (or stayed,
 *       the same for 8 * HARDWARE_THREAD_TIME_MS).
 * INPUTS: args... this is currently just the hardware memory, but could be more variables.
 * OUTPUTS: null is returned*/
void* hardwareManager(void* args)
{
	if(args == NULL)
	{
		pthread_exit(NULL);
	}

	Hardware *hwMem = (Hardware *)args;
	int i = 0;
	while(!hwMem->halt)
	{
		///TODO: make this less time intensive? Look into setting button presses on an interupt or poll less frequently until a change is found then speed it up for a short period...
		delay(1);
		long currentTimeMs = currentMillis();
		if((currentTimeMs - hwMem->lastReadMs) >= HARDWARE_THREAD_TIME_MS)
		{
			i++;
			hwMem->lastReadMs = currentTimeMs;
			pthread_mutex_lock(&hwMem->hardwareLock);
			for(int buttonIndex = 0; buttonIndex < NUM_OF_BUTTONS; buttonIndex++)
			{
				hwMem->buttons[buttonIndex].debounceVals = (uint8_t)(hwMem->buttons[buttonIndex].debounceVals << 1) + digitalRead(hwMem->buttons[buttonIndex].pin);
				switch(hwMem->buttons[buttonIndex].debounceVals)
				{
					///TODO: make it so the first "press/release" is thrown out. This is it getting a defult state.
					case 0:
						if(hwMem->buttons[buttonIndex].state == FALSE)
						{
							buttonAction(buttonIndex,hwMem->buttons[buttonIndex].state);
						}
						hwMem->buttons[buttonIndex].debouncing = FALSE;
						hwMem->buttons[buttonIndex].state = TRUE;
						break;
					case 255:
						if(hwMem->buttons[buttonIndex].state == TRUE)
						{
							buttonAction(buttonIndex,hwMem->buttons[buttonIndex].state);
						}
						hwMem->buttons[buttonIndex].debouncing = FALSE;
						hwMem->buttons[buttonIndex].state = FALSE;
						break;
					default:
						hwMem->buttons[buttonIndex].debouncing = TRUE;
						break;
				}
			}
			for(int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++)
			{
				digitalWrite(hwMem->leds[ledIndex].pin, hwMem->leds[ledIndex].state);
			}
			pthread_mutex_unlock(&hwMem->hardwareLock);
		}
	}
	cleanHardware(hwMem);
	pthread_exit(NULL);
}

/* FUNCTION: initHardware
 * DESC: Used to initialize the memory and hardware states of all the hardware. 
 * 	 It then kicks off the hardwareManager thread.
 * INPUTS: Hardware *hwMem - Hardware related memory addresses
 * OUTPUTS: -1 for error, 0 for no error.*/
int initHardware(Hardware *hwMem)
{
	if(hwMem == NULL) return -1;

	wiringPiSetup();

	for(int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++)
	{
		hwMem->leds[ledIndex] = (Led){ .pin = defineLeds[ledIndex], .state = FALSE};
		pinMode(hwMem->leds[ledIndex].pin,OUTPUT);
		digitalWrite(hwMem->leds[ledIndex].pin,LOW);
	}
	for(int buttonIndex = 0; buttonIndex < NUM_OF_BUTTONS; buttonIndex++)
	{
		hwMem->buttons[buttonIndex] = (Button){ .pin = defineButtons[buttonIndex], .debouncing = TRUE, .debounceVals = 55, .state = FALSE };
		pinMode(hwMem->buttons[buttonIndex].pin,INPUT);
		pullUpDnControl(hwMem->buttons[buttonIndex].pin, PUD_OFF);
	}
	hwMem->halt = FALSE;
	hwMem->lastReadMs = currentMillis();
	pthread_mutex_init(&hwMem->hardwareLock,NULL);
	pthread_create(&hwMem->id,NULL,hardwareManager,hwMem);

	return 0;
}

/* FUNCTION: cleanHardware
 * DESC: Used to reset any hardware outputs back to low and inputs.
 *
 * INPUTS: Hardware *hwMem - Hardware related memory addresses
 * OUTPUTS: -1 for error, 0 for no error.*/
int cleanHardware(Hardware *hwMem)
{
	if(hwMem == NULL) return -1;
	for(int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++)
	{
		digitalWrite(hwMem->leds[ledIndex].pin,LOW);
		pinMode(hwMem->leds[ledIndex].pin,INPUT);
	}
	return 0;
}

/* FUNCTION: testLeds
 * DESC: Goes through each of the leds and turns them off, on, off
 *
 * INPUTS: Hardware *hwMem - Hardware related memory addresses
 * OUTPUTS:*/
int testLeds(Hardware *hwMem)
{
	if(hwMem == NULL) return -1;
	for(int ledIndex = 0; ledIndex < NUM_OF_LEDS; ledIndex++)
	{
		digitalWrite(hwMem->leds[ledIndex].pin,LOW);
		delay(100);
		digitalWrite(hwMem->leds[ledIndex].pin,HIGH);
		delay(100);
		digitalWrite(hwMem->leds[ledIndex].pin,LOW);
	}
	return 0;
}

/* FUNCTION: currentMillis()
 * DESC: called to get the system time in ms.
 *
 * INPUTS: n/a
 * OUTPUTS: system time in ms  */
long currentMillis()
{
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

/* FUNCTION: buttonAction
 * DESC: This function is called by the Hardware thread whenever a button is freshly pressed or released. It copies the (undecided yet) function pointer or string related to that buttons new state
 * to a (not yet created) action queue. It does this in a thread safe way because the queue is being serviced at a regular interval via another thread. 
 * INPUTS: buttonIndex - the button that was pressed, state - the new state of the button.
 * OUTPUTS: 0 for no errors.*/
int buttonAction(int buttonIndex, bool state)
{
	static bool toggle = TRUE;
	if(state)
	{
		printf("PRESSED %d STATE %d\n", buttonIndex, state);
		if(toggle)
		{
			///TODO: Whenever this function is called, a function pointer should be added to the queue for an action to be taken. It should only do this if the queue is not protected by a mutex lock.
			system("asterisk -rx \"rpt fun 443240 *3472440\"");
		}
		else
		{
			system("asterisk -rx \"rpt fun 443240 *1472440\"");
		}
	toggle = !toggle;
	}
	return 0;
}
