/**
 * @file AppConfig.h
 * @brief Application configuration loaded from AppConfig.json.
 *
 * AppConfig stitches together logical HAL device names (from HardwareDefinitions.json)
 * with AllStarLink node numbers. At load time every string name is resolved to a HAL
 * array index so the rest of the application works with plain integers, never strings.
 *
 * "none" in the JSON (or an unrecognized name) resolves to -1, meaning no device
 * is assigned for that role.
 *
 * Typical usage:
 *   AppConfig cfg;
 *   loadAppConfig(&cfg, APPCONFIG_FILE_PATH, &hal);
 *   int ch = findChannelByNode(&cfg, "2462");
 *   HALLedSetBlink(&hal, cfg.channels[ch].txLedIdx, 200, 200);
 *   ...
 *   deinitAppConfig(&cfg);
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "HAL.h"

/* One channel: a node number bound to a set of HAL LED and button indices. */
typedef struct {
    int  channelNumber;
    char node[17];      /* node number string, e.g. "2462" (max 16 chars + NUL) */
    int  con1LedIdx;    /* HAL LED index, or -1 if not mapped */
    int  con2LedIdx;
    int  txLedIdx;
    int  rxLedIdx;
    int  buttonIdx;     /* HAL button index, or -1 if not mapped */
} ChannelMapping;

typedef struct {
    char            version[16];
    int             mainTxLedIdx;
    int             mainRxLedIdx;
    int             mainAux1LedIdx;
    int             mainAux2LedIdx;
    ChannelMapping *channels;     /* heap-allocated array, length = numChannels */
    int             numChannels;
} AppConfig;

/**
 * @brief Parse AppConfig.json and resolve all logical names to HAL indices.
 *
 * @param cfg      Caller-allocated AppConfig to populate.
 * @param filePath Path to AppConfig.json.
 * @param hal      Fully loaded HAL (HALLoadConfig must have been called first).
 * @return 0 on success, -1 on parse or allocation failure.
 */
int loadAppConfig(AppConfig *cfg, const char *filePath, HAL *hal);

/** @brief Free the channel array inside cfg. Does not free cfg itself. */
void deinitAppConfig(AppConfig *cfg);

/**
 * @brief Print a human-readable dump of the loaded config to stdout.
 *
 * Each resolved index is printed alongside the HAL logical name so the output
 * can be cross-checked directly against HardwareDefinitions.json and
 * AppConfig.json. Unresolved entries (-1) are shown as "(none)".
 *
 * @param cfg Populated AppConfig.
 * @param hal HAL that was passed to loadAppConfig (used for name lookups).
 */
void printAppConfig(AppConfig *cfg, HAL *hal);

/**
 * @brief Find the channel index whose node string matches node.
 * @return Channel array index on success, -1 if not found.
 */
int findChannelByNode(AppConfig *cfg, const char *node);

#endif /* APP_CONFIG_H */
