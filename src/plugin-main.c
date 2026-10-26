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

/*
 * What RastryControl needs from OBS that obs-websocket does not offer, as
 * obs-websocket "vendor requests" of the vendor "rastry" (CallVendorRequest).
 * It opens nothing on screen: everything is data in, data out.
 *
 * Requests (requests.c):
 *   GetVersion -> { version, api, obsVersion }
 */

#include <obs-module.h>
#include <plugin-support.h>

#include "obs-websocket-api.h"
#include "requests.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "RastryControl's helper: what OBS's remote control does not offer.";
}

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "loaded (version %s)", PLUGIN_VERSION);
	return true;
}

// obs-websocket is ready only once every module has loaded
void obs_module_post_load(void)
{
	obs_websocket_vendor vendor = obs_websocket_register_vendor("rastry");
	if (!vendor) {
		obs_log(LOG_WARNING, "obs-websocket is not available: RastryControl cannot use this plugin");
		return;
	}
	rastry_register_requests(vendor);
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "unloaded");
}
