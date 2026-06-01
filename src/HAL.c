#include "HAL.h"

//private functions for JSON parsing
char *jsoncStripComments(const char *jsonString);
static json_object * parseJSONFile(const char *filePath);

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

HALStatus_t HALLoadConfig(HAL_t *hal, const char *configFilePath)
{
    if (hal == NULL || configFilePath == NULL) {
        return HAL_ERROR_UNDEFINED_ERROR; // Invalid pointer
    }

    char *cleanedConfig = jsoncStripComments(configFilePath);
    if (cleanedConfig == NULL) {
        return HAL_ERROR_INVALID_CONFIG; // Failed to clean comments
    }
    json_object *config = parseJSONFile(cleanedConfig);
    free(cleanedConfig);
    if (config == NULL) {
        return HAL_ERROR_INVALID_CONFIG; // Failed to parse JSON
    }
    // Load configuration from the specified file path
    // This is a placeholder for actual configuration loading logic
    // You would typically read the file, parse it, and populate the HAL structure

    return HAL_SUCCESS; // Return success if configuration is loaded successfully
}

#pragma region JSONPARSING

char *jsoncStripComments(const char *jsonString)
{
    if (!jsonString) return NULL;
 
    size_t len = strlen(jsonString);
    /* Output can never be larger than input */
    char *out = malloc(len + 1);
    if (!out) return NULL;
 
    size_t i = 0, j = 0;
    int in_string = 0;
 
    while (i < len) {
        char c = jsonString[i];
 
        /* Track string boundaries (handle escaped quotes) */
        if (c == '"' && (i == 0 || jsonString[i - 1] != '\\')) {
            in_string = !in_string;
            out[j++] = c;
            i++;
            continue;
        }
 
        if (!in_string) {
            /* Line comment: // — strip to end of line */
            if (c == '/' && i + 1 < len && jsonString[i + 1] == '/') {
                while (i < len && jsonString[i] != '\n') i++;
                continue;
            }
 
            /* Block comment: /* ... */ 
            if (c == '/' && i + 1 < len && jsonString[i + 1] == '*') {
                i += 2;
                while (i + 1 < len && !(jsonString[i] == '*' && jsonString[i + 1] == '/')) i++;
                i += 2; /* skip closing */ 
                continue;
            }
        }
 
        out[j++] = c;
        i++;
    }
 
    out[j] = '\0';
    return out;
}

struct json_object * parseJSONFile(const char *filePath)
{
    if (!filePath) return NULL;

    FILE *f = fopen(filePath, "rb");
    if (!f) { perror("jsonc_parse_file: fopen"); return NULL; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fprintf(stderr, "jsonc_parse_file: file empty\n");
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

    /* ---- Use json_tokener_parse (available in all json-c versions) ---- */
    struct json_object *root = json_tokener_parse(clean);
    free(clean);

    if (!root) {
        fprintf(stderr, "jsonc_parse_file: json_tokener_parse failed — "
                        "check JSON syntax\n");
        return NULL;
    }

    /* Print only objects where "type" == "gpioButton" */
    if (json_object_get_type(root) == json_type_array) {
        int len = json_object_array_length(root);
        for (int i = 0; i < len; i++) {
            struct json_object *item = json_object_array_get_idx(root, i);

            struct json_object *type_val;
            if (json_object_object_get_ex(item, "type", &type_val) &&
                json_object_is_type(type_val, json_type_string) &&
                strcmp(json_object_get_string(type_val), "gpioButton") == 0)
            {
                printf("=== gpioButton [%d] ===\n", i);
                printf("%s\n", json_object_to_json_string_ext(item, JSON_C_TO_STRING_PRETTY));
            }
        }
    } else {
        fprintf(stderr, "parseJSONFile: expected top-level JSON array\n");
    }

    return root;
}

#pragma endregion JSONPARSING