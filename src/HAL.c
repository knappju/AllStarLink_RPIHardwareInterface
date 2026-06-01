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

/* ── Private helpers ────────────────────────────────────────────────────── */

static char        *jsoncStripComments(const char *jsonString);
static json_object *parseJSONFile(const char *filePath);

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

HAL_t *initHAL(void)
{
    wiringPiSetup();

    HAL_t *hal = calloc(1, sizeof(HAL_t));
    if (!hal) return NULL;

    if (pthread_mutex_init(&hal->HALLock, NULL) != 0) {
        free(hal);
        return NULL;
    }

    return hal;
}

HALStatus_t deinitHAL(HAL_t *hal)
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
    free(hal);
    return HAL_SUCCESS;
}

HALStatus_t HALLoadConfig(HAL_t *hal, const char *configFilePath)
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

int HALFindButtonByName(HAL_t *hal, const char *logicalName)
{
    if (!hal || !logicalName) return -1;
    for (int i = 0; i < hal->numButtons; i++) {
        if (strcmp(hal->buttons[i].logicalName, logicalName) == 0) return i;
    }
    return -1;
}

HALStatus_t HALButtonRead(HAL_t *hal, int index, uint8_t *state)
{
    if (!hal || !state || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->read(b->impl, state);
}

HALStatus_t HALButtonGetTimeInState(HAL_t *hal, int index, unsigned long *timeInState)
{
    if (!hal || !timeInState || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->getTimeInState(b->impl, timeInState);
}

HALStatus_t HALButtonRegisterCB(HAL_t *hal, int index, void (*cb)(uint8_t state))
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->registerCB(b->impl, cb);
}

HALStatus_t HALButtonUnregisterCB(HAL_t *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->unregisterCB(b->impl);
}

HALStatus_t HALButtonEnableCB(HAL_t *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->enableCB(b->impl);
}

HALStatus_t HALButtonDisableCB(HAL_t *hal, int index)
{
    if (!hal || index < 0 || index >= hal->numButtons) return HAL_ERROR_NULL_POINTER;
    HAL_Button_t *b = &hal->buttons[index];
    return b->disableCB(b->impl);
}

/* ── LED API ────────────────────────────────────────────────────────────── */

int HALFindLedByName(HAL_t *hal, const char *logicalName)
{
    if (!hal || !logicalName) return -1;
    for (int i = 0; i < hal->numLeds; i++) {
        if (strcmp(hal->leds[i].logicalName, logicalName) == 0) return i;
    }
    return -1;
}

HALStatus_t HALLedSetConstant(HAL_t *hal, int index, HALLedMode_t mode)
{
    if (!hal || index < 0 || index >= hal->numLeds) return HAL_ERROR_NULL_POINTER;
    HAL_Led_t *l = &hal->leds[index];
    return l->setConstant(l->impl, mode);
}

HALStatus_t HALLedSetOneShot(HAL_t *hal, int index, unsigned long durationMs)
{
    if (!hal || index < 0 || index >= hal->numLeds) return HAL_ERROR_NULL_POINTER;
    HAL_Led_t *l = &hal->leds[index];
    return l->setOneShot(l->impl, durationMs);
}

HALStatus_t HALLedSetBlink(HAL_t *hal, int index, unsigned long onDurationMs, unsigned long offDurationMs)
{
    if (!hal || index < 0 || index >= hal->numLeds) return HAL_ERROR_NULL_POINTER;
    HAL_Led_t *l = &hal->leds[index];
    return l->setBlink(l->impl, onDurationMs, offDurationMs);
}

/* ── JSON parsing ───────────────────────────────────────────────────────── */

/* Return a heap-allocated copy of jsonString with all C-style // and block
 * comments removed. The caller must free() the result.
 * String literals are left intact (comment markers inside strings are kept). */
static char *jsoncStripComments(const char *jsonString)
{
    if (!jsonString) return NULL;

    size_t len = strlen(jsonString);
    char *out = malloc(len + 1);
    if (!out) return NULL;

    size_t i = 0, j = 0;
    int in_string = 0;

    while (i < len) {
        char c = jsonString[i];

        /* Track whether we are inside a JSON string so we do not mistake
         * a "//" inside a string value for a comment marker. */
        if (c == '"' && (i == 0 || jsonString[i - 1] != '\\')) {
            in_string = !in_string;
            out[j++] = c;
            i++;
            continue;
        }

        if (!in_string) {
            /* Line comment: skip to end of line. */
            if (c == '/' && i + 1 < len && jsonString[i + 1] == '/') {
                while (i < len && jsonString[i] != '\n') i++;
                continue;
            }
            if (c == '/' && i + 1 < len && jsonString[i + 1] == '*') {
                i += 2;
                while (i + 1 < len && !(jsonString[i] == '*' && jsonString[i + 1] == '/')) i++;
                i += 2;
                continue;
            }
        }

        out[j++] = c;
        i++;
    }

    out[j] = '\0';
    return out;
}

/* Read filePath into memory, strip comments, and parse with json-c. */
static json_object *parseJSONFile(const char *filePath)
{
    if (!filePath) return NULL;

    FILE *f = fopen(filePath, "rb");
    if (!f) { perror("parseJSONFile: fopen"); return NULL; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fprintf(stderr, "parseJSONFile: file empty\n");
        fclose(f);
        return NULL;
    }

    char *raw = malloc((size_t)size + 1);
    if (!raw) { fclose(f); return NULL; }
    fread(raw, 1, (size_t)size, f);
    raw[size] = '\0';
    fclose(f);

    char *clean = jsoncStripComments(raw);
    free(raw);
    if (!clean) return NULL;

    struct json_object *root = json_tokener_parse(clean);
    free(clean);

    if (!root) {
        fprintf(stderr, "parseJSONFile: json_tokener_parse failed\n");
        return NULL;
    }

    return root;
}
