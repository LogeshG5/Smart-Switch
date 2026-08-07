#include "api_config.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/data/json.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>

LOG_MODULE_REGISTER(api_config, CONFIG_LOG_DEFAULT_LEVEL);

// --- ISOLATED BUFFERS FOR CONCURRENT REQUEST SAFETY ---
#define SETTINGS_RX_BUF_SIZE 512
#define SETTINGS_TX_BUF_SIZE 512
static char settings_rx_buffer[SETTINGS_RX_BUF_SIZE];
static size_t settings_rx_length;
static char settings_tx_buffer[SETTINGS_TX_BUF_SIZE];

#define SCHEDULES_RX_BUF_SIZE 1024
#define SCHEDULES_TX_BUF_SIZE 1024
static char schedules_rx_buffer[SCHEDULES_RX_BUF_SIZE];
static size_t schedules_rx_length;
static char schedules_tx_buffer[SCHEDULES_TX_BUF_SIZE];

// ===================================================================================
// JSON Parsing Intermediate Payload Structs (CORRECTED FOR ZEPHYR JSON LIBRARY)
// ===================================================================================

/**

@brief Intermediate settings structure matching Zephyr JSON parsing
expectations.

Strings must be char pointers, and numbers must be standard 32-bit integers.
*/
struct settings_payload {
  const char *name;
  const char *wifi_ssid;
  const char *wifi_password;
  const char *timezone;
  int32_t max_on_time_minutes;
};

/**

@brief Intermediate schedule structure matching Zephyr JSON parsing
expectations.

Uses int32_t to prevent stack corruption when parsing JSON_TOK_NUMBER.
*/
struct schedule_payload {
  bool enabled;
  int32_t day_mask;
  int32_t start_hour;
  int32_t start_minute;
  int32_t end_hour;
  int32_t end_minute;
};

// ===================================================================================
// JSON Descriptors
// ===================================================================================

static const struct json_obj_descr settings_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct settings_payload, name, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct settings_payload, wifi_ssid, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct settings_payload, wifi_password,
                        JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct settings_payload, timezone, JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct settings_payload, max_on_time_minutes,
                        JSON_TOK_NUMBER),
};

static const struct json_obj_descr schedule_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct schedule_payload, enabled, JSON_TOK_TRUE),
    JSON_OBJ_DESCR_PRIM(struct schedule_payload, day_mask, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct schedule_payload, start_hour, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct schedule_payload, start_minute, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct schedule_payload, end_hour, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct schedule_payload, end_minute, JSON_TOK_NUMBER),
};

// ===================================================================================
// PART 1: /api/settings IMPLEMENTATION
// ===================================================================================

static int api_settings_get(struct http_response_ctx *response_ctx) {
  const app_config_t *config = config_get();

  int len =
      snprintk(settings_tx_buffer, sizeof(settings_tx_buffer),
               "{\"name\":\"%s\",\"wifi_ssid\":\"%s\",\"wifi_password\":\"%s\","
               "\"timezone\":\"%s\",\"max_on_time_minutes\":%u}",
               config->name, config->wifi_ssid, config->wifi_password,
               config->timezone, config->max_on_time_minutes);

  if (len > 0 && len < sizeof(settings_tx_buffer)) {
    response_ctx->status = 200;
    response_ctx->body = (uint8_t *)settings_tx_buffer;
    response_ctx->body_len = len;
  } else {
    LOG_ERR("Failed to serialize settings JSON, buffer too small?");
    response_ctx->status = 500;
  }
  return 0;
}

/**

@brief Handles POST /api/settings using the Zephyr JSON library safely.
*/
static int api_settings_post(struct http_response_ctx *response_ctx) {
  app_config_t *target_config = config_get_editable();
  struct settings_payload parsed = {0};

  // Parse safely into intermediate payload struct
  int ret = json_obj_parse(settings_rx_buffer, settings_rx_length,
                           settings_descr, ARRAY_SIZE(settings_descr), &parsed);
  if (ret < 0) {
    LOG_ERR("JSON parser error for settings: %d", ret);
    response_ctx->status = 400; // Bad Request
    return ret;
  }

  // Safely copy values from the payload into the persistent config struct
  if (parsed.name) {
    strncpy(target_config->name, parsed.name, sizeof(target_config->name) - 1);
    target_config->name[sizeof(target_config->name) - 1] = '\0';
  }
  if (parsed.wifi_ssid) {
    strncpy(target_config->wifi_ssid, parsed.wifi_ssid,
            sizeof(target_config->wifi_ssid) - 1);
    target_config->wifi_ssid[sizeof(target_config->wifi_ssid) - 1] = '\0';
  }
  if (parsed.wifi_password) {
    strncpy(target_config->wifi_password, parsed.wifi_password,
            sizeof(target_config->wifi_password) - 1);
    target_config->wifi_password[sizeof(target_config->wifi_password) - 1] =
        '\0';
  }
  if (parsed.timezone) {
    strncpy(target_config->timezone, parsed.timezone,
            sizeof(target_config->timezone) - 1);
    target_config->timezone[sizeof(target_config->timezone) - 1] = '\0';
  }

  target_config->max_on_time_minutes = (uint16_t)parsed.max_on_time_minutes;

  target_config->version = CONFIG_VERSION;
  config_save();
  response_ctx->status = 200;
  return 0;
}

static int api_settings_callback(struct http_client_ctx *client,
                                 enum http_transaction_status status,
                                 const struct http_request_ctx *request_ctx,
                                 struct http_response_ctx *response_ctx,
                                 void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_GET) {
      api_settings_get(response_ctx);
    } else if (client->method == HTTP_POST) {
      if (request_ctx->data_len > 0) {
        memcpy(&settings_rx_buffer[settings_rx_length], request_ctx->data,
               request_ctx->data_len);
        settings_rx_length += request_ctx->data_len;
      }
      settings_rx_buffer[settings_rx_length] = '\0';
      api_settings_post(response_ctx);
    } else {
      response_ctx->status = 405; // Method Not Allowed
    }
    response_ctx->final_chunk = true;
    settings_rx_length = 0; // Reset isolated buffer
  } else if (status == HTTP_SERVER_REQUEST_DATA_MORE) {
    if (settings_rx_length + request_ctx->data_len < SETTINGS_RX_BUF_SIZE) {
      memcpy(&settings_rx_buffer[settings_rx_length], request_ctx->data,
             request_ctx->data_len);
      settings_rx_length += request_ctx->data_len;
      settings_rx_buffer[settings_rx_length] = '\0';
    } else {
      LOG_ERR("Settings POST payload too large for buffer!");
      response_ctx->status = 413;
      response_ctx->final_chunk = true;
      settings_rx_length = 0;
      return -ENOMEM;
    }
  }
  return 0;
}

// ===================================================================================
// PART 2: /api/schedules IMPLEMENTATION
// ===================================================================================

/**

@brief Parses a single schedule object using the Zephyr JSON library.
*/
static int parse_schedule(const char *json, size_t len, schedule_t *sched) {
  struct schedule_payload payload = {0};

  int ret = json_obj_parse((char *)json, len, schedule_descr,
                           ARRAY_SIZE(schedule_descr), &payload);
  if (ret < 0) {
    LOG_ERR("JSON parser error for schedule: %d", ret);
    return ret;
  }

  // Safely transfer parsed 32-bit values down to 8-bit variables in schedule_t
  sched->enabled = payload.enabled;
  sched->day_mask = (uint8_t)payload.day_mask;
  sched->start_hour = (uint8_t)payload.start_hour;
  sched->start_minute = (uint8_t)payload.start_minute;
  sched->end_hour = (uint8_t)payload.end_hour;
  sched->end_minute = (uint8_t)payload.end_minute;

  return 0;
}

static int get_index_from_url(const char *url) {
  const char *base = "/api/schedulesupd/";
  if (strncmp(url, base, strlen(base)) != 0) {
    return -1;
  }
  const char *index_str = url + strlen(base);
  // Check if the URL is longer than the base and the character after the base
  // is a digit
  if (strlen(url) > strlen(base) && *index_str >= '0' && *index_str <= '9') {
    return atoi(index_str);
  }
  return -1;
}

static int api_schedules_get_all(struct http_response_ctx *response_ctx) {
  const app_config_t *config = config_get();
  int pos = 0;

  pos += snprintk(schedules_tx_buffer + pos, sizeof(schedules_tx_buffer) - pos,
                  "[");
  for (int i = 0; i < config->schedule_count; i++) {
    const schedule_t *s = &config->schedules[i];
    pos +=
        snprintk(schedules_tx_buffer + pos, sizeof(schedules_tx_buffer) - pos,
                 "%s{\"enabled\":%s,\"day_mask\":%u,\"start_hour\":%u,\"start_"
                 "minute\":%u,"
                 "\"end_hour\":%u,\"end_minute\":%u}",
                 (i > 0) ? "," : "", s->enabled ? "true" : "false", s->day_mask,
                 s->start_hour, s->start_minute, s->end_hour, s->end_minute);
  }
  pos += snprintk(schedules_tx_buffer + pos, sizeof(schedules_tx_buffer) - pos,
                  "]");

  if (pos > 0 && pos < sizeof(schedules_tx_buffer)) {
    response_ctx->status = 200;
    response_ctx->body = (uint8_t *)schedules_tx_buffer;
    response_ctx->body_len = pos;
  } else {
    LOG_ERR("Failed to serialize schedules JSON, buffer too small?");
    response_ctx->status = 500;
  }
  return 0;
}

static int api_schedules_post(struct http_response_ctx *response_ctx) {
  app_config_t *config = config_get_editable();
  if (config->schedule_count >= MAX_SCHEDULES) {
    response_ctx->status = 413;
    return 0;
  }
  LOG_DBG("POST schedules payload: %s", schedules_rx_buffer);
  if (parse_schedule(schedules_rx_buffer, schedules_rx_length,
                     &config->schedules[config->schedule_count]) != 0) {
    response_ctx->status = 400; // Bad Request from parser
    return -EINVAL;
  }

  config->schedule_count++;
  config_save();
  response_ctx->status = 201;
  return 0;
}

static int api_schedules_put(int index,
                             struct http_response_ctx *response_ctx) {
  app_config_t *config = config_get_editable();
  if (index < 0 || index >= config->schedule_count) {
    response_ctx->status = 404;
    return 0;
  }
  LOG_DBG("PUT schedule %d payload: %s", index, schedules_rx_buffer);
  if (parse_schedule(schedules_rx_buffer, schedules_rx_length,
                     &config->schedules[index]) != 0) {
    response_ctx->status = 400; // Bad Request from parser
    return -EINVAL;
  }

  config_save();
  response_ctx->status = 200;
  return 0;
}

static int api_schedules_delete(int index,
                                struct http_response_ctx *response_ctx) {
  app_config_t *config = config_get_editable();
  if (index < 0 || index >= config->schedule_count) {
    response_ctx->status = 404;
    return 0;
  }

  int remaining = config->schedule_count - index - 1;
  if (remaining > 0) {
    memmove(&config->schedules[index], &config->schedules[index + 1],
            remaining * sizeof(schedule_t));
  }
  config->schedule_count--;
  config_save();
  response_ctx->status = 200;
  return 0;
}

static int api_schedules_callback(struct http_client_ctx *client,
                                  enum http_transaction_status status,
                                  const struct http_request_ctx *request_ctx,
                                  struct http_response_ctx *response_ctx,
                                  void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    int index = get_index_from_url(client->url_buffer);
    LOG_DBG("Schedules API: Method=%d, URL='%s', Index=%d", client->method,
            client->url_buffer, index);

    switch (client->method) {
    case HTTP_GET:
      api_schedules_get_all(response_ctx);
      break;
    case HTTP_POST:
      if (request_ctx->data_len > 0) {
        memcpy(&schedules_rx_buffer[schedules_rx_length], request_ctx->data,
               request_ctx->data_len);
        schedules_rx_length += request_ctx->data_len;
      }
      schedules_rx_buffer[schedules_rx_length] = '\0';
      api_schedules_post(response_ctx);
      break;
    case HTTP_PUT:
      if (request_ctx->data_len > 0) {
        memcpy(&schedules_rx_buffer[schedules_rx_length], request_ctx->data,
               request_ctx->data_len);
        schedules_rx_length += request_ctx->data_len;
      }
      schedules_rx_buffer[schedules_rx_length] = '\0';
      api_schedules_put(index, response_ctx);
      break;
    case HTTP_DELETE:
      api_schedules_delete(index, response_ctx);
      break;
    default:
      response_ctx->status = 405;
      break;
    }
    response_ctx->final_chunk = true;
    schedules_rx_length = 0; // Reset isolated buffer
  } else if (status == HTTP_SERVER_REQUEST_DATA_MORE) {
    if (schedules_rx_length + request_ctx->data_len < SCHEDULES_RX_BUF_SIZE) {
      memcpy(&schedules_rx_buffer[schedules_rx_length], request_ctx->data,
             request_ctx->data_len);
      schedules_rx_length += request_ctx->data_len;
      schedules_rx_buffer[schedules_rx_length] = '\0';
    } else {
      LOG_ERR("Schedules PUT/POST payload too large for buffer!");
      response_ctx->status = 413;
      response_ctx->final_chunk = true;
      schedules_rx_length = 0;
      return -ENOMEM;
    }
  }
  return 0;
}

// --- API RESOURCE DEFINITIONS ---
struct http_resource_detail_dynamic api_settings_detail = {
    .common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
               .bitmask_of_supported_http_methods =
                   BIT(HTTP_GET) | BIT(HTTP_POST)},
    .cb = api_settings_callback,
};

struct http_resource_detail_dynamic api_schedules_detail = {
    .common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
               .bitmask_of_supported_http_methods =
                   BIT(HTTP_GET) | BIT(HTTP_POST)},
    .cb = api_schedules_callback,
};

struct http_resource_detail_dynamic api_schedules_wildcard_detail = {
    .common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
               .bitmask_of_supported_http_methods =
                   BIT(HTTP_PUT) | BIT(HTTP_DELETE)},
    .cb = api_schedules_callback,
};

void api_config_init(void) {
  settings_rx_length = 0;
  schedules_rx_length = 0;
}
