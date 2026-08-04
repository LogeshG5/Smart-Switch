#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

/*
 * Start WiFi management.
 *
 * If credentials exist:
 *     try station mode
 *
 * Otherwise:
 *     start AP mode
 */
int wifi_manager_start(void);

/*
 * Returns true when connected
 */
bool wifi_manager_is_connected(void);

/*
 * Force AP mode.
 *
 * Used later by:
 * - factory reset
 * - failed connection
 */
int wifi_manager_start_ap(void);

bool wifi_manager_is_ap_mode(void);

#endif
