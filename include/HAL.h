#ifndef HAL_H
#define HAL_H

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "globalDefines.h"
#include "json-c/json.h"

typedef enum {
    HAL_SUCCESS = 0,
    HAL_ERROR_UNDEFINED_ERROR = -1,
    HAL_ERROR_INVALID_CONFIG = -2,
} HALStatus_t;

typedef struct {
    pthread_t id;
    pthread_mutex_t HALLock;
} HAL_t;

HAL_t * initHAL();
HALStatus_t HALLoadConfig(HAL_t *hal, const char *configFilePath);
HALStatus_t deinitHAL(HAL_t *hal);


#endif /* HAL_H */