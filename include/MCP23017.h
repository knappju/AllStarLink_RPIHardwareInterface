#ifndef MCP23017_H
#define MCP23017_H

typedef enum {
    GPIO_LED_SUCCESS = 0,
    GPIO_LED_UNDEFINED_ERROR = -1,
    GPIO_LED_ERROR_NULL_POINTER,
    GPIO_LED_ERROR_INVALID_MODE
} MCP23017Status_t;

typedef enum {
    MCP23017_LED_OFF = 0,
    MCP23017_LED_ON = 1,
    MCP23017_LED_ONESHOT,
    MCP23017_LED_BLINK_ON,
    MCP23017_LED_BLINK_OFF
} MCP23017LedMode_t;

typedef struct {

}MCP23017Memory_t;

MCP23017Memory_t * MCP23017Init();//TODO: need to know pins for all everything by the time this is created.
MCP23017Status_t MCP23017Deinit(void* ledMemory);
MCP23017Status_t MCP23017SetConstant(void* ledMemory, MCP23017LedMode_t mode);
MCP23017Status_t MCP23017SetOneShot(void* ledMemory, unsigned long durationMs);
MCP23017Status_t MCP23017SetBlink(void* ledMemory, unsigned long onDurationMs, unsigned long offDurationMs);

#endif // MCP23017_H