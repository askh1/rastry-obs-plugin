#pragma once

#include <obs-module.h>

void rastry_feed_load(void);
void rastry_feed_unload(void);
void rastry_get_preview_encoders(obs_data_t *request, obs_data_t *response, void *data);
void rastry_start_preview_feed(obs_data_t *request, obs_data_t *response, void *data);
void rastry_stop_preview_feed(obs_data_t *request, obs_data_t *response, void *data);
