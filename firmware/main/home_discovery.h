#ifndef HOME_DISCOVERY_H
#define HOME_DISCOVERY_H

#include "esp_err.h"

/* Call from one startup owner after the default Wi-Fi netifs/event loop exist.
 * This module must be the application's sole mDNS owner. unit_id is the per-unit ID, "Home-ABCD"
 * (four hex digits); it only names the service instance ("Inifuss Home-ABCD"). The host name is
 * inifuss.local on every unit, and *hostname receives what was registered: if another device
 * already holds "inifuss" the responder may pick another (inifuss-2). It does not change the
 * persisted AP SSID. Repeated start for the same unit is idempotent; another unit is rejected.
 * ESP_OK means local service registration, not successful client resolution.
 * On error, the caller keeps its numeric IP URL and never requires mDNS. */
esp_err_t home_discovery_start(const char *unit_id, char hostname[33]);

#endif
