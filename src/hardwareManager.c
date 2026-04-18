#include "hardwareManager.h"

// Config vars - ///TODO: make a config.json or similar to easily pull out these values.
int defineLeds[NUM_OF_LEDS] = {1,4,5,6,26,27,28};
int defineButtons[NUM_OF_BUTTONS] = {7,0,2,3};

// Private Functions
void* hardwareManager(void* args);
int cleanHardware();
int buttonAction(int buttonIndex, uint8_t state);
long currentMillis();

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
	int counter = 0;
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
					case 0:
						if(hwMem->buttons[buttonIndex].state == BUTTON_STATE_PRESSED)
						{
							buttonAction(buttonIndex,hwMem->buttons[buttonIndex].state);
						}
						hwMem->buttons[buttonIndex].debouncing = FALSE;
						hwMem->buttons[buttonIndex].state = BUTTON_STATE_RELEASED;
						break;
					case 255:
						if(hwMem->buttons[buttonIndex].state == BUTTON_STATE_RELEASED)
						{
							buttonAction(buttonIndex,hwMem->buttons[buttonIndex].state);
						}
						hwMem->buttons[buttonIndex].debouncing = FALSE;
						hwMem->buttons[buttonIndex].state = BUTTON_STATE_PRESSED;
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
		counter++;
		if(counter >= 1000){
			counter = 0;
			hwMem->leds[5].state = !hwMem->leds[5].state;
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
		hwMem->buttons[buttonIndex] = (Button){ .pin = defineButtons[buttonIndex], .debouncing = TRUE, .debounceVals = 55, .state = BUTTON_STATE_UNDEFINED };
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

/* FUNCTION: buttonAction
 * DESC: This function is called by the Hardware thread whenever a button is freshly pressed or released. It copies the (undecided yet) function pointer or string related to that buttons new state
 * to a (not yet created) action queue. It does this in a thread safe way because the queue is being serviced at a regular interval via another thread. 
 * INPUTS: buttonIndex - the button that was pressed, state - the new state of the button.
 * OUTPUTS: 0 for no errors.*/
int buttonAction(int buttonIndex, uint8_t state)
{
	static bool toggle1 = TRUE;
	static bool toggle2 = TRUE;
	static bool toggle3 = TRUE;
	if(buttonIndex == 1 && state == BUTTON_STATE_PRESSED)// seattle
	{
		if(toggle1)
		{
			system("asterisk -rx \"rpt fun 443240 *32462\"");
		}
		else
		{
			system("asterisk -rx \"rpt fun 443240 *12462\"");
		}
		toggle1 = !toggle1;
	}
	if(buttonIndex == 2 && state == BUTTON_STATE_PRESSED)// detroit
	{
		if(toggle2)
		{
			system("asterisk -rx \"rpt fun 443240 *347185\"");
		}
		else
		{
			system("asterisk -rx \"rpt fun 443240 *147185\"");
		}
	toggle2 = !toggle2;
	}
	if(buttonIndex == 3 && state == BUTTON_STATE_PRESSED)// east coast reflector
	{
		if(toggle3)
		{
			system("asterisk -rx \"rpt fun 443240 *347243\"");
		}
		else
		{
			system("asterisk -rx \"rpt fun 443240 *147243\"");
		}
		toggle3 = !toggle3;
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