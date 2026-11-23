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

#pragma once

#include "obs-websocket-api.h"

/* The vendor "rastry"'s requests; the API number goes up when they change. */
#define RASTRY_API 3

void rastry_register_requests(obs_websocket_vendor vendor);
