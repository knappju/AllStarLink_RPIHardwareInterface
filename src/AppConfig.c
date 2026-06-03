/**
 * @file AppConfig.c
 * @brief Application configuration loader for AppConfig.json.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AppConfig.h"
#include "jsonConfig.h"

/* ── JSON helpers ───────────────────────────────────────────────────────── */

static const char *getString(json_object *obj, const char *key)
{
    json_object *val;
    if (!json_object_object_get_ex(obj, key, &val)) return NULL;
    return json_object_get_string(val);
}

static int getInt(json_object *obj, const char *key, int defaultVal)
{
    json_object *val;
    if (!json_object_object_get_ex(obj, key, &val)) return defaultVal;
    return json_object_get_int(val);
}

/* ── Name resolution ────────────────────────────────────────────────────── */

static int resolveLed(HAL *hal, const char *name)
{
    if (!name || strcmp(name, "none") == 0) return -1;
    int idx = HALFindLedByName(hal, name);
    if (idx == -1)
        fprintf(stderr, "AppConfig: LED '%s' not found in HAL\n", name);
    return idx;
}

static int resolveButton(HAL *hal, const char *name)
{
    if (!name || strcmp(name, "none") == 0) return -1;
    int idx = HALFindButtonByName(hal, name);
    if (idx == -1)
        fprintf(stderr, "AppConfig: button '%s' not found in HAL\n", name);
    return idx;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int loadAppConfig(AppConfig *cfg, const char *filePath, HAL *hal)
{
    if (!cfg || !filePath || !hal) return -1;

    memset(cfg, 0, sizeof(*cfg));
    cfg->mainTxLedIdx   = -1;
    cfg->mainRxLedIdx   = -1;
    cfg->mainAux1LedIdx = -1;
    cfg->mainAux2LedIdx = -1;

    json_object *root = parseJSONFile(filePath);
    if (!root) return -1;

    if (json_object_get_type(root) != json_type_object) {
        fprintf(stderr, "loadAppConfig: root is not a JSON object\n");
        json_object_put(root);
        return -1;
    }

    const char *ver = getString(root, "appConfigVersion");
    if (ver) strncpy(cfg->version, ver, sizeof(cfg->version) - 1);

    cfg->mainTxLedIdx   = resolveLed(hal, getString(root, "mainTxLed"));
    cfg->mainRxLedIdx   = resolveLed(hal, getString(root, "mainRxLed"));
    cfg->mainAux1LedIdx = resolveLed(hal, getString(root, "mainAux1Led"));
    cfg->mainAux2LedIdx = resolveLed(hal, getString(root, "mainAux2Led"));

    json_object *mappings;
    if (!json_object_object_get_ex(root, "channelMappings", &mappings) ||
        json_object_get_type(mappings) != json_type_array) {
        fprintf(stderr, "loadAppConfig: missing or invalid 'channelMappings'\n");
        json_object_put(root);
        return -1;
    }

    int n = json_object_array_length(mappings);
    if (n > 0) {
        cfg->channels = calloc(n, sizeof(ChannelMapping));
        if (!cfg->channels) {
            json_object_put(root);
            return -1;
        }
    }
    cfg->numChannels = n;

    for (int i = 0; i < n; i++) {
        json_object    *entry = json_object_array_get_idx(mappings, i);
        ChannelMapping *ch    = &cfg->channels[i];

        ch->channelNumber = getInt(entry, "channelNumber", -1);

        const char *node = getString(entry, "node");
        if (node) strncpy(ch->node, node, sizeof(ch->node) - 1);

        ch->con1LedIdx = resolveLed(hal,    getString(entry, "con1Led"));
        ch->con2LedIdx = resolveLed(hal,    getString(entry, "con2Led"));
        ch->txLedIdx   = resolveLed(hal,    getString(entry, "txLed"));
        ch->rxLedIdx   = resolveLed(hal,    getString(entry, "rxLed"));
        ch->buttonIdx  = resolveButton(hal, getString(entry, "button"));
    }

    json_object_put(root);
    return 0;
}

void deinitAppConfig(AppConfig *cfg)
{
    if (!cfg) return;
    free(cfg->channels);
    cfg->channels    = NULL;
    cfg->numChannels = 0;
}

int findChannelByNode(AppConfig *cfg, const char *node)
{
    if (!cfg || !node) return -1;
    for (int i = 0; i < cfg->numChannels; i++) {
        if (strcmp(cfg->channels[i].node, node) == 0) return i;
    }
    return -1;
}

void printAppConfig(AppConfig *cfg, HAL *hal)
{
    if (!cfg || !hal) return;

#define LED_NAME(idx)  ((idx) >= 0 ? HALGetLedName(hal, idx)    : "none")
#define BTN_NAME(idx)  ((idx) >= 0 ? HALGetButtonName(hal, idx) : "none")

    printf("AppConfig v%s\n", cfg->version);
    printf("  mainTxLed:   %2d (%s)\n", cfg->mainTxLedIdx,   LED_NAME(cfg->mainTxLedIdx));
    printf("  mainRxLed:   %2d (%s)\n", cfg->mainRxLedIdx,   LED_NAME(cfg->mainRxLedIdx));
    printf("  mainAux1Led: %2d (%s)\n", cfg->mainAux1LedIdx, LED_NAME(cfg->mainAux1LedIdx));
    printf("  mainAux2Led: %2d (%s)\n", cfg->mainAux2LedIdx, LED_NAME(cfg->mainAux2LedIdx));

    for (int i = 0; i < cfg->numChannels; i++) {
        ChannelMapping *ch = &cfg->channels[i];
        printf("  Channel %d  node=%-8s  "
               "con1=%2d (%-8s)  con2=%2d (%-8s)  "
               "tx=%2d (%-8s)  rx=%2d (%-8s)  "
               "btn=%2d (%s)\n",
               ch->channelNumber, ch->node,
               ch->con1LedIdx, LED_NAME(ch->con1LedIdx),
               ch->con2LedIdx, LED_NAME(ch->con2LedIdx),
               ch->txLedIdx,   LED_NAME(ch->txLedIdx),
               ch->rxLedIdx,   LED_NAME(ch->rxLedIdx),
               ch->buttonIdx,  BTN_NAME(ch->buttonIdx));
    }

#undef LED_NAME
#undef BTN_NAME
}
