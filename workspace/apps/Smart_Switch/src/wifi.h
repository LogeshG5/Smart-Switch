#ifndef WIFI_H_
#define WIFI_H_

#include <stdbool.h>

typedef void (*wifi_disconnect_callback_t)(void);
void wifi_set_disconnect_callback(wifi_disconnect_callback_t callback);
// Function prototypes
void wifi_init(void);
int wifi_connect(const char *ssid, const char *psk);
int wifi_wait_for_ip_addr(void);
int wifi_disconnect(void);
bool wifi_is_connected(void);

#endif // WIFI_H_
