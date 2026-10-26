#include <obs-module.h>
#include <plugin-support.h>

#include "requests.h"

// Which plugin and OBS: RastryControl checks it is there and up to date
static void get_version(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(data);
	obs_data_set_string(response, "version", PLUGIN_VERSION);
	obs_data_set_int(response, "api", RASTRY_API);
	obs_data_set_string(response, "obsVersion", obs_get_version_string());
}

void rastry_register_requests(obs_websocket_vendor vendor)
{
	struct {
		const char *name;
		obs_websocket_request_callback_function callback;
	} requests[] = {
		{"GetVersion", get_version},
	};
	for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
		if (!obs_websocket_vendor_register_request(vendor, requests[i].name, requests[i].callback, NULL))
			obs_log(LOG_WARNING, "could not register %s", requests[i].name);
	}
	obs_log(LOG_INFO, "vendor \"rastry\" ready (api %d)", RASTRY_API);
}
