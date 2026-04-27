/**
 * @file json_loader.c
 * @brief Implementation of schema-driven JSON loader.
 *
 * AI-generated module for loading JSON data into C structs based on a defined schema.
 * This module implements a generic system for mapping JSON data
 * (parsed via cJSON) into C structs using a schema definition.
 *
 * The loader works by:
 * 1. Iterating over schema fields
 * 2. Looking up JSON keys
 * 3. Validating types
 * 4. Writing values into struct memory using offsets
 *
 * Supports:
 * - Primitive types (int, bool, string)
 * - Nested objects (recursive schemas)
 * - Arrays of objects (dynamic allocation)
 */

#include "jsonLoader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 *                 INTERNAL PARSER FUNCTIONS
 * ============================================================
 */

/**
 * @brief Parse an integer field.
 *
 * @param out Pointer to destination field
 * @param item JSON value
 * @param f Field metadata (unused here)
 * @return 0 on success, non-zero on failure
 */
static int parse_int(void *out, cJSON *item, const Field *f) {
    if (!cJSON_IsNumber(item)) return -1;
    *(int*)out = item->valueint;
    return 0;
}

/**
 * @brief Parse a boolean field.
 *
 * @param out Pointer to destination field
 * @param item JSON value
 * @param f Field metadata (unused here)
 * @return 0 on success, non-zero on failure
 */
static int parse_bool(void *out, cJSON *item, const Field *f) {
    if (!cJSON_IsBool(item)) return -1;
    *(int*)out = cJSON_IsTrue(item);
    return 0;
}

/**
 * @brief Parse a string field.
 *
 * Copies the JSON string into a fixed-size buffer defined by Field.size.
 *
 * @param out Pointer to destination buffer
 * @param item JSON value
 * @param f Field metadata (must include size)
 * @return 0 on success, non-zero on failure
 */
static int parse_string(void *out, cJSON *item, const Field *f) {
    if (!cJSON_IsString(item)) return -1;
    snprintf((char*)out, f->size, "%s", item->valuestring);
    return 0;
}

/* Forward declaration for recursion */
int load_schema(cJSON *json, void *out, const Schema *schema);

/* ============================================================
 *                 FIELD DISPATCH LOGIC
 * ============================================================
 */

/**
 * @brief Parse a single field based on its type.
 *
 * This function dispatches parsing logic based on FieldType or
 * calls a custom parser if one is provided.
 *
 * @param field_ptr Pointer to location inside struct
 * @param item JSON value
 * @param f Field definition
 * @return 0 on success, non-zero on failure
 *
 * @note For FT_OBJECT and FT_ARRAY, parsing is recursive.
 * @warning Memory is dynamically allocated for arrays and must be freed by caller.
 */
static int parse_field(void *field_ptr, cJSON *item, const Field *f) {

    /* Use custom parser if provided */
    if (f->parser)
        return f->parser(field_ptr, item, f);

    switch (f->type) {

        case FT_INT:
            return parse_int(field_ptr, item, f);

        case FT_BOOL:
            return parse_bool(field_ptr, item, f);

        case FT_STRING:
            return parse_string(field_ptr, item, f);

        case FT_OBJECT:
            if (!cJSON_IsObject(item)) return -1;

            /**
             * Recursive call:
             * Load nested JSON object into nested struct
             */
            return load_schema(item, field_ptr, f->sub);

        case FT_ARRAY: {
            if (!cJSON_IsArray(item)) return -1;

            int count = cJSON_GetArraySize(item);

            /**
             * Allocate array of pointers to elements
             */
            void **arr = malloc(sizeof(void*) * count);
            if (!arr) return -1;

            for (int i = 0; i < count; i++) {
                cJSON *elem = cJSON_GetArrayItem(item, i);

                /**
                 * Allocate memory for each element struct
                 */
                arr[i] = malloc(f->elem->struct_size);
                if (!arr[i]) return -1;

                memset(arr[i], 0, f->elem->struct_size);

                /**
                 * Recursively load each element
                 */
                if (load_schema(elem, arr[i], f->elem) != 0)
                    return -1;
            }

            /**
             * Store pointer to allocated array
             */
            *(void**)field_ptr = arr;

            /**
             * Optionally store array count in struct
             */
            if (f->count_offset) {
                int *count_ptr = (int*)((char*)field_ptr + (f->count_offset - f->offset));
                *count_ptr = count;
            }

            return 0;
        }
    }

    return -1;
}

/* ============================================================
 *                 SCHEMA LOADER
 * ============================================================
 */

/**
 * @brief Load JSON object into struct using schema definition.
 *
 * Iterates over each field in the schema and:
 * - Looks up the corresponding JSON key
 * - Validates presence (if required)
 * - Parses and writes value into struct
 *
 * @param json Root JSON object
 * @param out Pointer to struct to populate
 * @param schema Schema definition
 * @return 0 on success, non-zero on failure
 *
 * @warning This function assumes 'out' is zero-initialized.
 */
int load_schema(cJSON *json, void *out, const Schema *schema) {
    for (int i = 0; i < schema->count; i++) {
        const Field *f = &schema->fields[i];

        cJSON *item = cJSON_GetObjectItem(json, f->key);

        /**
         * Calculate pointer to field inside struct using offset
         */
        void *field_ptr = (char*)out + f->offset;

        if (!item) {
            if (f->required) {
                printf("Missing required field: %s\n", f->key);
                return -1;
            }
            continue;
        }

        /**
         * Parse and assign value
         */
        if (parse_field(field_ptr, item, f) != 0) {
            printf("Invalid field: %s\n", f->key);
            return -1;
        }
    }

    return 0;
}

/* ============================================================
 *                 FILE LOADING
 * ============================================================
 */

/**
 * @brief Load and parse a JSON file into a cJSON object.
 *
 * Reads entire file into memory and parses using cJSON.
 *
 * @param filename Path to JSON file
 * @return cJSON root object, or NULL on failure
 *
 * @note Caller must free result using cJSON_Delete().
 */
cJSON *load_json_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);

    char *data = malloc(len + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(data);
    free(data);

    return json;
}

/* ============================================================
 *                 HIGH-LEVEL WRAPPER
 * ============================================================
 */

/**
 * @brief Load JSON file and populate struct using schema.
 *
 * This is the primary entry point for users of the library.
 *
 * Steps:
 * 1. Load JSON file
 * 2. Zero-initialize output struct
 * 3. Apply schema mapping
 * 4. Clean up JSON object
 *
 * @param filename Path to JSON file
 * @param out Pointer to struct to populate
 * @param schema Schema definition
 * @return 0 on success, non-zero on failure
 */
int load_json_into(const char *filename, void *out, const Schema *schema) {
    cJSON *json = load_json_file(filename);
    if (!json) return -1;

    /**
     * Ensure struct starts in known state
     */
    memset(out, 0, schema->struct_size);

    int result = load_schema(json, out, schema);

    cJSON_Delete(json);
    return result;
}