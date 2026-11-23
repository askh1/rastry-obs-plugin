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

#include <obs.h>

void rastry_get_sceneitem_extras(obs_data_t *request, obs_data_t *response, void *data);
void rastry_set_sceneitem_scale_filter(obs_data_t *request, obs_data_t *response, void *data);
void rastry_set_sceneitem_transition(obs_data_t *request, obs_data_t *response, void *data);
void rastry_get_transition_kinds(obs_data_t *request, obs_data_t *response, void *data);
void rastry_create_group(obs_data_t *request, obs_data_t *response, void *data);
