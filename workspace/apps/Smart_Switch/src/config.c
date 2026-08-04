#include "config.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(config_storage, CONFIG_LOG_DEFAULT_LEVEL);

// The single, global instance of our application's configuration.
static app_config_t config;

// Guard to ensure settings subsystem is initialized only once.
static bool is_initialized = false;

// --- Settings Subsystem Handlers ---

static int app_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg) {
  const char *next;
  if (settings_name_steq(name, "config", &next) && !next) {
    if (len != sizeof(app_config_t)) {
      LOG_ERR(
          "Config size mismatch! Flash: %d, RAM: %d. Ignoring stored settings.",
          (int)len, sizeof(app_config_t));
      return -EINVAL;
    }

    app_config_t loaded_config;
    int rc = read_cb(cb_arg, &loaded_config, sizeof(loaded_config));
    if (rc < 0) {
      LOG_ERR("Failed to read config from flash (err: %d)", rc);
      return rc;
    }

    // CRITICAL: Check version of loaded config against firmware version.
    if (loaded_config.version != CONFIG_VERSION) {
      LOG_WRN("Config version mismatch! Flash: %u, FW: %u. Applying factory "
              "defaults.",
              loaded_config.version, CONFIG_VERSION);
      return -EINVAL; // Mismatch triggers a factory reset in config_load.
    }

    // Versions match, apply the loaded configuration.
    memcpy(&config, &loaded_config, sizeof(app_config_t));
    LOG_INF("Configuration loaded successfully from Flash (version %u).",
            config.version);
    return 0;
  }
  return -ENOENT;
}

static int app_settings_export(int (*storage_func)(const char *name,
                                                   const void *value,
                                                   size_t val_len)) {
  return storage_func("app/config", &config, sizeof(app_config_t));
}

static struct settings_handler app_conf_handler = {
    .name = "app",
    .h_set = app_settings_set,
    .h_export = app_settings_export,
};

// --- Public API ---

void config_factory_reset(void) {
  LOG_WRN("Performing factory reset. All settings will be lost.");
  memset(&config, 0, sizeof(config));
  config.version = CONFIG_VERSION;
  strncpy(config.name, "My New IoT Device", sizeof(config.name) - 1);
  strncpy(config.timezone, "UTC", sizeof(config.timezone) - 1);
  config.max_on_time_minutes = 0;
  config.schedule_count = 1;
  schedule_t *schedule = &config.schedules[0];
  schedule->enabled = true;
  schedule->day_mask = 62; // Mon - Fri
  schedule->start_hour = 9;
  schedule->start_minute = 0;
  schedule->end_hour = 17;
  schedule->end_minute = 0;
}

void config_load(void) {
  if (!is_initialized) {
    int rc = settings_subsys_init();
    if (rc != 0) {
      LOG_ERR("Settings subsystem initialization failed (err: %d). Loading "
              "defaults.",
              rc);
      config_factory_reset();
      return;
    }
    rc = settings_register(&app_conf_handler);
    if (rc != 0) {
      LOG_ERR("Failed to register settings handler (err: %d).", rc);
    }
    is_initialized = true;
  }

  // Attempt to load settings from persistent storage.
  int rc = settings_load();
  if (rc != 0) {
    LOG_WRN("No saved config found, or load failed/rejected (err: %d). Loading "
            "defaults.",
            rc);
    config_factory_reset();
    // Save the fresh factory defaults to flash immediately.
    config_save();
  }
}

int config_save(void) {
  LOG_INF("Attempting to save configuration to flash...");
  config_dump(); // Log what we're about to save.

  int rc = settings_save();
  if (rc != 0) {
    LOG_ERR("Failed to save configuration to Flash (err: %d)", rc);
  } else {
    LOG_INF("Configuration successfully saved to Flash.");
  }
  return rc;
}

// --- Debugging & Getters ---

static void print_byte_as_binary(uint8_t byte) {
  LOG_MODULE_DECLARE(config_storage,
                     CONFIG_LOG_DEFAULT_LEVEL); // Ensure LOG_* can be used here
  char bin_str[11] = "0b";                      // 0b + 8 bits + null terminator
  for (int i = 7; i >= 0; i--) {
    bin_str[9 - i] = ((byte >> i) & 1) ? '1' : '0';
  }
  printk("%s",
         bin_str); // Use printk for inline printing without extra log headers
}

void config_dump(void) {
  LOG_INF("--- CONFIGURATION DUMP ---");
  LOG_INF("Version: %u", config.version);
  LOG_INF("-- Device Settings --");
  LOG_INF("Name: '%s'", config.name);
  LOG_INF("WiFi SSID: '%s'", config.wifi_ssid);
  // SECURITY: Do not log the cleartext password.
  LOG_INF("WiFi Password: '[****]'");
  LOG_INF("Timezone: '%s'", config.timezone);
  LOG_INF("Max On-Time (minutes): %u", config.max_on_time_minutes);
  LOG_INF("-- Schedules (%u) --", config.schedule_count);

  if (config.schedule_count == 0) {
    LOG_INF("No schedules defined.");
  } else {
    for (int i = 0; i < config.schedule_count; i++) {
      const schedule_t *s = &config.schedules[i];
      char buf[128];
      int pos = 0;
      pos += snprintf(buf + pos, sizeof(buf) - pos, "--- Schedule %d ---\n",
                      i + 1);
      pos += snprintf(buf + pos, sizeof(buf) - pos,
                      "  Enabled: %s | Day Mask: %u (",
                      s->enabled ? "true" : "false", s->day_mask);
      printk("%s", buf); // Print buffer before binary part
      print_byte_as_binary(s->day_mask);
      printk(") | Time: %02u:%02u - %02u:%02u\n", s->start_hour,
             s->start_minute, s->end_hour, s->end_minute);
    }
  }
  LOG_INF("--- END OF DUMP ---");
}

const app_config_t *config_get(void) { return &config; }

app_config_t *config_get_editable(void) { return &config; }
