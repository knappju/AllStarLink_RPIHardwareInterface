/**
 * @file jsonConfig.c
 * @brief Utilities for loading JSON config files that may contain C-style comments.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jsonConfig.h"

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
            /* Block comment: skip to closing */
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

json_object *parseJSONFile(const char *filePath)
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
        fprintf(stderr, "parseJSONFile: json_tokener_parse failed for '%s'\n", filePath);
        return NULL;
    }

    return root;
}
