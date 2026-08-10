#include "api_system.h"
#include "relay.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(api_system, CONFIG_LOG_DEFAULT_LEVEL);

#define RELAY_STATE_BUF_SIZE 512
static char relay_state_buffer[RELAY_STATE_BUF_SIZE];

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

static int api_relay_state_callback(struct http_client_ctx *client,
                                    enum http_transaction_status status,
                                    const struct http_request_ctx *request_ctx,
                                    struct http_response_ctx *response_ctx,
                                    void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_GET) {
      int len = snprintf(relay_state_buffer, sizeof(relay_state_buffer),
                         "{\"state\":%u}", relay_is_on());

      if (len > 0 && len < sizeof(relay_state_buffer)) {
        response_ctx->status = 200;
        response_ctx->body = (uint8_t *)relay_state_buffer;
        response_ctx->body_len = len;
      } else {
        LOG_ERR("Failed to serialize settings JSON, buffer too small?");
        response_ctx->status = 500;
      }
    } else {
      response_ctx->status = 405; // Method Not Allowed
    }
    response_ctx->final_chunk = true;
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

struct http_resource_detail_dynamic api_relay_state = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        },
    .cb = api_relay_state_callback,
};

