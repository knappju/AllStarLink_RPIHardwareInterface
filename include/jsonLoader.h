#ifndef JSON_LOADER_H
#define JSON_LOADER_H

#include <stddef.h>
#include <cjson/cJSON.h>

/**
 * @file json_loader.h
 * @brief Generic JSON-to-struct loader using schema mapping.
 *
 * AI-generated module for loading JSON data into C structs based on a defined schema.
 * This module provides a reusable system for mapping JSON data (via cJSON)
 * into C structs using a schema definition. It supports:
 * - Primitive types (int, bool, string)
 * - Nested objects
 * - Arrays of objects
 * - Required/optional field validation
 *
 * The system works by describing struct fields using offsets and mapping them
 * to JSON keys.
 */

/**
 * @enum FieldType
 * @brief Supported JSON field types.
 */
typedef enum {
    FT_INT,     /**< Integer value */
    FT_BOOL,    /**< Boolean value */
    FT_STRING,  /**< Null-terminated string */
    FT_OBJECT,  /**< Nested object */
    FT_ARRAY    /**< Array of objects */
} FieldType;

/* Forward declarations */
typedef struct Field Field;
typedef struct Schema Schema;

/**
 * @typedef ParserFn
 * @brief Optional custom parser function.
 *
 * Allows overriding default parsing behavior for a field.
 *
 * @param out Pointer to destination field in struct
 * @param item cJSON object representing the value
 * @param field Field definition metadata
 * @return 0 on success, non-zero on failure
 */
typedef int (*ParserFn)(void *out, cJSON *item, const Field *field);

/**
 * @struct Field
 * @brief Describes a single struct field mapping.
 *
 * Each Field defines how a JSON key maps to a location in a struct.
 */
struct Field {
    const char *key;      /**< JSON key name */
    FieldType type;       /**< Expected data type */
    size_t offset;        /**< Byte offset within struct */

    int required;         /**< Whether the field must exist */

    /* Type-specific metadata */
    size_t size;          /**< Buffer size for strings */
    const Schema *sub;    /**< Schema for nested objects */
    const Schema *elem;   /**< Schema for array elements */

    size_t count_offset;  /**< Offset to store array length */

    ParserFn parser;      /**< Optional custom parser */
};

/**
 * @struct Schema
 * @brief Describes a struct layout for JSON mapping.
 */
struct Schema {
    const Field *fields;     /**< Array of field definitions */
    int count;               /**< Number of fields */
    size_t struct_size;      /**< Size of target struct */
};

/**
 * @brief Load JSON object into a struct using a schema.
 *
 * @param json Parsed cJSON object
 * @param out Pointer to struct to populate
 * @param schema Schema describing struct layout
 * @return 0 on success, non-zero on failure
 */
int load_schema(cJSON *json, void *out, const Schema *schema);

/**
 * @brief Load a JSON file into a cJSON object.
 *
 * @param filename Path to JSON file
 * @return cJSON root object, or NULL on failure
 */
cJSON *load_json_file(const char *filename);

/**
 * @brief Convenience function: load file and map into struct.
 *
 * Combines file loading, parsing, and schema mapping.
 *
 * @param filename Path to JSON file
 * @param out Pointer to struct to populate
 * @param schema Schema definition
 * @return 0 on success, non-zero on failure
 */
int load_json_into(const char *filename, void *out, const Schema *schema);

/* =========================
 * Field Definition Macros
 * ========================= */

/**
 * @brief Define an integer field mapping.
 */
#define FIELD_INT(type, field, key, required) \
    { key, FT_INT, offsetof(type, field), required, 0, NULL, NULL, 0, NULL }

/**
 * @brief Define a boolean field mapping.
 */
#define FIELD_BOOL(type, field, key, required) \
    { key, FT_BOOL, offsetof(type, field), required, 0, NULL, NULL, 0, NULL }

/**
 * @brief Define a string field mapping.
 */
#define FIELD_STRING(type, field, key, required) \
    { key, FT_STRING, offsetof(type, field), required, sizeof(((type*)0)->field), NULL, NULL, 0, NULL }

/**
 * @brief Define a nested object field mapping.
 */
#define FIELD_OBJECT(type, field, key, required, schema) \
    { key, FT_OBJECT, offsetof(type, field), required, 0, schema, NULL, 0, NULL }

/**
 * @brief Define an array field mapping.
 *
 * @param type Struct type
 * @param field Pointer-to-array field
 * @param count_field Field storing array length
 * @param key JSON key
 * @param required Whether field is required
 * @param elem_schema Schema for array elements
 */
#define FIELD_ARRAY(type, field, count_field, key, required, elem_schema) \
    { key, FT_ARRAY, offsetof(type, field), required, 0, NULL, elem_schema, offsetof(type, count_field), NULL }

#endif