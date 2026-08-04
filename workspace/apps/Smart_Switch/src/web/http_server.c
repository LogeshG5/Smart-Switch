#include "http_server.h"
#include "api_config.h"
#include "api_system.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/service.h> // Official Zephyr 4.4 HTTP Service header

LOG_MODULE_REGISTER(http_server, CONFIG_LOG_DEFAULT_LEVEL);

#define HTTP_SERVER_START_RETRIES 5
#define HTTP_SERVER_RETRY_DELAY_MS 1000

// --- Configuration ---
static uint16_t http_port = 80;

// --- Static Web Server Files ---
static const uint8_t index_html[] = {
#include "index.html.inc"
};
static const uint8_t style_css[] = {
#include "style.css.inc"
};
static const uint8_t app_js[] = {
#include "app.js.inc"
};

// --- Resource Detail Structures (Compliant with Zephyr 4.4 API) ---

static struct http_resource_detail_static index_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_STATIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_GET),
            .content_type = "text/html",
        },
    .static_data = index_html,
    .static_data_len = sizeof(index_html),
};

static struct http_resource_detail_static style_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_STATIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_GET),
            .content_type = "text/css",
        },
    .static_data = style_css,
    .static_data_len = sizeof(style_css),
};

static struct http_resource_detail_static js_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_STATIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_GET),
            .content_type = "application/javascript",
        },
    .static_data = app_js,
    .static_data_len = sizeof(app_js),
};

// --- HTTP Service Definition (Zephyr 4.4 Standard) ---
HTTP_SERVICE_DEFINE(tv_scheduler, "0.0.0.0", &http_port, 9, 10, NULL, NULL,
                    NULL);

// --- Resource Definitions (Verified 4-Argument Macro for Zephyr 4.4) ---
HTTP_RESOURCE_DEFINE(index_resource, tv_scheduler, "/", &index_detail);
HTTP_RESOURCE_DEFINE(style_resource, tv_scheduler, "/style.css", &style_detail);
HTTP_RESOURCE_DEFINE(js_resource, tv_scheduler, "/app.js", &js_detail);

// API resources (callbacks defined in api_config.c)
HTTP_RESOURCE_DEFINE(settings_api_resource, tv_scheduler, "/api/settings",
                     &api_settings_detail);

// Schedules endpoints (GET all / POST new and PUT/DELETE wildcard)
HTTP_RESOURCE_DEFINE(schedules_api_base_resource, tv_scheduler,
                     "/api/schedules", &api_schedules_detail);
HTTP_RESOURCE_DEFINE(schedules_api_wildcard_resource, tv_scheduler,
                     "/api/schedules/*", &api_schedules_wildcard_detail);

// reset endpoint
HTTP_RESOURCE_DEFINE(system_api_resource, tv_scheduler, "/api/system/reset",
                     &api_system_reset_detail);

// --- Public Start Function ---
int http_server_start_serving(void) {
  LOG_INF("Starting HTTP server on port %u...", http_port);

  // Initialize API config variables
  api_config_init();

  int ret = -1;
  uint32_t delay_ms = HTTP_SERVER_RETRY_DELAY_MS;

  for (int attempt = 1; attempt <= HTTP_SERVER_START_RETRIES; attempt++) {
    ret = http_server_start();

    if (ret == 0) {
      LOG_INF("HTTP server started successfully on attempt %d.", attempt);
      return 0; // Success! Exit early
    }

    // Handle failure: Print warning with error code (e.g. -12 for ENOMEM, or
    // -117 for EADDRINUSE)
    LOG_WRN("HTTP server start failed on attempt %d/%d (error: %d). Retrying "
            "in %u ms...",
            attempt, HTTP_SERVER_START_RETRIES, ret, delay_ms);

    // Sleep before the next attempt
    k_msleep(delay_ms);

    // Optional: Exponential backoff to avoid hammering the socket subsystem
    delay_ms *= 2;
  }

  // Fatal: All retries failed
  LOG_ERR("FATAL: Failed to start HTTP server after %d attempts.",
          HTTP_SERVER_START_RETRIES);
  return ret;
}

