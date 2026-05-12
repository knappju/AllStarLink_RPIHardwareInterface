#include "HAL.h"

HAL_t * initHAL()
{
    HAL_t *hal = (HAL_t *) malloc(sizeof(HAL_t));
    if (hal == NULL) {
        return NULL; // Memory allocation failed
    }

    if (pthread_mutex_init(&hal->HALLock, NULL) != 0) {
        free(hal);
        return NULL; // Mutex initialization failed
    }

    // Initialize other members of HAL_t as needed

    return hal;
}

HALStatus_t deinitHAL(HAL_t *hal)
{
    if (hal == NULL) {
        return HAL_ERROR_UNDEFINED_ERROR; // Invalid pointer
    }

    pthread_mutex_destroy(&hal->HALLock);
    free(hal);

    return HAL_SUCCESS;
}