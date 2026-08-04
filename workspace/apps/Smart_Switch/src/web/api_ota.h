#ifndef API_OTA_H
#define API_OTA_H

#include <zephyr/net/http/service.h>

extern struct http_resource_detail_dynamic api_ota_start_detail;
extern struct http_resource_detail_dynamic api_ota_upload_detail;
extern struct http_resource_detail_dynamic api_ota_finish_detail;

#endif /* API_OTA_H */
