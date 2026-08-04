#include "api_ota.h"
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(api_ota, CONFIG_LOG_DEFAULT_LEVEL);

static struct flash_img_context ota_ctx;
static bool ota_in_progress = false;

// Delayed Warm Reboot Timer
static void ota_reboot_handler(struct k_timer *timer_id) {
  LOG_WRN("Rebooting to swap active images...");
  sys_reboot(SYS_REBOOT_COLD);
}

K_TIMER_DEFINE(ota_reboot_timer, ota_reboot_handler, NULL);

// 1. Start Session Callback
static int api_ota_start_cb(struct http_client_ctx *client,
                            enum http_transaction_status status,
                            const struct http_request_ctx *request_ctx,
                            struct http_response_ctx *response_ctx,
                            void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_POST) {
      LOG_INF("Starting OTA flash session...");

      int rc = flash_img_init(&ota_ctx);
      if (rc < 0) {
        LOG_ERR("Failed to initialize flash image writer: %d", rc);
        response_ctx->status = 500;
        response_ctx->final_chunk = true;
        return rc;
      }

      ota_in_progress = true;
      response_ctx->status = 200;
      response_ctx->final_chunk = true;
    } else {
      response_ctx->status = 405;
      response_ctx->final_chunk = true;
    }
  }
  return 0;
}

// 2. Stream Data Chunks Callback
static int api_ota_upload_cb(struct http_client_ctx *client,
                             enum http_transaction_status status,
                             const struct http_request_ctx *request_ctx,
                             struct http_response_ctx *response_ctx,
                             void *user_data) {
  if (!ota_in_progress) {
    response_ctx->status = 400; // Bad Request (Session not started)
    response_ctx->final_chunk = true;
    return 0;
  }

  if (status == HTTP_SERVER_REQUEST_DATA_FINAL ||
      status == HTTP_SERVER_REQUEST_DATA_MORE) {
    if (request_ctx->data_len > 0) {
      // Write chunk to Flash Slot 1
      int rc = flash_img_buffered_write(&ota_ctx, request_ctx->data,
                                        request_ctx->data_len, false);
      if (rc < 0) {
        LOG_ERR("Failed to write binary chunk to flash: %d", rc);
        ota_in_progress = false;
        response_ctx->status = 500;
        response_ctx->final_chunk = true;
        return rc;
      }
    }
  }

  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    response_ctx->status = 200;
    response_ctx->final_chunk = true;
  }
  return 0;
}

// 3. Finalize & Reboot Callback
static int api_ota_finish_cb(struct http_client_ctx *client,
                             enum http_transaction_status status,
                             const struct http_request_ctx *request_ctx,
                             struct http_response_ctx *response_ctx,
                             void *user_data) {
  if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
    if (client->method == HTTP_POST) {
      LOG_INF("Finalizing OTA firmware update...");

      // Complete the write (flushes any remaining buffer to flash)
      int rc = flash_img_buffered_write(&ota_ctx, NULL, 0, true);
      if (rc < 0) {
        LOG_ERR("Failed to finalize flash write: %d", rc);
        response_ctx->status = 500;
        response_ctx->final_chunk = true;
        return rc;
      }

      // Tell MCUboot that Slot 1 contains a valid, pending update image
      rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
      if (rc < 0) {
        LOG_ERR("Failed to request MCUboot upgrade: %d", rc);
        response_ctx->status = 500;
        response_ctx->final_chunk = true;
        return rc;
      }

      LOG_INF("Upgrade marked successfully. Rebooting in 3s...");
      response_ctx->status = 200;
      response_ctx->final_chunk = true;
      ota_in_progress = false;

      // Trigger warm system reboot in 3 seconds
      k_timer_start(&ota_reboot_timer, K_SECONDS(3), K_NO_WAIT);
    } else {
      response_ctx->status = 405;
      response_ctx->final_chunk = true;
    }
  }
  return 0;
}

// --- Endpoints Detail Definitions ---

struct http_resource_detail_dynamic api_ota_start_detail = {
    .common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
               .bitmask_of_supported_http_methods = BIT(HTTP_POST)},
    .cb = api_ota_start_cb,
};

struct http_resource_detail_dynamic api_ota_upload_detail = {
    .common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
               .bitmask_of_supported_http_methods = BIT(HTTP_POST)},
    .cb = api_ota_upload_cb,
};

struct http_resource_detail_dynamic api_ota_finish_detail = {
    .common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
               .bitmask_of_supported_http_methods = BIT(HTTP_POST)},
    .cb = api_ota_finish_cb,
};
