#include "api_system.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(api_system, CONFIG_LOG_DEFAULT_LEVEL);

// --- Delayed Reboot Timer ---

// Timer callback that performs the cold reboot after the packet is sent
static void reboot_timer_handler(struct k_timer *timer_id) {
  LOG_WRN("Executing delayed reboot now!");
  sys_reboot(SYS_REBOOT_COLD);
}

// Define a one-shot kernel timer
K_TIMER_DEFINE(reboot_timer, reboot_timer_handler, NULL);

// --- API Callback for /api/system/reset ---

static int api_system_reset_callback(struct http_client_ctx *client,
                                     enum http_transaction_status status,
                                     const struct http_request_ctx *request_ctx,
                                     struct http_response_ctx *response_ctx,
                                     void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_POST) {
      LOG_INF("Received request to reboot device.");

      // 1. Set the response data to return immediately
      response_ctx->status = 200;
      response_ctx->final_chunk = true;

      // 2. Start a one-shot timer to reboot in 1 second.
      // This 1-second delay ensures the network layer finishes transmitting the
      // TCP frames.
      LOG_WRN("System will reboot in 1 second...");
      k_timer_start(&reboot_timer, K_SECONDS(1), K_NO_WAIT);

    } else {
      response_ctx->status = 405; // Method Not Allowed
      response_ctx->final_chunk = true;
    }
  }
  return 0;
}

// --- API Resource Definition ---

struct http_resource_detail_dynamic api_system_reset_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_POST),
        },
    .cb = api_system_reset_callback,
};
