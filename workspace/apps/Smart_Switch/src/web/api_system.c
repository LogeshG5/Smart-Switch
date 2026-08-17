#include "api_system.h"
#include "relay.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(api_system, CONFIG_LOG_DEFAULT_LEVEL);

// --- ISOLATED RX BUFFER FOR CONCURRENT API SAFETY ---
#define CONTROL_RX_BUF_SIZE 256
static char control_rx_buffer[CONTROL_RX_BUF_SIZE];
static size_t control_rx_length;

#define RELAY_STATE_BUF_SIZE 128
static char relay_state_buffer[RELAY_STATE_BUF_SIZE];

// --- OVERRIDE STATE VARIABLES ---
static bool override_active = false;
static bool override_state =
    false; // true = Block (Relay ON), false = Allow (Relay OFF)
static int64_t temp_allow_expiry_ms = 0; // Expiry uptime stamp in milliseconds

// --- JSON Parsing Intermediate Payload Struct ---
struct control_payload {
  const char *action;
  int32_t duration_minutes;
};

// --- JSON Descriptor matching the incoming control payload ---
static const struct json_obj_descr control_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct control_payload, action, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct control_payload, duration_minutes,
                        JSON_TOK_NUMBER),
};

// --- Delayed Reboot Timer ---
static void reboot_timer_handler(struct k_timer *timer_id) {
  LOG_WRN("Executing delayed reboot now!");
  sys_reboot(SYS_REBOOT_COLD);
}

K_TIMER_DEFINE(reboot_timer, reboot_timer_handler, NULL);

// ===================================================================================
// PUBLIC API FOR SCHEDULER & MAIN INTEGRATION
// ===================================================================================

/**

@brief Checks if a manual or temporary override is currently active.

@param state Pointer to write the active override state to (true = Block, false
= Allow).

@return true if an override is active, false if the system should follow the
normal scheduler.
*/
bool api_system_get_override(bool *state) {
  if (temp_allow_expiry_ms > 0) {
    if (k_uptime_get() < temp_allow_expiry_ms) {
      *state = false; // Forced ALLOW state (Relay OFF)
      return true;
    } else {
      // Temporary allow period has expired
      LOG_INF("Temporary allow countdown has expired. Returning control to "
              "scheduler.");
      temp_allow_expiry_ms = 0;
      override_active = false;
    }
  }

  if (override_active) {
    *state = override_state;
    return true;
  }

  return false;
}

// ===================================================================================
// API CALLBACKS
// ===================================================================================

static int api_system_reset_callback(struct http_client_ctx *client,
                                     enum http_transaction_status status,
                                     const struct http_request_ctx *request_ctx,
                                     struct http_response_ctx *response_ctx,
                                     void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_POST) {
      LOG_INF("Received request to reboot device.");
      response_ctx->status = 200;
      response_ctx->final_chunk = true;

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
      // Simply serialize and return the immediate current relay output state
      int len = snprintf(relay_state_buffer, sizeof(relay_state_buffer),
                         "{\"state\":%u}", relay_is_on());
      if (len > 0 && len < sizeof(relay_state_buffer)) {
        response_ctx->status = 200;
        response_ctx->body = (uint8_t *)relay_state_buffer;
        response_ctx->body_len = len;
      } else {
        LOG_ERR("Failed to serialize relay state JSON.");
        response_ctx->status = 500;
      }
    } else {
      response_ctx->status = 405; // Method Not Allowed
    }
    response_ctx->final_chunk = true;
  }
  return 0;
}

/**

@brief Handles manual relay actions and temporary override durations safely.
*/
static int api_system_control_post(struct http_response_ctx *response_ctx) {
  struct control_payload payload = {0};

  int ret = json_obj_parse(control_rx_buffer, control_rx_length, control_descr,
                           ARRAY_SIZE(control_descr), &payload);
  if (ret < 0) {
    LOG_ERR("JSON parse failure in control endpoint: %d", ret);
    response_ctx->status = 400; // Bad Request
    return ret;
  }

  if (!payload.action) {
    LOG_ERR("Missing action property in control JSON.");
    response_ctx->status = 400;
    return -EINVAL;
  }

  if (strcmp(payload.action, "on") == 0) {
    // Manual Forced Block (Relay ON)
    override_active = true;
    override_state = true;
    temp_allow_expiry_ms = 0; // Clear any active countdown timers
    relay_set(true);          // Actuate GPIO output instantly
    LOG_INF("Manual override activated: FORCED BLOCK (Relay ON).");

  } else if (strcmp(payload.action, "off") == 0) {
    // Manual Forced Allow (Relay OFF)
    override_active = true;
    override_state = false;
    temp_allow_expiry_ms = 0; // Clear any active countdown timers
    relay_set(false);         // Actuate GPIO output instantly
    LOG_INF("Manual override activated: FORCED ALLOW (Relay OFF).");

  } else if (strcmp(payload.action, "temporary_allow") == 0) {
    if (payload.duration_minutes <= 0) {
      LOG_ERR("Invalid duration minutes payload: %d", payload.duration_minutes);
      response_ctx->status = 400;
      return -EINVAL;
    }

    // Set the countdown uptime clock boundary
    temp_allow_expiry_ms =
        k_uptime_get() + (payload.duration_minutes * 60 * 1000);
    override_active = true;
    override_state = false;
    relay_set(false); // Actuate GPIO output instantly to unblock appliance
    LOG_INF("Temporary Allow activated for %d minute(s). Expiry at %lld ms.",
            payload.duration_minutes, temp_allow_expiry_ms);

  } else if (strcmp(payload.action, "auto") == 0) {
    // Bonus action to release overrides back to scheduled rules
    override_active = false;
    temp_allow_expiry_ms = 0;
    LOG_INF("All manual overrides cleared. System returned to auto schedule.");

  } else {
    LOG_ERR("Unsupported action parameter: %s", payload.action);
    response_ctx->status = 400;
    return -EINVAL;
  }

  response_ctx->status = 200;
  return 0;
}

static int api_system_control_callback(
    struct http_client_ctx *client, enum http_transaction_status status,
    const struct http_request_ctx *request_ctx,
    struct http_response_ctx *response_ctx, void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_POST) {
      if (request_ctx->data_len > 0) {
        memcpy(&control_rx_buffer[control_rx_length], request_ctx->data,
               request_ctx->data_len);
        control_rx_length += request_ctx->data_len;
      }
      control_rx_buffer[control_rx_length] = '\0';
      api_system_control_post(response_ctx);
    } else {
      response_ctx->status = 405; // Method Not Allowed
    }
    response_ctx->final_chunk = true;
    control_rx_length = 0; // Reset isolated buffer
  } else if (status == HTTP_SERVER_REQUEST_DATA_MORE) {
    if (control_rx_length + request_ctx->data_len < CONTROL_RX_BUF_SIZE) {
      memcpy(&control_rx_buffer[control_rx_length], request_ctx->data,
             request_ctx->data_len);
      control_rx_length += request_ctx->data_len;
      control_rx_buffer[control_rx_length] = '\0';
    } else {
      LOG_ERR("Control POST payload too large for buffer!");
      response_ctx->status = 413; // Payload Too Large
      response_ctx->final_chunk = true;
      control_rx_length = 0;
      return -ENOMEM;
    }
  }
  return 0;
}

// ===================================================================================
// API RESOURCE DEFINITIONS
// ===================================================================================

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

struct http_resource_detail_dynamic api_system_control_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_POST),
        },
    .cb = api_system_control_callback,
};
