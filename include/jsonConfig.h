/**
 * @file jsonConfig.h
 * @brief Utilities for loading JSON config files that may contain C-style comments.
 *
 * Usage:
 *   json_object *root = parseJSONFile("/path/to/config.json");
 *   if (!root) { ... handle error ... }
 *   // ... use root via json-c API ...
 *   json_object_put(root);
 */

#ifndef JSON_CONFIG_H
#define JSON_CONFIG_H

#include "json-c/json.h"

/**
 * @brief Read a JSON file, strip C-style // and block comments, and parse it.
 *
 * The file may contain C-style line and block comments (useful for annotated
 * config files). Comment markers inside JSON string values are preserved.
 *
 * @param filePath Path to the JSON file.
 * @return Parsed json_object* on success (caller must json_object_put() it),
 *         or NULL on any error (file not found, parse failure, alloc failure).
 */
json_object *parseJSONFile(const char *filePath);

#endif /* JSON_CONFIG_H */
