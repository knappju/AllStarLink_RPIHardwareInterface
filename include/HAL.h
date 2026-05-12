#ifndef HAL_H
#define HAL_H

typedef enum {
    HAL_SUCCESS = 0,
    HAL_ERROR_UNDEFINED_ERROR = -1
} HALStatus_t;

typedef struct {
    pthread_t id;
    pthread_mutex_t HALLock;
    
} HAL_t;

HAL_t * initHAL();
HALStatus_t deinitHAL(HAL_t *hal);


#endif /* HAL_H */