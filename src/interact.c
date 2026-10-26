/*
 * Interacting with a source (a browser source: clicks, scrolling, typing)
 * from RastryControl's preview, as OBS's Interact window does, without
 * opening anything on the streamer's screen.
 *
 *   InteractMouse { sourceName, event, x, y, button?, clicks?, deltaX?, deltaY?, modifiers? }
 *     event: "move" | "down" | "up" | "wheel" | "leave"
 *     x, y: in the source's own pixels; button: 0 left, 1 middle, 2 right
 *   InteractKey { sourceName, down, text?, vkey?, modifiers? }
 *     text: what the key types; vkey: Windows virtual-key code (Enter, arrows…)
 *   modifiers: { shift, ctrl, alt }
 */

#include <obs-module.h>
#include <plugin-support.h>

#include "interact.h"

static uint32_t modifiers_of(obs_data_t *request)
{
	obs_data_t *m = obs_data_get_obj(request, "modifiers");
	uint32_t out = 0;
	if (m) {
		if (obs_data_get_bool(m, "shift"))
			out |= INTERACT_SHIFT_KEY;
		if (obs_data_get_bool(m, "ctrl"))
			out |= INTERACT_CONTROL_KEY;
		if (obs_data_get_bool(m, "alt"))
			out |= INTERACT_ALT_KEY;
		obs_data_release(m);
	}
	return out;
}

/* Only sources that take interaction (OBS's own check) */
static obs_source_t *interactive(obs_data_t *request, obs_data_t *response)
{
	obs_source_t *source = obs_get_source_by_name(obs_data_get_string(request, "sourceName"));
	if (!source) {
		obs_data_set_string(response, "error", "No source with that name.");
		return NULL;
	}
	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_INTERACTION)) {
		obs_data_set_string(response, "error", "This source cannot be interacted with.");
		obs_source_release(source);
		return NULL;
	}
	return source;
}

void rastry_interact_mouse(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	obs_source_t *source = interactive(request, response);
	if (!source)
		return;
	struct obs_mouse_event event = {
		.modifiers = modifiers_of(request),
		.x = (int32_t)obs_data_get_int(request, "x"),
		.y = (int32_t)obs_data_get_int(request, "y"),
	};
	const char *kind = obs_data_get_string(request, "event");
	int32_t button = (int32_t)obs_data_get_int(request, "button");
	if (button == 0)
		event.modifiers |= INTERACT_MOUSE_LEFT;
	else if (button == 1)
		event.modifiers |= INTERACT_MOUSE_MIDDLE;
	else
		event.modifiers |= INTERACT_MOUSE_RIGHT;
	uint32_t clicks = (uint32_t)obs_data_get_int(request, "clicks");
	if (!clicks)
		clicks = 1;

	if (strcmp(kind, "down") == 0 || strcmp(kind, "up") == 0) {
		obs_source_send_focus(source, true);
		obs_source_send_mouse_click(source, &event, button, strcmp(kind, "up") == 0, clicks);
	} else if (strcmp(kind, "wheel") == 0) {
		obs_source_send_mouse_wheel(source, &event, (int)obs_data_get_int(request, "deltaX"),
					    (int)obs_data_get_int(request, "deltaY"));
	} else if (strcmp(kind, "leave") == 0) {
		obs_source_send_mouse_move(source, &event, true);
	} else {
		obs_source_send_mouse_move(source, &event, false);
	}
	obs_source_release(source);
}

void rastry_interact_key(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	obs_source_t *source = interactive(request, response);
	if (!source)
		return;
	struct obs_key_event event = {
		.modifiers = modifiers_of(request),
		.text = (char *)obs_data_get_string(request, "text"),
		.native_vkey = (uint32_t)obs_data_get_int(request, "vkey"),
	};
	obs_source_send_focus(source, true);
	obs_source_send_key_click(source, &event, !obs_data_get_bool(request, "down"));
	obs_source_release(source);
}
