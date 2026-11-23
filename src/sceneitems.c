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
 * What a scene item has in OBS's menu that obs-websocket cannot set: the
 * scale filter, and the transitions it shows and hides with.
 *
 *   GetSceneItemExtras { sceneName, sceneItemId }
 *     -> { scaleFilter, show: { kind?, duration }, hide: { kind?, duration } }
 *   SetSceneItemScaleFilter { sceneName, sceneItemId, scaleFilter }
 *     scaleFilter: "disable" | "point" | "bicubic" | "bilinear" | "lanczos" | "area"
 *   SetSceneItemTransition { sceneName, sceneItemId, show, kind?, duration? }
 *     show: true for showing, false for hiding; no kind = none
 *   GetTransitionKinds -> { kinds: [{ kind, name }] }
 *   CreateGroup { sceneName, groupName, sources: [{ name }] } -> { sceneItemId }
 */

#include <obs-module.h>
#include <plugin-support.h>

#include "sceneitems.h"

static const struct {
	enum obs_scale_type type;
	const char *name;
} SCALE_FILTERS[] = {
	{OBS_SCALE_DISABLE, "disable"}, {OBS_SCALE_POINT, "point"},     {OBS_SCALE_BICUBIC, "bicubic"},
	{OBS_SCALE_BILINEAR, "bilinear"}, {OBS_SCALE_LANCZOS, "lanczos"}, {OBS_SCALE_AREA, "area"},
};

/* The scene item a request names (a new reference), or NULL with the reason in `response`. */
static obs_sceneitem_t *find_item(obs_data_t *request, obs_data_t *response)
{
	obs_source_t *source = obs_get_source_by_name(obs_data_get_string(request, "sceneName"));
	obs_scene_t *scene = source ? obs_scene_from_source(source) : NULL;
	if (source && !scene)
		scene = obs_group_from_source(source);
	obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, obs_data_get_int(request, "sceneItemId")) : NULL;
	if (item)
		obs_sceneitem_addref(item);
	else
		obs_data_set_string(response, "error", "No scene item with that id in that scene.");
	obs_source_release(source);
	return item;
}

static void describe_transition(obs_sceneitem_t *item, bool show, obs_data_t *out)
{
	obs_data_t *t = obs_data_create();
	obs_source_t *transition = obs_sceneitem_get_transition(item, show);
	if (transition)
		obs_data_set_string(t, "kind", obs_source_get_id(transition));
	obs_data_set_int(t, "duration", obs_sceneitem_get_transition_duration(item, show));
	obs_data_set_obj(out, show ? "show" : "hide", t);
	obs_data_release(t);
}

void rastry_get_sceneitem_extras(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	obs_sceneitem_t *item = find_item(request, response);
	if (!item)
		return;
	enum obs_scale_type scale = obs_sceneitem_get_scale_filter(item);
	for (size_t i = 0; i < sizeof(SCALE_FILTERS) / sizeof(SCALE_FILTERS[0]); i++)
		if (SCALE_FILTERS[i].type == scale)
			obs_data_set_string(response, "scaleFilter", SCALE_FILTERS[i].name);
	describe_transition(item, true, response);
	describe_transition(item, false, response);
	obs_sceneitem_release(item);
}

void rastry_set_sceneitem_scale_filter(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	const char *wanted = obs_data_get_string(request, "scaleFilter");
	for (size_t i = 0; i < sizeof(SCALE_FILTERS) / sizeof(SCALE_FILTERS[0]); i++) {
		if (strcmp(SCALE_FILTERS[i].name, wanted) != 0)
			continue;
		obs_sceneitem_t *item = find_item(request, response);
		if (!item)
			return;
		obs_sceneitem_set_scale_filter(item, SCALE_FILTERS[i].type);
		obs_sceneitem_release(item);
		return;
	}
	obs_data_set_string(response, "error", "Unknown scale filter.");
}

void rastry_set_sceneitem_transition(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	obs_sceneitem_t *item = find_item(request, response);
	if (!item)
		return;
	bool show = obs_data_get_bool(request, "show");
	const char *kind = obs_data_get_string(request, "kind");
	if (kind && *kind) {
		// Each item keeps its own private transition, as OBS does
		obs_source_t *transition = obs_source_create_private(kind, show ? "Show transition" : "Hide transition", NULL);
		if (!transition) {
			obs_data_set_string(response, "error", "Unknown kind of transition.");
			obs_sceneitem_release(item);
			return;
		}
		obs_sceneitem_set_transition(item, show, transition);
		obs_source_release(transition);
	} else {
		obs_sceneitem_set_transition(item, show, NULL);
	}
	if (obs_data_has_user_value(request, "duration"))
		obs_sceneitem_set_transition_duration(item, show, (uint32_t)obs_data_get_int(request, "duration"));
	obs_sceneitem_release(item);
}

void rastry_get_transition_kinds(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(data);
	obs_data_array_t *kinds = obs_data_array_create();
	const char *id;
	for (size_t i = 0; obs_enum_transition_types(i, &id); i++) {
		// Hidden or deprecated kinds are not offered by OBS either
		if (obs_get_source_output_flags(id) & OBS_SOURCE_DEPRECATED)
			continue;
		obs_data_t *k = obs_data_create();
		obs_data_set_string(k, "kind", id);
		obs_data_set_string(k, "name", obs_source_get_display_name(id));
		obs_data_array_push_back(kinds, k);
		obs_data_release(k);
	}
	obs_data_set_array(response, "kinds", kinds);
	obs_data_array_release(kinds);
}

/*
 * CreateGroup { sceneName, groupName, sources: [{ name }] } -> { sceneItemId }
 * Puts those sources of the scene into a new group (obs-websocket cannot make
 * groups). Sources not in the scene are left out; none found is an error.
 */
void rastry_create_group(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	const char *group_name = obs_data_get_string(request, "groupName");
	if (!group_name || !*group_name) {
		obs_data_set_string(response, "error", "No group name.");
		return;
	}
	obs_source_t *taken = obs_get_source_by_name(group_name);
	if (taken) {
		obs_source_release(taken);
		obs_data_set_string(response, "error", "Something in OBS already has that name.");
		return;
	}
	obs_source_t *source = obs_get_source_by_name(obs_data_get_string(request, "sceneName"));
	obs_scene_t *scene = source ? obs_scene_from_source(source) : NULL;
	obs_data_array_t *sources = obs_data_get_array(request, "sources");
	size_t count = sources ? obs_data_array_count(sources) : 0;
	if (!scene || count == 0) {
		obs_data_set_string(response, "error", scene ? "No sources to group." : "No scene with that name.");
		obs_data_array_release(sources);
		obs_source_release(source);
		return;
	}
	obs_sceneitem_t **items = bzalloc(sizeof(*items) * count);
	size_t found = 0;
	for (size_t i = 0; i < count; i++) {
		obs_data_t *entry = obs_data_array_item(sources, i);
		obs_sceneitem_t *item = obs_scene_find_source(scene, obs_data_get_string(entry, "name"));
		if (item)
			items[found++] = item;
		obs_data_release(entry);
	}
	if (found == 0) {
		obs_data_set_string(response, "error", "None of those sources is in the scene.");
	} else {
		obs_sceneitem_t *group = obs_scene_insert_group(scene, group_name, items, found);
		if (group)
			obs_data_set_int(response, "sceneItemId", obs_sceneitem_get_id(group));
		else
			obs_data_set_string(response, "error", "OBS could not make the group.");
	}
	bfree(items);
	obs_data_array_release(sources);
	obs_source_release(source);
}
