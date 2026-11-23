/*
Rastry for OBS
Copyright (C) 2026 Rastry

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

#include "requests.h"
#include "properties.h"
#include "sceneitems.h"
#include "interact.h"
#include "feed.h"

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
		{"GetProperties", rastry_get_properties},
		{"PressPropertyButton", rastry_press_property_button},
		{"GetSceneItemExtras", rastry_get_sceneitem_extras},
		{"SetSceneItemScaleFilter", rastry_set_sceneitem_scale_filter},
		{"SetSceneItemTransition", rastry_set_sceneitem_transition},
		{"GetTransitionKinds", rastry_get_transition_kinds},
		{"CreateGroup", rastry_create_group},
		{"InteractMouse", rastry_interact_mouse},
		{"InteractKey", rastry_interact_key},
		{"GetPreviewEncoders", rastry_get_preview_encoders},
		{"StartPreviewFeed", rastry_start_preview_feed},
		{"StopPreviewFeed", rastry_stop_preview_feed},
	};
	for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
		if (!obs_websocket_vendor_register_request(vendor, requests[i].name, requests[i].callback, NULL))
			obs_log(LOG_WARNING, "could not register %s", requests[i].name);
	}
	obs_log(LOG_INFO, "vendor \"rastry\" ready (api %d)", RASTRY_API);
}
