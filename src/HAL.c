/**
 * @file HAL.c
 * @brief Hardware Abstraction Layer implementation.
 *
 * HALLoadConfig() parses HardwareDefinitions.json, instantiates the
 * appropriate driver for each entry ("gpioButton", "gpioLed", "MCP23017Led"),
 * and wires the driver's functions into the HAL vtable. The rest of the
 * application then calls hardware by logical name without knowing which
 * physical driver is beneath.
 *
 * The JSON parser (jsoncStripComments + json_tokener_parse) pre-processes the
 * file to remove C-style line and block comments before handing it to json-c,
 * since the config files use comments for documentation.
 */

#include <wiringPi.h>
#include "HAL.h"
#include "gpioButton.h"
#include "gpioLed.h"
#include "jsonConfig.h"

/* ── GPIO button vtable wrappers ────────────────────────────────────────── */

/* These thin wrappers translate gpioButton status codes to HAL status codes
 * and provide a uniform void* impl signature for the vtable. */

static HALStatus_t gpioBtn_read(void *impl, uint8_t *state) {
    return gpioButtonRead(impl, state) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioBtn_getTimeInState(void *impl, unsigned long *t) {
    return gpioButtonGetTimeInState(impl, t) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioBtn_registerCB(void *impl, void (*cb)(uint8_t state)) {
    return gpioButtonRegisterCB(impl, cb) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioBtn_unregisterCB(void *impl) {
    return gpioButtonUnregisterCB(impl) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioBtn_enableCB(void *impl) {
    return gpioButtonEnableCB(impl) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioBtn_disableCB(void *impl) {
    return gpioButtonDisableCB(impl) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioBtn_deinit(void *impl) {
    return gpioButtonDeinit(impl) == GPIO_BUTTON_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}

/* ── GPIO LED vtable wrappers ───────────────────────────────────────────── */

/* Map the HAL mode enum to the gpio driver's mode enum. Only ON/OFF are valid
 * for setConstant; ONESHOT and BLINK have dedicated API calls. */
static gpioLedMode_t halModeToGpio(HALLedMode_t mode) {
    switch (mode) {
        case HAL_LED_MODE_ON:  return GPIO_LED_ON;
        case HAL_LED_MODE_OFF: /* fall through */
        default:               return GPIO_LED_OFF;
    }
}
static HALStatus_t gpioLed_setConstant(void *impl, HALLedMode_t mode) {
    return gpioLedSetConstant(impl, halModeToGpio(mode)) == GPIO_LED_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioLed_setOneShot(void *impl, unsigned long durationMs) {
    return gpioLedSetOneShot(impl, durationMs) == GPIO_LED_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioLed_setBlink(void *impl, unsigned long onMs, unsigned long offMs) {
    return gpioLedSetBlink(impl, onMs, offMs) == GPIO_LED_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}
static HALStatus_t gpioLed_deinit(void *impl) {
    return gpioLedDeinit(impl) == GPIO_LED_SUCCESS ? HAL_SUCCESS : HAL_ERROR_UNDEFINED_ERROR;
}

/* ── JSON config helpers ────────────────────────────────────────────────── */

/* Parse "up"/"down" pull resistor strings from JSON into wiringPi constants. */
static int parsePull(const char *str) {
    if (str && strcmp(str, "up")   == 0) return PUD_UP;
    if (str && strcmp(str, "down") == 0) return PUD_DOWN;
    return PUD_OFF;
}

/* Parse interrupt edge strings from JSON into wiringPi constants. */
static int parseInterruptEdge(const char *str) {
    if (str && strcmp(str, "falling") == 0) return INT_EDGE_FALLING;
    if (str && strcmp(str, "both")    == 0) return INT_EDGE_BOTH;
    return INT_EDGE_RISING;
}

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

int initHAL(HAL *halMem)
{
    if (halMem == NULL) {
        return -1;
    }

    wiringPiSetup();

    if (pthread_mutex_init(&halMem->HALLock, NULL) != 0)
        return -1;

    return 0;
}

HALStatus_t deinitHAL(HAL *hal)
{
    if (!hal) return HAL_ERROR_NULL_POINTER;

    for (int i = 0; i < hal->numButtons; i++) {
        HAL_Button_t *b = &hal->buttons[i];
        if (b->impl && b->deinit) b->deinit(b->impl);
    }
    free(hal->buttons);

    for (int i = 0; i < hal->numLeds; i++) {
        HAL_Led_t *l = &hal->leds[i];
        if (l->impl && l->deinit) l->deinit(l->impl);
    }
    free(hal->leds);

    pthread_mutex_destroy(&hal->HALLock);
    return HAL_SUCCESS;
}

HALStatus_t HALLoadConfig(HAL *hal, const char *configFilePath)
{
    if (!hal || !configFilePath) return HAL_ERROR_NULL_POINTER;

    json_object *root = parseJSONFile(configFilePath);
    if (!root) return HAL_ERROR_INVALID_CONFIG;

    if (json_object_get_type(root) != json_type_array) {
        json_object_put(root);
        return HAL_ERROR_INVALID_CONFIG;
    }

    int len = json_object_array_length(root);
    int nButtons = 0, nLeds = 0;

    /* First pass: count each device type so we can allocate exact-size arrays. */
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(root, i);
        json_object *typeObj;
        if (!json_object_object_get_ex(item, "type", &typeObj)) continue;
        const char *type = json_object_get_string(typeObj);
        if      (strcmp(type, "gpioButton")  == 0)                         nButtons++;
        else if (strcmp(type, "gpioLed")     == 0 ||
                 strcmp(type, "MCP23017Led") == 0)                          nLeds++;
    }

    if (nButtons > 0) {
        hal->buttons = calloc(nButtons, sizeof(HAL_Button_t));
        if (!hal->buttons) { json_object_put(root); return HAL_ERROR_ALLOC; }
    }
    if (nLeds > 0) {
        hal->leds = calloc(nLeds, sizeof(HAL_Led_t));
        if (!hal->leds) {
            free(hal->buttons);
            hal->buttons = NULL;
            json_object_put(root);
            return HAL_ERROR_ALLOC;
        }
    }

    /* Second pass: initialize each device and populate its vtable. */
    for (int i = 0; i < len; i++) {
        json_object *item = json_object_array_get_idx(root, i);
        json_object *typeObj, *pinObj, *nameObj, *debounceObj, *pullObj, *interruptObj;

        if (!json_object_object_get_ex(item, "type", &typeObj)) continue;
        const char *type = json_object_get_string(typeObj);

        json_object_object_get_ex(item, "logicalName", &nameObj);
        const char *name = nameObj ? json_object_get_string(nameObj) : "";

        if (strcmp(type, "gpioButton") == 0) {
            json_object_object_get_ex(item, "pin",           &pinObj);
            json_object_object_get_ex(item, "debounceTimeMS",&debounceObj);
            json_object_object_get_ex(item, "pull",          &pullObj);
            json_object_object_get_ex(item, "interrupt",     &interruptObj);

            int pin      = pinObj      ? json_object_get_int(pinObj)                            : -1;
            int debounce = debounceObj ? json_object_get_int(debounceObj)                        : 50;
            int pull     = parsePull(pullObj ? json_object_get_string(pullObj) : NULL);
            int edge     = parseInterruptEdge(interruptObj ? json_object_get_string(interruptObj) : NULL);

            void *impl = gpioButtonInit(pin, pull, debounce, edge);
            if (!impl) {
                fprintf(stderr, "HALLoadConfig: gpioButtonInit failed for '%s' (pin %d)\n", name, pin);
                continue;
            }

            HAL_Button_t *b   = &hal->buttons[hal->numButtons++];
            b->type           = HAL_BUTTON_TYPE_GPIO;
            strncpy(b->logicalName, name, sizeof(b->logicalName) - 1);
            b->impl           = impl;
            b->read           = gpioBtn_read;
            b->getTimeInState = gpioBtn_getTimeInState;
            b->registerCB     = gpioBtn_registerCB;
            b->unregisterCB   = gpioBtn_unregisterCB;
            b->enableCB       = gpioBtn_enableCB;
            b->disableCB      = gpioBtn_disableCB;
            b->deinit         = gpioBtn_deinit;

        } else if (strcmp(type, "gpioLed") == 0) {
            json_object_object_get_ex(item, "pin", &pinObj);
            int pin = pinObj ? json_object_get_int(pinObj) : -1;

            void *impl = gpioLedInit(pin);
            if (!impl) {
                fprintf(stderr, "HALLoadConfig: gpioLedInit failed for '%s' (pin %d)\n", name, pin);
                continue;
            }

            HAL_Led_t *l   = &hal->leds[hal->numLeds++];
            l->type        = HAL_LED_TYPE_GPIO;
            strncpy(l->logicalName, name, sizeof(l->logicalName) - 1);
            l->impl        = impl;
            l->setConstant = gpioLed_setConstant;
            l->setOneShot  = gpioLed_setOneShot;
            l->setBlink    = gpioLed_setBlink;
            l->deinit      = gpioLed_deinit;

        }
        /* TODO: add MCP23017Led case once the driver is implemented. */
    }

    json_object_put(root);
    return HAL_SUCCESS;
}

/* ── Button API ─────────────────────────────────────────────────────────── */

int HALFindButtonByName(HAL *hal, const char *logicalName)
{
    if (!hal || !logicalName) return -1;
    for (int i = 0; i < hal->numButtons; i++) {
        if (strcmp(hal->buttons[i].logicalName, logicalName) == 0) return i;
    }
    return -1;
}

const char *HALGetButtonName(HAL *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return NULL;
    return hal->buttons[index].logicalName;
}

HALStatus_t HALButtonRead(HAL *hal, int index, uint8_t *state)
{
    if (!hal || !state || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->read(b->impl, state);
}

HALStatus_t HALButtonGetTimeInState(HAL *hal, int index, unsigned long *timeInState)
{
    if (!hal || !timeInState || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->getTimeInState(b->impl, timeInState);
}

HALStatus_t HALButtonRegisterCB(HAL *hal, int index, void (*cb)(uint8_t state))
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->registerCB(b->impl, cb);
}

HALStatus_t HALButtonUnregisterCB(HAL *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->unregisterCB(b->impl);
}

HALStatus_t HALButtonEnableCB(HAL *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->enableCB(b->impl);
}

HALStatus_t HALButtonDisableCB(HAL *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->disableCB(b->impl);
}

/* ── LED API ────────────────────────────────────────────────────────────── */

int HALFindLedByName(HAL *hal, const char *logicalName)
{
    if (!hal || !logicalName) return -1;
    for (int i = 0; i < hal->numLeds; i++) {
        if (strcmp(hal->leds[i].logicalName, logicalName) == 0) return i;
    }
    return -1;
}

const char *HALGetLedName(HAL *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numLeds) return NULL;
    return hal->leds[index].logicalName;
}

HALStatus_t HALLedSetConstant(HAL *hal, int index, HALLedMode_t mode)
{
    if (!hal || index < 0 || index >= hal->numLeds) return HAL_ERROR_NULL_POINTER;
    HAL_Led_t *l = &hal->leds[index];
    return l->setConstant(l->impl, mode);
}

HALStatus_t HALLedSetOneShot(HAL *hal, int index, unsigned long durationMs)
{
    if (!hal || index < 0 || index >= hal->numLeds) return HAL_ERROR_NULL_POINTER;
    HAL_Led_t *l = &hal->leds[index];
    return l->setOneShot(l->impl, durationMs);
}

HALStatus_t HALLedSetBlink(HAL *hal, int index, unsigned long onDurationMs, unsigned long offDurationMs)
{
    if (!hal || index < 0 || index >= hal->numLeds) return HAL_ERROR_NULL_POINTER;
    HAL_Led_t *l = &hal->leds[index];
    return l->setBlink(l->impl, onDurationMs, offDurationMs);
}


int globalcounter = 1;

void buttonCallbackTest(uint8_t state){
    
    if(state == 0){
        printf("globalcounter: %d\n", globalcounter++);
    }
}
