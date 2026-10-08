#include "home_discovery.h"
#include "mdns.h"
#include "esp_app_desc.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The host name is the product's, the same on every unit; the per-unit ID (the public random AP
 * suffix, "Home-XXXX") only tells units apart in the service instance name. It is never a MAC,
 * token, account, private device identifier, user-provided room name, or location. */
#define HOSTNAME "inifuss"
static char registered_unit[10];
static bool registered;

static bool unit_id_ok(const char *input)
{
    if (!input || strlen(input) != 9 || strncmp(input, "Home-", 5))
        return false;
    for (size_t i = 5; i < 9; ++i) {
        char c = input[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

/* hostname receives the name actually registered, which the responder may have changed if
 * another device on the network already claims "inifuss" ("inifuss-2"). */
esp_err_t home_discovery_start(const char *unit_id, char hostname[33])
{
    if (!unit_id_ok(unit_id))
        return ESP_ERR_INVALID_ARG;
    if (registered)
        return strcmp(unit_id, registered_unit) ? ESP_ERR_INVALID_STATE : ESP_OK;

    esp_err_t result = mdns_init();
    if (result != ESP_OK)
        return result;
    result = mdns_hostname_set(HOSTNAME);
    if (result == ESP_OK) {
        char instance[24];
        snprintf(instance, sizeof instance, "Inifuss %s", unit_id);
        result = mdns_instance_name_set(instance);
    }
    if (result == ESP_OK) {
        mdns_txt_item_t txt[] = {
            {"model", "NOTE4C"},
            {"version", esp_app_get_description()->version},
        };
        result = mdns_service_add(NULL, "_http", "_tcp", 80, txt,
                                  sizeof(txt) / sizeof(txt[0]));
    }
    if (result != ESP_OK) {
        /* Only free an instance this startup call successfully initialized.
         * No global DNS resolver, captive portal, network reset or retry loop. */
        mdns_free();
        return result;
    }
    if (mdns_hostname_get(hostname) != ESP_OK)
        snprintf(hostname, 33, "%s", HOSTNAME);
    memcpy(registered_unit, unit_id, sizeof registered_unit);
    registered = true;
    return ESP_OK;
}
