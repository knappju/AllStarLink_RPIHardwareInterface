/**
 * @file HAL.h
 * @brief Hardware Abstraction Layer (HAL) for buttons and LEDs.
 *
 * The HAL owns an array of HAL_Button_t and an array of HAL_Led_t. Each entry
 * holds a logical name (from the JSON config), a pointer to the underlying
 * driver instance, and a vtable of driver function pointers. This lets the
 * rest of the application address hardware by name without knowing which
 * driver (GPIO, MCP23017, etc.) backs a given device.
 *
 * Typical usage:
 *   HAL *hal = initHAL();
 *   HALLoadConfig(hal, HARDWARE_DEFINITIONS_FILE_PATH);
 *   int idx = HALFindLedByName(hal, "STATUS_LED");
 *   HALLedSetBlink(hal, idx, 500, 500);
 *   ...
 *   deinitHAL(hal);
 */

#ifndef HAL_H
#define HAL_H

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "globalDefines.h"
#include "json-c/json.h"

typedef enum {
    HAL_SUCCESS               =  0,
    HAL_ERROR_UNDEFINED_ERROR = -1,
    HAL_ERROR_INVALID_CONFIG  = -2,  /* JSON file missing, malformed, or wrong type */
    HAL_ERROR_NULL_POINTER    = -3,
    HAL_ERROR_NOT_FOUND       = -4,  /* named device does not exist */
    HAL_ERROR_ALLOC           = -5,  /* malloc/calloc returned NULL */
} HALStatus_t;

/* ── Button abstraction ─────────────────────────────────────────────────── */

typedef enum {
    HAL_BUTTON_TYPE_GPIO = 0,  /* wiringPi GPIO button */
} HALButtonType_t;

typedef struct {
    HALButtonType_t  type;
    char             logicalName[64];    /* name from HardwareDefinitions.json */
    void            *impl;              /* opaque pointer to driver memory */
    /* vtable: populated by HALLoadConfig() based on the driver type */
    HALStatus_t    (*read)          (void *impl, uint8_t *state);
    HALStatus_t    (*getTimeInState)(void *impl, unsigned long *timeInState);
    HALStatus_t    (*registerCB)    (void *impl, void (*cb)(uint8_t state));
    HALStatus_t    (*unregisterCB)  (void *impl);
    HALStatus_t    (*enableCB)      (void *impl);
    HALStatus_t    (*disableCB)     (void *impl);
    HALStatus_t    (*deinit)        (void *impl);
} HAL_Button_t;

/* ── LED abstraction ────────────────────────────────────────────────────── */

typedef enum {
    HAL_LED_MODE_OFF     = 0,
    HAL_LED_MODE_ON,
    HAL_LED_MODE_ONESHOT,
    HAL_LED_MODE_BLINK,
} HALLedMode_t;

typedef enum {
    HAL_LED_TYPE_GPIO    = 0,   /* wiringPi GPIO LED */
    HAL_LED_TYPE_MCP23017,      /* I2C expander LED (driver pending) */
} HALLedType_t;

typedef struct {
    HALLedType_t   type;
    char           logicalName[64];      /* name from HardwareDefinitions.json */
    void          *impl;                /* opaque pointer to driver memory */
    /* vtable: populated by HALLoadConfig() based on the driver type */
    HALStatus_t  (*setConstant)(void *impl, HALLedMode_t mode);
    HALStatus_t  (*setOneShot) (void *impl, unsigned long durationMs);
    HALStatus_t  (*setBlink)   (void *impl, unsigned long onDurationMs, unsigned long offDurationMs);
    HALStatus_t  (*deinit)     (void *impl);
} HAL_Led_t;

/* ── HAL ────────────────────────────────────────────────────────────────── */

typedef struct {
    pthread_t        id;
    pthread_mutex_t  HALLock;     /* guards the button/LED arrays during config load */
    HAL_Button_t    *buttons;     /* heap-allocated array, length = numButtons */
    int              numButtons;
    HAL_Led_t       *leds;        /* heap-allocated array, length = numLeds */
    int              numLeds;
    bool             initialized;
} HAL;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

/**
 * @brief Allocate a HAL, call wiringPiSetup(), and initialize the mutex.
 */
int initHAL(HAL *halMem); /* alternative that takes caller-allocated memory */

/**
 * @brief Load hardware definitions from a JSON config file.
 *
 * Parses HardwareDefinitions.json (which may contain C-style // and block
 * comments), allocates driver instances for each entry, and populates the
 * button and LED arrays with their vtables.
 *
 * @param hal            HAL instance returned by initHAL().
 * @param configFilePath Path to the JSON hardware definitions file.
 * @return HAL_SUCCESS, or a HALStatus_t error code.
 */
HALStatus_t HALLoadConfig(HAL *hal, const char *configFilePath);

/**
 * @brief Deinit all buttons and LEDs, destroy the mutex, and free the HAL.
 * @param hal HAL instance returned by initHAL().
 */
HALStatus_t deinitHAL(HAL *hal);

/* ── Button API ─────────────────────────────────────────────────────────── */

/**
 * @brief Find a button by its logical name.
 * @return Array index on success, -1 if not found.
 */
int         HALFindButtonByName    (HAL *hal, const char *logicalName);
HALStatus_t HALButtonRead          (HAL *hal, int index, uint8_t *state);
HALStatus_t HALButtonGetTimeInState(HAL *hal, int index, unsigned long *timeInState);
HALStatus_t HALButtonRegisterCB    (HAL *hal, int index, void (*cb)(uint8_t state));
HALStatus_t HALButtonUnregisterCB  (HAL *hal, int index);
HALStatus_t HALButtonEnableCB      (HAL *hal, int index);
HALStatus_t HALButtonDisableCB     (HAL *hal, int index);

/* ── LED API ────────────────────────────────────────────────────────────── */

/**
 * @brief Find an LED by its logical name.
 * @return Array index on success, -1 if not found.
 */
int         HALFindLedByName(HAL *hal, const char *logicalName);
HALStatus_t HALLedSetConstant(HAL *hal, int index, HALLedMode_t mode);
HALStatus_t HALLedSetOneShot (HAL *hal, int index, unsigned long durationMs);
HALStatus_t HALLedSetBlink   (HAL *hal, int index, unsigned long onDurationMs, unsigned long offDurationMs);


void buttonCallbackTest(uint8_t state);
#endif /* HAL_H */
