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
 * Moderators' preview: a scene rendered by OBS itself, encoded by the best
 * encoder this PC really has, handed to RastryControl over a private named
 * pipe. No screenshots, no JPEG, and nothing is decided for one kind of PC:
 * the encoders are the ones OBS lists here, tried in order until one starts.
 *
 * Requests:
 *   GetPreviewEncoders -> { encoders: [{ id, codec, hardware }] }, best first
 *   StartPreviewFeed { scene, width, fps, kbps, codecs? } ->
 *     { ok, id, pipe, encoder, codec, hardware, width, height, fps } or { ok: false, error }
 *     fps: OBS's own frame rate divided by a whole number, the closest to what was asked
 *   StopPreviewFeed { id }
 *
 * On the pipe, little endian, one record per encoded frame:
 *   u32 size, u8 flags (1 = key frame), i64 pts in microseconds,
 *   i64 delay in microseconds over the fastest frame so far (how steady the encoder is), data.
 * Key frames carry the encoder's headers first, so anyone can start at one.
 *
 * A feed ends when RastryControl closes the pipe or never opens it: a crash on
 * its side cannot leave OBS rendering and encoding for nobody.
 */

#include <obs-module.h>
#include <plugin-support.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>

#include "feed.h"

#define OUTPUT_ID "rastry_preview_output"
// A viewer that lost packets recovers at the next key frame: OBS's encoders cannot be asked for one
#define KEY_FRAME_SECONDS 1
#define CONNECT_TIMEOUT_MS 5000
// About a second of 1080p: past this the reader is too slow, frames are dropped up to a key frame
#define MAX_QUEUED_BYTES (2 * 1024 * 1024)
#define MAX_WIDTH 3840
#define MIN_WIDTH 320
#define MAX_KBPS 50000
#define MAX_FEEDS 8

struct record {
	struct record *next;
	size_t size;
	uint8_t bytes[];
};

struct feed {
	struct feed *next;
	long long id;
	char pipe_name[80];
	HANDLE pipe;
	HANDLE thread;
	HANDLE wake;
	HANDLE done;
	volatile LONG stopping;
	CRITICAL_SECTION lock;
	struct record *head, *tail;
	size_t queued;
	bool waiting_key;
	bool has_clock;
	int64_t clock_base;
	obs_view_t *view;
	obs_output_t *output;
	obs_encoder_t *encoder;
	obs_source_t *scene;
};

static CRITICAL_SECTION feeds_lock;
static struct feed *feeds;
static long long next_id = 1;

/* The encoders, ranked */

enum family { FAMILY_NVENC, FAMILY_AMF, FAMILY_QSV, FAMILY_X264, FAMILY_OTHER };

static enum family family_of(const char *id)
{
	if (strstr(id, "nvenc"))
		return FAMILY_NVENC;
	if (strstr(id, "amf"))
		return FAMILY_AMF;
	if (strstr(id, "qsv"))
		return FAMILY_QSV;
	if (strcmp(id, "obs_x264") == 0)
		return FAMILY_X264;
	return FAMILY_OTHER;
}

static bool is_hardware(enum family family)
{
	return family == FAMILY_NVENC || family == FAMILY_AMF || family == FAMILY_QSV;
}

// Video encoders a browser can play and that keep up live; others (HEVC, software AV1) never
static bool usable(const char *id, const char *codec)
{
	if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
		return false;
	if (obs_get_encoder_caps(id) & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
		return false;
	enum family family = family_of(id);
	if (family == FAMILY_OTHER)
		return false;
	if (strcmp(codec, "h264") == 0)
		return true;
	return strcmp(codec, "av1") == 0 && is_hardware(family);
}

// Calls back with every usable encoder for this codec: hardware first, the CPU last
static void for_each_encoder(const char *codec, bool (*callback)(const char *id, bool hardware, void *data),
			     void *data)
{
	static const enum family order[] = {FAMILY_NVENC, FAMILY_AMF, FAMILY_QSV, FAMILY_X264};
	for (size_t f = 0; f < sizeof(order) / sizeof(order[0]); f++) {
		const char *id;
		for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
			const char *its = obs_get_encoder_codec(id);
			if (!its || strcmp(its, codec) != 0 || !usable(id, its) || family_of(id) != order[f])
				continue;
			if (!callback(id, is_hardware(order[f]), data))
				return;
		}
	}
}

/* The output that takes the encoded frames */

static void queue_record(struct feed *feed, struct encoder_packet *packet)
{
	uint8_t *headers = NULL;
	size_t headers_size = 0;
	if (packet->keyframe)
		obs_encoder_get_extra_data(feed->encoder, &headers, &headers_size);

	uint32_t size = (uint32_t)(headers_size + packet->size);
	int64_t pts = packet->pts * 1000000 * packet->timebase_num / packet->timebase_den;
	// Packet times start at the feed's first frame: only differences can be told
	int64_t since = (int64_t)(os_gettime_ns() / 1000) - packet->dts_usec;
	if (!feed->has_clock || since < feed->clock_base) {
		feed->has_clock = true;
		feed->clock_base = since;
	}
	int64_t latency = since - feed->clock_base;
	uint8_t flags = packet->keyframe ? 1 : 0;

	size_t total = 4 + 1 + 8 + 8 + (size_t)size;
	struct record *record = bmalloc(sizeof(struct record) + total);
	record->next = NULL;
	record->size = total;
	uint8_t *at = record->bytes;
	memcpy(at, &size, 4);
	at[4] = flags;
	memcpy(at + 5, &pts, 8);
	memcpy(at + 13, &latency, 8);
	at += 21;
	if (headers_size)
		memcpy(at, headers, headers_size);
	memcpy(at + headers_size, packet->data, packet->size);

	EnterCriticalSection(&feed->lock);
	if (feed->queued + total > MAX_QUEUED_BYTES)
		feed->waiting_key = true;
	if (feed->waiting_key && packet->keyframe && feed->queued + total <= MAX_QUEUED_BYTES)
		feed->waiting_key = false;
	if (feed->waiting_key) {
		LeaveCriticalSection(&feed->lock);
		bfree(record);
		return;
	}
	if (feed->tail)
		feed->tail->next = record;
	else
		feed->head = record;
	feed->tail = record;
	feed->queued += total;
	LeaveCriticalSection(&feed->lock);
	SetEvent(feed->wake);
}

struct preview_output {
	obs_output_t *output;
	struct feed *feed;
};

static const char *output_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return "Rastry preview";
}

static void *output_create(obs_data_t *settings, obs_output_t *output)
{
	struct preview_output *self = bzalloc(sizeof(*self));
	self->output = output;
	self->feed = (struct feed *)(intptr_t)obs_data_get_int(settings, "feed");
	return self;
}

static void output_destroy(void *data)
{
	bfree(data);
}

static bool output_start(void *data)
{
	struct preview_output *self = data;
	if (!obs_output_can_begin_data_capture(self->output, 0))
		return false;
	if (!obs_output_initialize_encoders(self->output, 0))
		return false;
	return obs_output_begin_data_capture(self->output, 0);
}

static void output_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);
	struct preview_output *self = data;
	obs_output_end_data_capture(self->output);
}

static void output_packet(void *data, struct encoder_packet *packet)
{
	struct preview_output *self = data;
	if (packet && packet->type == OBS_ENCODER_VIDEO)
		queue_record(self->feed, packet);
}

static struct obs_output_info output_info = {
	.id = OUTPUT_ID,
	.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED,
	.get_name = output_name,
	.create = output_create,
	.destroy = output_destroy,
	.start = output_start,
	.stop = output_stop,
	.encoded_packet = output_packet,
};

/* One feed */

static obs_data_t *encoder_settings(const char *id, const char *codec, long long kbps)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", kbps);
	obs_data_set_int(settings, "max_bitrate", kbps);
	obs_data_set_int(settings, "keyint_sec", KEY_FRAME_SECONDS);
	bool h264 = strcmp(codec, "h264") == 0;
	// No frames held back: every setting that adds delay off, and a profile WebRTC plays
	switch (family_of(id)) {
	case FAMILY_NVENC:
		obs_data_set_string(settings, "rate_control", "cbr");
		obs_data_set_string(settings, "preset", "p2");
		obs_data_set_string(settings, "tune", "ll");
		obs_data_set_string(settings, "multipass", "disabled");
		obs_data_set_bool(settings, "lookahead", false);
		obs_data_set_bool(settings, "psycho_aq", false);
		obs_data_set_int(settings, "bf", 0);
		if (h264)
			obs_data_set_string(settings, "profile", "baseline");
		break;
	case FAMILY_AMF:
		obs_data_set_string(settings, "preset", "speed");
		obs_data_set_int(settings, "bf", 0);
		if (h264)
			obs_data_set_string(settings, "profile", "baseline");
		break;
	case FAMILY_QSV:
		obs_data_set_string(settings, "target_usage", "TU7");
		obs_data_set_string(settings, "latency", "ultra-low");
		obs_data_set_int(settings, "bframes", 0);
		if (h264)
			obs_data_set_string(settings, "profile", "baseline");
		break;
	default:
		obs_data_set_string(settings, "preset", "veryfast");
		obs_data_set_string(settings, "tune", "zerolatency");
		obs_data_set_string(settings, "profile", "baseline");
		break;
	}
	return settings;
}

struct attempt {
	struct feed *feed;
	video_t *video;
	long long kbps;
	uint32_t divisor;
	const char *codec;
	const char *started;
	bool hardware;
	struct dstr failures;
};

// Tries one encoder for real: a PC may list one its GPU or driver cannot run
static bool try_encoder(const char *id, bool hardware, void *data)
{
	struct attempt *attempt = data;
	struct feed *feed = attempt->feed;

	obs_data_t *settings = encoder_settings(id, attempt->codec, attempt->kbps);
	feed->encoder = obs_video_encoder_create(id, "rastry-preview", settings, NULL);
	obs_data_release(settings);
	if (feed->encoder) {
		obs_encoder_set_video(feed->encoder, attempt->video);
		obs_encoder_set_frame_rate_divisor(feed->encoder, attempt->divisor);
		obs_output_set_video_encoder(feed->output, feed->encoder);
		if (obs_output_start(feed->output)) {
			attempt->started = id;
			attempt->hardware = hardware;
			return false;
		}
		const char *why = obs_output_get_last_error(feed->output);
		if (why)
			obs_log(LOG_INFO, "preview: %s did not start: %s", id, why);
		obs_output_set_video_encoder(feed->output, NULL);
		obs_encoder_release(feed->encoder);
		feed->encoder = NULL;
	}
	if (attempt->failures.len)
		dstr_cat(&attempt->failures, ", ");
	dstr_cat(&attempt->failures, id);
	return true;
}

static void teardown(struct feed *feed)
{
	if (feed->output) {
		obs_output_force_stop(feed->output);
		obs_output_release(feed->output);
	}
	if (feed->encoder) {
		obs_encoder_set_video(feed->encoder, NULL);
		obs_encoder_release(feed->encoder);
	}
	if (feed->view) {
		obs_view_remove(feed->view);
		obs_view_set_source(feed->view, 0, NULL);
		obs_view_destroy(feed->view);
	}
	if (feed->scene)
		obs_source_release(feed->scene);
	feed->output = NULL;
	feed->encoder = NULL;
	feed->view = NULL;
	feed->scene = NULL;
}

static void feed_free(struct feed *feed)
{
	while (feed->head) {
		struct record *next = feed->head->next;
		bfree(feed->head);
		feed->head = next;
	}
	if (feed->pipe != INVALID_HANDLE_VALUE)
		CloseHandle(feed->pipe);
	if (feed->wake)
		CloseHandle(feed->wake);
	if (feed->done)
		CloseHandle(feed->done);
	DeleteCriticalSection(&feed->lock);
	bfree(feed);
}

// Waits for RastryControl to open the pipe, no longer than the time limit
static bool wait_for_reader(struct feed *feed)
{
	OVERLAPPED overlapped = {0};
	overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	bool connected = false;
	if (ConnectNamedPipe(feed->pipe, &overlapped)) {
		connected = true;
	} else if (GetLastError() == ERROR_PIPE_CONNECTED) {
		connected = true;
	} else if (GetLastError() == ERROR_IO_PENDING) {
		HANDLE waits[2] = {overlapped.hEvent, feed->done};
		DWORD result = WaitForMultipleObjects(2, waits, FALSE, CONNECT_TIMEOUT_MS);
		if (result == WAIT_OBJECT_0) {
			connected = true;
		} else {
			CancelIoEx(feed->pipe, &overlapped);
			DWORD ignored;
			GetOverlappedResult(feed->pipe, &overlapped, &ignored, TRUE);
		}
	}
	CloseHandle(overlapped.hEvent);
	return connected;
}

static bool write_all(struct feed *feed, const uint8_t *bytes, size_t size)
{
	OVERLAPPED overlapped = {0};
	overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	bool ok = false;
	if (WriteFile(feed->pipe, bytes, (DWORD)size, NULL, &overlapped) || GetLastError() == ERROR_IO_PENDING) {
		HANDLE waits[2] = {overlapped.hEvent, feed->done};
		DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
		DWORD written = 0;
		if (result == WAIT_OBJECT_0) {
			ok = GetOverlappedResult(feed->pipe, &overlapped, &written, FALSE) && written == size;
		} else {
			CancelIoEx(feed->pipe, &overlapped);
			GetOverlappedResult(feed->pipe, &overlapped, &written, TRUE);
		}
	}
	CloseHandle(overlapped.hEvent);
	return ok;
}

static void forget(struct feed *feed)
{
	EnterCriticalSection(&feeds_lock);
	for (struct feed **at = &feeds; *at; at = &(*at)->next) {
		if (*at == feed) {
			*at = feed->next;
			break;
		}
	}
	LeaveCriticalSection(&feeds_lock);
}

// The feed's life: it owns everything, so a stop from anywhere just asks it to end
static DWORD WINAPI feed_thread(void *data)
{
	struct feed *feed = data;
	if (wait_for_reader(feed)) {
		HANDLE waits[2] = {feed->wake, feed->done};
		while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0) {
			bool alive = true;
			for (;;) {
				EnterCriticalSection(&feed->lock);
				struct record *record = feed->head;
				if (record) {
					feed->head = record->next;
					if (!feed->head)
						feed->tail = NULL;
				}
				LeaveCriticalSection(&feed->lock);
				if (!record)
					break;
				alive = write_all(feed, record->bytes, record->size);
				EnterCriticalSection(&feed->lock);
				feed->queued -= record->size;
				LeaveCriticalSection(&feed->lock);
				bfree(record);
				if (!alive)
					break;
			}
			if (!alive)
				break;
		}
	} else {
		obs_log(LOG_INFO, "preview: feed %lld was never opened", feed->id);
	}
	forget(feed);
	teardown(feed);
	obs_log(LOG_INFO, "preview: feed %lld stopped", feed->id);
	HANDLE thread = feed->thread;
	feed_free(feed);
	CloseHandle(thread);
	return 0;
}

// Only the Windows user running OBS, and only from this PC
static bool create_pipe(struct feed *feed)
{
	uint8_t random[16];
	if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		return false;
	char hex[33];
	for (size_t i = 0; i < sizeof(random); i++)
		snprintf(hex + i * 2, 3, "%02x", random[i]);
	snprintf(feed->pipe_name, sizeof(feed->pipe_name), "\\\\.\\pipe\\rastry-preview-%s", hex);

	SECURITY_ATTRIBUTES security = {sizeof(security), NULL, FALSE};
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorA("D:P(A;;GA;;;OW)", SDDL_REVISION_1,
								  &security.lpSecurityDescriptor, NULL))
		return false;
	feed->pipe = CreateNamedPipeA(feed->pipe_name,
				      PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
				      PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1024 * 1024, 0, 0,
				      &security);
	LocalFree(security.lpSecurityDescriptor);
	return feed->pipe != INVALID_HANDLE_VALUE;
}

static void fail(obs_data_t *response, const char *error)
{
	obs_log(LOG_INFO, "preview: %s", error);
	obs_data_set_bool(response, "ok", false);
	obs_data_set_string(response, "error", error);
}

static size_t feed_count(void)
{
	size_t count = 0;
	EnterCriticalSection(&feeds_lock);
	for (struct feed *feed = feeds; feed; feed = feed->next)
		count++;
	LeaveCriticalSection(&feeds_lock);
	return count;
}

void rastry_start_preview_feed(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	const char *scene_name = obs_data_get_string(request, "scene");
	long long width = obs_data_get_int(request, "width");
	long long fps = obs_data_get_int(request, "fps");
	long long kbps = obs_data_get_int(request, "kbps");
	if (!*scene_name || width < MIN_WIDTH || width > MAX_WIDTH || fps < 1 || fps > 60 || kbps < 100 ||
	    kbps > MAX_KBPS) {
		fail(response, "Which scene, width, frame rate or bitrate?");
		return;
	}
	if (feed_count() >= MAX_FEEDS) {
		fail(response, "Too many preview feeds at once.");
		return;
	}
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		fail(response, "OBS has no video yet.");
		return;
	}
	obs_source_t *scene = obs_get_source_by_name(scene_name);
	if (!scene || !obs_scene_from_source(scene)) {
		obs_source_release(scene);
		fail(response, "No such scene.");
		return;
	}

	struct feed *feed = bzalloc(sizeof(*feed));
	feed->pipe = INVALID_HANDLE_VALUE;
	feed->scene = scene;
	InitializeCriticalSection(&feed->lock);
	feed->wake = CreateEventA(NULL, FALSE, FALSE, NULL);
	feed->done = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (!create_pipe(feed)) {
		teardown(feed);
		feed_free(feed);
		fail(response, "The preview's pipe could not be created.");
		return;
	}

	// Its own render of the scene, at its own size: the stream and recording are not touched
	ovi.output_width = (uint32_t)width & ~1u;
	ovi.output_height = (uint32_t)(width * ovi.base_height / ovi.base_width) & ~1u;
	// OBS renders every view at its own rate; the encoder takes one frame in so many
	double canvas_fps = (double)ovi.fps_num / ovi.fps_den;
	uint32_t divisor = (uint32_t)(canvas_fps / (double)fps + 0.5);
	if (divisor < 1)
		divisor = 1;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	feed->view = obs_view_create();
	obs_view_set_source(feed->view, 0, scene);
	video_t *video = obs_view_add2(feed->view, &ovi);
	if (!video) {
		teardown(feed);
		feed_free(feed);
		fail(response, "OBS could not render the scene for the preview.");
		return;
	}

	obs_data_t *output_settings = obs_data_create();
	obs_data_set_int(output_settings, "feed", (long long)(intptr_t)feed);
	feed->output = obs_output_create(OUTPUT_ID, "rastry-preview", output_settings, NULL);
	obs_data_release(output_settings);

	struct attempt attempt = {.feed = feed, .video = video, .kbps = kbps, .divisor = divisor};
	dstr_init(&attempt.failures);
	obs_data_array_t *wanted = obs_data_get_array(request, "codecs");
	size_t wanted_count = wanted ? obs_data_array_count(wanted) : 0;
	for (size_t i = 0; i < (wanted_count ? wanted_count : 1) && !attempt.started; i++) {
		attempt.codec = "h264";
		obs_data_t *item = NULL;
		if (wanted_count) {
			item = obs_data_array_item(wanted, i);
			const char *codec = obs_data_get_string(item, "codec");
			attempt.codec = strcmp(codec, "av1") == 0 ? "av1" : "h264";
		}
		for_each_encoder(attempt.codec, try_encoder, &attempt);
		obs_data_release(item);
	}
	obs_data_array_release(wanted);

	if (!attempt.started) {
		struct dstr error = {0};
		dstr_cat(&error, "No encoder could start on this PC");
		if (attempt.failures.len) {
			dstr_cat(&error, ": ");
			dstr_cat_dstr(&error, &attempt.failures);
		}
		teardown(feed);
		feed_free(feed);
		fail(response, error.array);
		dstr_free(&error);
		dstr_free(&attempt.failures);
		return;
	}
	dstr_free(&attempt.failures);

	EnterCriticalSection(&feeds_lock);
	feed->id = next_id++;
	feed->next = feeds;
	feeds = feed;
	LeaveCriticalSection(&feeds_lock);

	obs_log(LOG_INFO, "preview: feed %lld of \"%s\" at %ux%u, %lld fps, %lld kbps with %s", feed->id, scene_name,
		ovi.output_width, ovi.output_height, fps, kbps, attempt.started);
	obs_data_set_bool(response, "ok", true);
	obs_data_set_int(response, "id", feed->id);
	obs_data_set_string(response, "pipe", feed->pipe_name);
	obs_data_set_string(response, "encoder", attempt.started);
	obs_data_set_string(response, "codec", attempt.codec);
	obs_data_set_bool(response, "hardware", attempt.hardware);
	obs_data_set_int(response, "width", ovi.output_width);
	obs_data_set_int(response, "height", ovi.output_height);
	obs_data_set_double(response, "fps", canvas_fps / divisor);
	feed->thread = CreateThread(NULL, 0, feed_thread, feed, 0, NULL);
}

void rastry_stop_preview_feed(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(data);
	long long id = obs_data_get_int(request, "id");
	bool found = false;
	EnterCriticalSection(&feeds_lock);
	for (struct feed *feed = feeds; feed; feed = feed->next) {
		if (feed->id == id) {
			SetEvent(feed->done);
			found = true;
			break;
		}
	}
	LeaveCriticalSection(&feeds_lock);
	obs_data_set_bool(response, "ok", found);
}

struct list {
	obs_data_array_t *array;
	const char *codec;
};

static bool add_encoder(const char *id, bool hardware, void *data)
{
	struct list *list = data;
	obs_data_t *item = obs_data_create();
	obs_data_set_string(item, "id", id);
	obs_data_set_string(item, "codec", list->codec);
	obs_data_set_bool(item, "hardware", hardware);
	obs_data_array_push_back(list->array, item);
	obs_data_release(item);
	return true;
}

void rastry_get_preview_encoders(obs_data_t *request, obs_data_t *response, void *data)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(data);
	struct list list = {.array = obs_data_array_create()};
	list.codec = "av1";
	for_each_encoder(list.codec, add_encoder, &list);
	list.codec = "h264";
	for_each_encoder(list.codec, add_encoder, &list);
	obs_data_set_array(response, "encoders", list.array);
	obs_data_array_release(list.array);
}

void rastry_feed_load(void)
{
	InitializeCriticalSection(&feeds_lock);
	obs_register_output(&output_info);
}

// Every feed asked to end, and given a moment to do it before OBS goes away
void rastry_feed_unload(void)
{
	EnterCriticalSection(&feeds_lock);
	for (struct feed *feed = feeds; feed; feed = feed->next)
		SetEvent(feed->done);
	LeaveCriticalSection(&feeds_lock);
	for (int i = 0; i < 100 && feed_count(); i++)
		os_sleep_ms(50);
}
