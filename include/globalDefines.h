/**
 * @file globalDefines.h
 * @brief Project-wide constants: version, array sizes, file paths, and
 *        initialization error bitmask values.
 */

#ifndef GLOBALDEFINES_H
#define GLOBALDEFINES_H

#define SOFTWARE_REVISION_NUMBER "3.0.0.0"

/* Asterisk log action strings that are relevant to node state tracking. */
#define ACTION_FILTERS {"RXKEY", "TXKEY", "RXUNKEY", "TXUNKEY", "LINKTRX", "LINKMONITOR", "LINKDISC"}

/* Default filesystem paths for all configuration files. */
#define RPT_CONF_FILE_PATH              "/etc/asterisk/rpt.conf"
#define LOG_FILE_PREFIX_PATH            "/var/log/asterisk/node_activity"
#define HARDWARE_DEFINITIONS_FILE_PATH  "/etc/asl-interface/HardwareDefinitions.json"
#define APPCONFIG_FILE_PATH             "/etc/asl-interface/AppConfig.json"
#define NODES_FILE_PATH                 "/etc/asl-interface/Nodes.json"

/*
 * Initialization error bitmask values.
 * OR these together to accumulate multiple errors in initializationResult.
 */
#define HARDWARE_DEFINITION_FILE_PATH_INIT_ERROR  0x0001
#define APPCONFIG_FILE_PATH_INIT_ERROR            0x0002
#define NODES_FILE_PATH_INIT_ERROR                0x0004
#define RPT_CONF_FILE_PATH_INIT_ERROR             0x0008
#define APP_MEMORY_ALLOCATION_INIT_ERROR          0x0010
#define RB_TREE_ALLOCATION_INIT_ERROR             0x0020
#define LISTENER_THREAD_INIT_ERROR                0x0040
#define HAL_INIT_ERROR                            0x0080

#endif /* GLOBALDEFINES_H */
