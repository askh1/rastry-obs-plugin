/*
 * The properties of a source or filter as OBS's own dialog shows them, as
 * data: what obs-websocket lacks to draw an exact form (labels, kinds of
 * field, limits, list items, groups, which fields show now).
 *
 *   GetProperties { sourceName, filterName? }
 *     -> { kind, settings, defaults, properties: [ property ] }
 *   PressPropertyButton { sourceName, filterName?, propertyName }
 *     -> { pressed }
 *
 * property: { name, label, description?, kind, visible, enabled, ... }
 *   kind "bool" | "int" | "float" | "text" | "path" | "list" | "color" |
 *        "colorAlpha" | "button" | "font" | "editableList" | "frameRate" | "group"
 *   int/float: min, max, step, slider, suffix
 *   text: textKind ("default" | "password" | "multiline" | "info"), monospace
 *   path: pathKind ("file" | "save" | "folder"), filter, defaultPath
 *   list: listKind ("list" | "editable" | "radio"), format ("int" | "float" | "string" | "bool"),
 *         items: [{ name, value, enabled }]
 *   editableList: listKind ("strings" | "files" | "filesAndUrls"), filter
 *   group: groupKind ("normal" | "checkable"), properties: [ property ]
 *
 * After a change, ask again: OBS shows and hides fields (and fills lists)
 * according to the settings, as its dialog does.
 */

#include <obs-module.h>
#include <plugin-support.h>

#include "properties.h"

static const char *kind_name(enum obs_property_type type)
{
	switch (type) {
	case OBS_PROPERTY_BOOL:
		return "bool";
	case OBS_PROPERTY_INT:
		return "int";
	case OBS_PROPERTY_FLOAT:
		return "float";
	case OBS_PROPERTY_TEXT:
		return "text";
	case OBS_PROPERTY_PATH:
		return "path";
	case OBS_PROPERTY_LIST:
		return "list";
	case OBS_PROPERTY_COLOR:
		return "color";
	case OBS_PROPERTY_COLOR_ALPHA:
		return "colorAlpha";
	case OBS_PROPERTY_BUTTON:
		return "button";
	case OBS_PROPERTY_FONT:
		return "font";
	case OBS_PROPERTY_EDITABLE_LIST:
		return "editableList";
	case OBS_PROPERTY_FRAME_RATE:
		return "frameRate";
	case OBS_PROPERTY_GROUP:
		return "group";
	default:
		return "unknown";
	}
}

static obs_data_array_t *describe_all(obs_properties_t *props);

static void describe_list(obs_property_t *p, obs_data_t *out)
{
	enum obs_combo_type type = obs_property_list_type(p);
	enum obs_combo_format format = obs_property_list_format(p);
	obs_data_set_string(out, "listKind",
			    type == OBS_COMBO_TYPE_EDITABLE ? "editable"
			    : type == OBS_COMBO_TYPE_RADIO  ? "radio"
							    : "list");
	obs_data_set_string(out, "format",
			    format == OBS_COMBO_FORMAT_INT     ? "int"
			    : format == OBS_COMBO_FORMAT_FLOAT ? "float"
			    : format == OBS_COMBO_FORMAT_BOOL  ? "bool"
							       : "string");
	obs_data_array_t *items = obs_data_array_create();
	size_t count = obs_property_list_item_count(p);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", obs_property_list_item_name(p, i));
		obs_data_set_bool(item, "enabled", !obs_property_list_item_disabled(p, i));
		switch (format) {
		case OBS_COMBO_FORMAT_INT:
			obs_data_set_int(item, "value", obs_property_list_item_int(p, i));
			break;
		case OBS_COMBO_FORMAT_FLOAT:
			obs_data_set_double(item, "value", obs_property_list_item_float(p, i));
			break;
		case OBS_COMBO_FORMAT_BOOL:
			obs_data_set_bool(item, "value", obs_property_list_item_bool(p, i));
			break;
		default:
			obs_data_set_string(item, "value", obs_property_list_item_string(p, i));
		}
		obs_data_array_push_back(items, item);
		obs_data_release(item);
	}
	obs_data_set_array(out, "items", items);
	obs_data_array_release(items);
}

static obs_data_t *describe(obs_property_t *p)
{
	obs_data_t *out = obs_data_create();
	enum obs_property_type type = obs_property_get_type(p);
	obs_data_set_string(out, "name", obs_property_name(p));
	obs_data_set_string(out, "label", obs_property_description(p));
	if (obs_property_long_description(p))
		obs_data_set_string(out, "description", obs_property_long_description(p));
	obs_data_set_string(out, "kind", kind_name(type));
	obs_data_set_bool(out, "visible", obs_property_visible(p));
	obs_data_set_bool(out, "enabled", obs_property_enabled(p));

	switch (type) {
	case OBS_PROPERTY_INT:
		obs_data_set_int(out, "min", obs_property_int_min(p));
		obs_data_set_int(out, "max", obs_property_int_max(p));
		obs_data_set_int(out, "step", obs_property_int_step(p));
		obs_data_set_bool(out, "slider", obs_property_int_type(p) == OBS_NUMBER_SLIDER);
		if (obs_property_int_suffix(p))
			obs_data_set_string(out, "suffix", obs_property_int_suffix(p));
		break;
	case OBS_PROPERTY_FLOAT:
		obs_data_set_double(out, "min", obs_property_float_min(p));
		obs_data_set_double(out, "max", obs_property_float_max(p));
		obs_data_set_double(out, "step", obs_property_float_step(p));
		obs_data_set_bool(out, "slider", obs_property_float_type(p) == OBS_NUMBER_SLIDER);
		if (obs_property_float_suffix(p))
			obs_data_set_string(out, "suffix", obs_property_float_suffix(p));
		break;
	case OBS_PROPERTY_TEXT: {
		enum obs_text_type t = obs_property_text_type(p);
		obs_data_set_string(out, "textKind",
				    t == OBS_TEXT_PASSWORD    ? "password"
				    : t == OBS_TEXT_MULTILINE ? "multiline"
				    : t == OBS_TEXT_INFO      ? "info"
							      : "default");
		obs_data_set_bool(out, "monospace", obs_property_text_monospace(p));
		break;
	}
	case OBS_PROPERTY_PATH: {
		enum obs_path_type t = obs_property_path_type(p);
		obs_data_set_string(out, "pathKind",
				    t == OBS_PATH_FILE_SAVE   ? "save"
				    : t == OBS_PATH_DIRECTORY ? "folder"
							      : "file");
		if (obs_property_path_filter(p))
			obs_data_set_string(out, "filter", obs_property_path_filter(p));
		if (obs_property_path_default_path(p))
			obs_data_set_string(out, "defaultPath", obs_property_path_default_path(p));
		break;
	}
	case OBS_PROPERTY_LIST:
		describe_list(p, out);
		break;
	case OBS_PROPERTY_EDITABLE_LIST: {
		enum obs_editable_list_type t = obs_property_editable_list_type(p);
		obs_data_set_string(out, "listKind",
				    t == OBS_EDITABLE_LIST_TYPE_FILES            ? "files"
				    : t == OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS ? "filesAndUrls"
										 : "strings");
		if (obs_property_editable_list_filter(p))
			obs_data_set_string(out, "filter", obs_property_editable_list_filter(p));
		break;
	}
	case OBS_PROPERTY_GROUP: {
		obs_data_set_string(out, "groupKind",
				    obs_property_group_type(p) == OBS_GROUP_CHECKABLE ? "checkable" : "normal");
		obs_data_array_t *children = describe_all(obs_property_group_content(p));
		obs_data_set_array(out, "properties", children);
		obs_data_array_release(children);
		break;
	}
	default:
		break;
	}
	return out;
}

static obs_data_array_t *describe_all(obs_properties_t *props)
{
	obs_data_array_t *list = obs_data_array_create();
	for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
		obs_data_t *item = describe(p);
		obs_data_array_push_back(list, item);
		obs_data_release(item);
	}
	return list;
}

/* The source or filter a request names (a new reference), or NULL with the reason in `response`. */
static obs_source_t *target(obs_data_t *request, obs_data_t *response)
{
	const char *source_name = obs_data_get_string(request, "sourceName");
	const char *filter_name = obs_data_get_string(request, "filterName");
	obs_source_t *source = obs_get_source_by_name(source_name);
	if (!source) {
		obs_data_set_string(response, "error", "No source with that name.");
		return NULL;
	}
	if (!filter_name || !*filter_name)
		return source;
	obs_source_t *filter = obs_source_get_filter_by_name(source, filter_name);
	obs_source_release(source);
	if (!filter)
		obs_data_set_string(response, "error", "No filter with that name on that source.");
	return filter;
}

void rastry_get_properties(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	obs_source_t *source = target(request, response);
	if (!source)
		return;
	obs_properties_t *props = obs_source_properties(source);
	obs_data_t *settings = obs_source_get_settings(source);
	// Show and hide fields as the dialog would for these settings
	if (props)
		obs_properties_apply_settings(props, settings);

	obs_data_set_string(response, "kind", obs_source_get_id(source));
	obs_data_set_obj(response, "settings", settings);
	obs_data_t *defaults = obs_data_get_defaults(settings);
	obs_data_set_obj(response, "defaults", defaults);
	obs_data_release(defaults);
	obs_data_array_t *list = props ? describe_all(props) : obs_data_array_create();
	obs_data_set_array(response, "properties", list);

	obs_data_array_release(list);
	obs_data_release(settings);
	obs_properties_destroy(props);
	obs_source_release(source);
}

void rastry_press_property_button(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	obs_source_t *source = target(request, response);
	if (!source)
		return;
	obs_properties_t *props = obs_source_properties(source);
	obs_property_t *p = props ? obs_properties_get(props, obs_data_get_string(request, "propertyName")) : NULL;
	bool pressed = false;
	if (p && obs_property_get_type(p) == OBS_PROPERTY_BUTTON)
		pressed = obs_property_button_clicked(p, source);
	else
		obs_data_set_string(response, "error", "No button with that name.");
	obs_data_set_bool(response, "pressed", pressed);
	obs_properties_destroy(props);
	obs_source_release(source);
}
