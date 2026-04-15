#ifndef hardwareManager_h
#define hardwareManager_h

#include <stdlib.h>
#include <wiringPi.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include "globalDefines.h"

// Defines
#define HARDWARE_THREAD_TIME_MS 5
#define BUTTON_STATE_UNDEFINED 0
#define BUTTON_STATE_RELEASED 1
#define BUTTON_STATE_PRESSED 2

// Public Hardware Structures
//structure for a button
typedef struct {
	//values protected by hardwareLock mutex
	int pin;
	uint8_t debounceVals;
	bool debouncing;
	uint8_t state;
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

// Public Functions
int initHardware();
int testLeds();

#endif /* HARDWAREMANAGER_H */