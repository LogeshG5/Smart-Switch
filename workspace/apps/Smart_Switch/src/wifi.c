#include "wifi.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(wifi_driver, CONFIG_LOG_DEFAULT_LEVEL);

// --- Configuration (UPDATED) ---
// Define timeout values as simple integers for easy use in logs
#define WIFI_CONNECT_TIMEOUT_S 30
#define IP_ADDR_OBTAIN_TIMEOUT_S 30

// --- State Management ---
static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(sem_wifi_connected, 0, 1);
static K_SEM_DEFINE(sem_ipv4_obtained, 0, 1);
static bool is_connected = false;

// --- Event Handlers ---

static void on_wifi_mgmt_event(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface) {
  const struct wifi_status *status = (const struct wifi_status *)cb->info;
  switch (mgmt_event) {
  case NET_EVENT_WIFI_CONNECT_RESULT:
    if (status->status == 0) {
      LOG_INF("Wi-Fi connected successfully!");
      is_connected = true;
    } else {
      LOG_ERR("Wi-Fi connection failed with status: %d", status->status);
      is_connected = false;
    }
    k_sem_give(&sem_wifi_connected);
    break;
  case NET_EVENT_WIFI_DISCONNECT_RESULT:
    LOG_INF("Wi-Fi disconnected.");
    is_connected = false;
    k_sem_reset(&sem_wifi_connected);
    k_sem_reset(&sem_ipv4_obtained);
    break;
  default:
    break;
  }
}

/**
 * @brief Event handler for IPv4 address acquisition.
 */
static void on_ipv4_mgmt_event(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface) {
  if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
    return;
  }

  // Iterate through the unicast IPv4 addresses
  for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {

    struct net_if_addr *ifaddr = &iface->config.ip.ipv4->unicast[i].ipv4;

    if (ifaddr->is_used && ifaddr->addr_type == NET_ADDR_DHCP) {
      char ip_buf[NET_IPV4_ADDR_LEN];

      // Now that we have the correct ifaddr, we can get its address
      if (net_addr_ntop(AF_INET, &ifaddr->address.in_addr, ip_buf,
                        sizeof(ip_buf))) {
        LOG_INF("IPv4 address obtained via DHCP: %s", ip_buf);
        k_sem_give(&sem_ipv4_obtained);
        return; // Exit after finding the DHCP address
      }
    }
  }
}

// --- Public API ---

void wifi_init(void) {
  net_mgmt_init_event_callback(&wifi_cb, on_wifi_mgmt_event,
                               NET_EVENT_WIFI_CONNECT_RESULT |
                                   NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_init_event_callback(&ipv4_cb, on_ipv4_mgmt_event,
                               NET_EVENT_IPV4_ADDR_ADD);
  net_mgmt_add_event_callback(&wifi_cb);
  net_mgmt_add_event_callback(&ipv4_cb);
}

int wifi_connect(const char *ssid, const char *psk) {
  struct net_if *iface = net_if_get_default();
  if (!iface) {
    LOG_ERR("Could not get default network interface.");
    return -ENODEV;
  }

  struct wifi_connect_req_params params = {
      .ssid = (const uint8_t *)ssid,
      .ssid_length = strlen(ssid),
      .psk = (const uint8_t *)psk,
      .psk_length = strlen(psk),
      .security = (psk && params.psk_length > 0) ? WIFI_SECURITY_TYPE_PSK
                                                 : WIFI_SECURITY_TYPE_NONE,
      .channel = WIFI_CHANNEL_ANY,
  };

  k_sem_reset(&sem_wifi_connected);
  is_connected = false;

  LOG_INF("Attempting to connect to SSID: %s", ssid);
  int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
  if (ret != 0) {
    LOG_ERR("Wi-Fi connection request failed with error: %d", ret);
    return ret;
  }

  // Wait for the connection result, with a timeout (CORRECTED)
  if (k_sem_take(&sem_wifi_connected, K_SECONDS(WIFI_CONNECT_TIMEOUT_S)) != 0) {
    LOG_ERR("Wi-Fi connection timed out after %u seconds.",
            WIFI_CONNECT_TIMEOUT_S);
    (void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
    return -ETIMEDOUT;
  }

  return is_connected ? 0 : -EIO;
}

int wifi_wait_for_ip_addr(void) {
  // Wait for the IPv4 address to be obtained, with a timeout (CORRECTED)
  if (k_sem_take(&sem_ipv4_obtained, K_SECONDS(IP_ADDR_OBTAIN_TIMEOUT_S)) !=
      0) {
    LOG_ERR("DHCP (IPv4 address) lease timed out after %u seconds.",
            IP_ADDR_OBTAIN_TIMEOUT_S);
    return -ETIMEDOUT;
  }
  return 0;
}

int wifi_disconnect(void) {
  struct net_if *iface = net_if_get_default();
  if (!iface) {
    return -ENODEV;
  }

  int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
  if (ret != 0) {
    LOG_ERR("Wi-Fi disconnection request failed with error: %d", ret);
  }
  return ret;
}
