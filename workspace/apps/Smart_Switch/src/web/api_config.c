#include "api_config.h"
#include "config.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// --- JSON Parsing Helpers (Unchanged, but now used with isolated buffers) ---
static bool json_get_string(const char *json, const char *key, char *out,
                            size_t out_len) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  char *start = strstr(json, pattern);
  if (!start)
    return false;
  start += strlen(pattern);
  char *end = strchr(start, '"');
  if (!end)
    return false;
  size_t length = end - start;
  if (length >= out_len)
    length = out_len - 1;
  memcpy(out, start, length);
  out[length] = '\0';
  return true;
}

static bool json_get_int(const char *json, const char *key, int *value) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  char *p = strstr(json, pattern);
  if (!p)
    return false;
  p += strlen(pattern);
  *value = atoi(p);
  return true;
}

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

static int api_settings_post(struct http_response_ctx *response_ctx) {
  app_config_t *config = config_get_editable();
  int val;
  LOG_DBG("POST settings payload: %s", settings_rx_buffer);

  config->version = CONFIG_VERSION;
  json_get_string(settings_rx_buffer, "name", config->name,
                  sizeof(config->name));
  json_get_string(settings_rx_buffer, "wifi_ssid", config->wifi_ssid,
                  sizeof(config->wifi_ssid));
  json_get_string(settings_rx_buffer, "wifi_password", config->wifi_password,
                  sizeof(config->wifi_password));
  json_get_string(settings_rx_buffer, "timezone", config->timezone,
                  sizeof(config->timezone));
  if (json_get_int(settings_rx_buffer, "max_on_time_minutes", &val)) {
    config->max_on_time_minutes = (uint16_t)val;
  }
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
      memcpy(&settings_rx_buffer[settings_rx_length], request_ctx->data,
             request_ctx->data_len);
      settings_rx_length += request_ctx->data_len;
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

static void parse_schedule(const char *json, schedule_t *sched) {
  int val;
  if (strstr(json, "\"enabled\":true"))
    sched->enabled = true;
  else
    sched->enabled = false;
  if (json_get_int(json, "day_mask", &val))
    sched->day_mask = (uint8_t)val;
  if (json_get_int(json, "start_hour", &val))
    sched->start_hour = (uint8_t)val;
  if (json_get_int(json, "start_minute", &val))
    sched->start_minute = (uint8_t)val;
  if (json_get_int(json, "end_hour", &val))
    sched->end_hour = (uint8_t)val;
  if (json_get_int(json, "end_minute", &val))
    sched->end_minute = (uint8_t)val;
}

static int get_index_from_url(const char *url) {
  const char *base = "/api/schedules/";
  const char *index_str = url + strlen(base);
  // Check if the URL is longer than the base and the character after the base
  // is a digit
  if (strlen(url) > strlen(base) && *index_str >= '0' && *index_str <= '9') {
    return atoi(index_str);
  }
  return -1; // No valid index found
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
  parse_schedule(schedules_rx_buffer,
                 &config->schedules[config->schedule_count]);
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
  parse_schedule(schedules_rx_buffer, &config->schedules[index]);
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
      memcpy(&schedules_rx_buffer[schedules_rx_length], request_ctx->data,
             request_ctx->data_len);
      schedules_rx_length += request_ctx->data_len;
      schedules_rx_buffer[schedules_rx_length] = '\0';
      api_schedules_post(response_ctx);
      break;
    case HTTP_PUT:
      memcpy(&schedules_rx_buffer[schedules_rx_length], request_ctx->data,
             request_ctx->data_len);
      schedules_rx_length += request_ctx->data_len;
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
    .cb =
        api_schedules_callback, // Both point to the same, intelligent callback
};

void api_config_init(void) {
  settings_rx_length = 0;
  schedules_rx_length = 0;
}
