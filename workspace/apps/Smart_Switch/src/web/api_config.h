#ifndef API_CONFIG_H
#define API_CONFIG_H

#include <zephyr/net/http/server.h>

extern struct http_resource_detail_dynamic api_config_detail;
extern struct http_resource_detail_dynamic api_settings_detail;
extern struct http_resource_detail_dynamic api_schedules_detail;
extern struct http_resource_detail_dynamic api_schedules_wildcard_detail;

void api_config_init(void);

#endif /* API_CONFIG_H */
