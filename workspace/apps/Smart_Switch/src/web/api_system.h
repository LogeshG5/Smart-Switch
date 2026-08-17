#ifndef API_SYSTEM_H
#define API_SYSTEM_H

#include <zephyr/net/http/service.h>

// Exposes the detail structure so http_server.c can reference it.
extern struct http_resource_detail_dynamic api_system_reset_detail;
extern struct http_resource_detail_dynamic api_relay_state;
extern struct http_resource_detail_dynamic api_system_control_detail;

#endif /* API_SYSTEM_H */
