#pragma once

#include <obs.h>

void rastry_get_sceneitem_extras(obs_data_t *request, obs_data_t *response, void *data);
void rastry_set_sceneitem_scale_filter(obs_data_t *request, obs_data_t *response, void *data);
void rastry_set_sceneitem_transition(obs_data_t *request, obs_data_t *response, void *data);
void rastry_get_transition_kinds(obs_data_t *request, obs_data_t *response, void *data);
