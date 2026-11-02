#pragma once

#include "obs-websocket-api.h"

/* The vendor "rastry"'s requests; the API number goes up when they change. */
#define RASTRY_API 2

void rastry_register_requests(obs_websocket_vendor vendor);
