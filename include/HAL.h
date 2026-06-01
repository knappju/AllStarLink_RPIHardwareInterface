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
    HAL_SUCCESS = 0,
    HAL_ERROR_UNDEFINED_ERROR = -1,
    HAL_ERROR_INVALID_CONFIG = -2,
    HAL_ERROR_NULL_POINTER = -3,
    HAL_ERROR_NOT_FOUND = -4,
    HAL_ERROR_ALLOC = -5,
} HALStatus_t;

/* ── Button abstraction ─────────────────────────────────────────────────── */

typedef enum {
    HAL_BUTTON_TYPE_GPIO = 0,
} HALButtonType_t;

typedef struct {
    HALButtonType_t  type;
    char             logicalName[64];
    void            *impl;
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
    HAL_LED_MODE_OFF = 0,
    HAL_LED_MODE_ON,
    HAL_LED_MODE_ONESHOT,
    HAL_LED_MODE_BLINK,
} HALLedMode_t;

typedef enum {
    HAL_LED_TYPE_GPIO = 0,
    HAL_LED_TYPE_MCP23017,
} HALLedType_t;

typedef struct {
    HALLedType_t   type;
    char           logicalName[64];
    void          *impl;
    HALStatus_t  (*setConstant)(void *impl, HALLedMode_t mode);
    HALStatus_t  (*setOneShot) (void *impl, unsigned long durationMs);
    HALStatus_t  (*setBlink)   (void *impl, unsigned long onDurationMs, unsigned long offDurationMs);
    HALStatus_t  (*deinit)     (void *impl);
} HAL_Led_t;

/* ── HAL ────────────────────────────────────────────────────────────────── */

typedef struct {
    pthread_t        id;
    pthread_mutex_t  HALLock;
    HAL_Button_t    *buttons;
    int              numButtons;
    HAL_Led_t       *leds;
    int              numLeds;
} HAL_t;

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

HAL_t      *initHAL(void);
HALStatus_t HALLoadConfig(HAL_t *hal, const char *configFilePath);
HALStatus_t deinitHAL(HAL_t *hal);

/* ── Button API ─────────────────────────────────────────────────────────── */

int         HALFindButtonByName   (HAL_t *hal, const char *logicalName);
HALStatus_t HALButtonRead         (HAL_t *hal, int index, uint8_t *state);
HALStatus_t HALButtonGetTimeInState(HAL_t *hal, int index, unsigned long *timeInState);
HALStatus_t HALButtonRegisterCB   (HAL_t *hal, int index, void (*cb)(uint8_t state));
HALStatus_t HALButtonUnregisterCB (HAL_t *hal, int index);
HALStatus_t HALButtonEnableCB     (HAL_t *hal, int index);
HALStatus_t HALButtonDisableCB    (HAL_t *hal, int index);

/* ── LED API ────────────────────────────────────────────────────────────── */

int         HALFindLedByName(HAL_t *hal, const char *logicalName);
HALStatus_t HALLedSetConstant(HAL_t *hal, int index, HALLedMode_t mode);
HALStatus_t HALLedSetOneShot (HAL_t *hal, int index, unsigned long durationMs);
HALStatus_t HALLedSetBlink   (HAL_t *hal, int index, unsigned long onDurationMs, unsigned long offDurationMs);

#endif /* HAL_H */
