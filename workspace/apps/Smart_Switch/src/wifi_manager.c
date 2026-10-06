#include "wifi_manager.h"
#include "config.h"
#include "wifi.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(wifi_manager, CONFIG_LOG_DEFAULT_LEVEL);

// --- Configuration ---
#define AP_SSID "TV-Scheduler"
#define AP_PASSWORD "tvscheduler"
#define AP_IP_ADDRESS "192.168.4.1"
#define AP_NETMASK "255.255.255.0"
#define AP_START_TIMEOUT K_SECONDS(10)
#define CONFIG_WIFI_CONNECT_RETRIES 5

// --- State Management ---
static struct net_if *ap_iface;
static struct net_if *sta_iface;
static struct net_mgmt_event_callback mgmt_cb;
static bool is_ap_mode = false;
static K_SEM_DEFINE(sem_ap_started, 0, 1);

// --- Event Handler ---
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t event, struct net_if *iface) {
  switch (event) {
  case NET_EVENT_WIFI_AP_ENABLE_RESULT:
    LOG_INF("Access Point enabled.");
    is_ap_mode = true;
    k_sem_give(&sem_ap_started);
    break;
  case NET_EVENT_WIFI_AP_DISABLE_RESULT:
    LOG_INF("Access Point disabled.");
    is_ap_mode = false;
    k_sem_reset(&sem_ap_started);
    break;
  default:
    break;
  }
}

// --- Internal Helper Functions ---

/**
 * @brief Configures the static IP and netmask for the AP interface. (CORRECTED)
 */
static int configure_ap_network(void) {
  struct net_in_addr addr, netmask;

  if (net_addr_pton(NET_AF_INET, AP_IP_ADDRESS, &addr) != 0) {
    LOG_ERR("Invalid AP IP address: %s", AP_IP_ADDRESS);
    return -EINVAL;
  }
  if (net_addr_pton(NET_AF_INET, AP_NETMASK, &netmask) != 0) {
    LOG_ERR("Invalid AP netmask: %s", AP_NETMASK);
    return -EINVAL;
  }

  if (net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
    LOG_ERR("Failed to add AP IP address.");
    return -EIO;
  }

  net_if_ipv4_set_gw(ap_iface, &addr);

  if (!net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask)) {
    LOG_ERR("Failed to set AP netmask.");
    return -EIO;
  }

  LOG_INF("AP network configured: IP=%s", AP_IP_ADDRESS);
  return 0;
}

static int start_dhcp_server(void) {
  struct net_in_addr pool_start;
  if (net_addr_pton(NET_AF_INET, "192.168.4.10", &pool_start) != 0) {
    LOG_ERR("Invalid DHCP pool start address.");
    return -EINVAL;
  }
  int ret = net_dhcpv4_server_start(ap_iface, &pool_start);
  if (ret != 0) {
    LOG_ERR("Failed to start DHCPv4 server (error: %d)", ret);
  } else {
    LOG_INF("DHCPv4 server started successfully.");
  }
  return ret;
}

int wifi_connect_with_retires(const char *wifi_ssid,
                              const char *wifi_password) {
  LOG_INF("Found Wi-Fi credentials for SSID: '%s'. Attempting to connect...",
          wifi_ssid);

  for (int retry = 0; retry < CONFIG_WIFI_CONNECT_RETRIES; retry++) {
    LOG_INF("Wi-Fi connection attempt %d/%d", retry,
            CONFIG_WIFI_CONNECT_RETRIES);

    if (wifi_connect(wifi_ssid, wifi_password) == 0) {
      LOG_INF("Wi-Fi connected successfully.");
      return 0;
    }

    LOG_WRN("Wi-Fi connection attempt %d failed.", retry);
    LOG_INF("Retrying in 60 seconds...");
    k_sleep(K_SECONDS(60));
  }

  LOG_ERR(
      "Failed to connect to Wi-Fi after %d attempts. Falling back to AP mode.",
      CONFIG_WIFI_CONNECT_RETRIES);

  return -1;
}

// --- Public API ---

int wifi_manager_start_ap(void) {
  LOG_INF("Starting device in Access Point mode...");
  if (ap_iface == NULL) {
    LOG_ERR("AP network interface is not available.");
    return -ENODEV;
  }
  struct wifi_connect_req_params ap_params = {
      .ssid = (const uint8_t *)AP_SSID,
      .ssid_length = strlen(AP_SSID),
      .psk = (const uint8_t *)AP_PASSWORD,
      .psk_length = strlen(AP_PASSWORD),
      .security = WIFI_SECURITY_TYPE_PSK,
      .band = WIFI_FREQ_BAND_2_4_GHZ,
      .channel = WIFI_CHANNEL_ANY,
  };
  k_sem_reset(&sem_ap_started);
  int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_params,
                     sizeof(ap_params));
  if (ret != 0) {
    LOG_ERR("AP enable request failed (error: %d)", ret);
    return ret;
  }
  if (k_sem_take(&sem_ap_started, AP_START_TIMEOUT) != 0) {
    LOG_ERR("AP enable timed out.");
    return -ETIMEDOUT;
  }
  if ((ret = configure_ap_network()) != 0 || (ret = start_dhcp_server()) != 0) {
    return ret;
  }
  LOG_INF("---------------------------------");
  LOG_INF(" Provisioning AP Mode Active");
  LOG_INF(" SSID:     %s", AP_SSID);
  LOG_INF(" Password: %s", AP_PASSWORD);
  LOG_INF(" IP Addr:  %s", AP_IP_ADDRESS);
  LOG_INF("---------------------------------");
  return 0;
}

/**
 * @brief Starts the Wi-Fi manager, deciding between Station or AP mode.
 * (CORRECTED)
 */
int wifi_manager_start(void) {
  wifi_init();
  net_mgmt_init_event_callback(&mgmt_cb, wifi_mgmt_event_handler,
                               NET_EVENT_WIFI_AP_ENABLE_RESULT |
                                   NET_EVENT_WIFI_AP_DISABLE_RESULT);
  net_mgmt_add_event_callback(&mgmt_cb);

  sta_iface = net_if_get_wifi_sta();
  ap_iface = net_if_get_wifi_sap();

  if (sta_iface == NULL || ap_iface == NULL) {
    LOG_ERR("Could not get STA (%p) or AP (%p) interface.", sta_iface,
            ap_iface);
    return -ENODEV;
  }

  const app_config_t *cfg = config_get();

  if (strlen(cfg->wifi_ssid) == 0) {
    LOG_WRN("No Wi-Fi credentials configured. Starting in AP mode.");
    return wifi_manager_start_ap();
  }

  LOG_INF("Found Wi-Fi credentials for SSID: '%s'. Attempting to connect...",
          cfg->wifi_ssid);
  if (wifi_connect_with_retires(cfg->wifi_ssid, cfg->wifi_password) != 0) {
    LOG_ERR("Failed to connect to Wi-Fi. Falling back to AP mode.");
    return wifi_manager_start_ap();
  }

  if (wifi_wait_for_ip_addr() != 0) {
    LOG_ERR("Failed to obtain IP address. Falling back to AP mode.");
    wifi_disconnect();
    return wifi_manager_start_ap();
  }

  LOG_INF("Wi-Fi connected and IP address obtained. Station mode active.");
  is_ap_mode = false;
  return 0;
}

bool wifi_manager_is_ap_mode(void) { return is_ap_mode; }
