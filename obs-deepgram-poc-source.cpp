#include <obs-module.h>

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>

#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "WebsocketEndpoint.hpp"

// TODO: turn this into a class/object - the aja plugin is a nice reference
struct deepgram_source_data {
	obs_source_t *context;
	obs_source_t *audio_source;
	WebsocketEndpoint *endpoint;
	int endpoint_id;
	char *audio_source_name;
};

static const char *deepgram_source_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Deepgram Source");
}

int16_t f32_to_i16(float f) {
	f = f * 32768;
	if (f > 32767) {
		return 32767;
	}
	if (f < -32768) {
		return -32768;
	}
	return (int16_t) f;
}

static void audio_capture(void *param, obs_source_t *source,
			      const struct audio_data *audio_data, bool muted)
{
    //blog(LOG_WARNING, "Running audio_capture.");

	struct deepgram_source_data *dgsd = (deepgram_source_data *) param;
	UNUSED_PARAMETER(source);

    if (audio_data != NULL) {
		if (dgsd->endpoint != NULL) {
			// this is assuming I guess floats for samples, but I'll need to check our audio format
			uint16_t *i16_audio = (uint16_t *) malloc(audio_data->frames * sizeof(float) / 2);
			for (int i = 0; i < audio_data->frames; i++) {
				float sample_float;
				memcpy(&sample_float, &audio_data->data[0][0 + i * sizeof(float)], sizeof(float));
				i16_audio[i] = f32_to_i16(sample_float);
			}
			dgsd->endpoint->send_binary(dgsd->endpoint_id, i16_audio, audio_data->frames * sizeof(float) / 2);

			// the following is for reference - I'm going to want to deal with num_channels
			/*
			for (size_t i = 0; i < cd->num_channels; i++) {
				circlebuf_push_back(&cd->sidechain_data[i],
					    audio_data->data[i],
					    audio_data->frames * sizeof(float));
			}
			*/
		}
    }
}

static void deepgram_source_update(void *data, obs_data_t *settings)
{
	blog(LOG_WARNING, "Running deepgram_source_update.");

    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;
	const char *audio_source_name = obs_data_get_string(settings, "audio_source_name");

    if (strcmp(dgsd->audio_source_name, audio_source_name) != 0) {
        if (dgsd->endpoint != NULL) {
            obs_source_remove_audio_capture_callback(dgsd->audio_source, audio_capture, dgsd);
            blog(LOG_WARNING, "Going to delete the websocket connection.");
            delete dgsd->endpoint;
        }
        dgsd->audio_source_name = bstrdup(audio_source_name);
        if (strcmp(audio_source_name, "none") != 0) {
			WebsocketEndpoint *endpoint = new WebsocketEndpoint();
			blog(LOG_WARNING, "About to try to connect.");
			int id = endpoint->connect("wss://api.deepgram.com/v1/listen?encoding=linear16&sample_rate=44100&channels=1");
			blog(LOG_WARNING, "I think we connected?");
			dgsd->endpoint = endpoint;
			dgsd->endpoint_id = id;

            obs_source_t *audio_source = obs_get_source_by_name(audio_source_name);
            dgsd->audio_source = audio_source;
			blog(LOG_WARNING, "About to register the audio callback.");
            obs_source_add_audio_capture_callback(audio_source, audio_capture, dgsd);
			blog(LOG_WARNING, "Audio callback should have been registered now");
        } else {
			dgsd->endpoint = NULL;
		}
    }
}

static void *deepgram_source_create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_WARNING, "Running deepgram_source_create.");

	struct deepgram_source_data *dgsd = (deepgram_source_data *) bzalloc(sizeof(struct deepgram_source_data));
	dgsd->context = source;
    dgsd->audio_source_name = "none";
	dgsd->endpoint = NULL;
	deepgram_source_update(dgsd, settings);

    //audio_output_get_info(obs_get_audio()).format;
    //audio_output_get_sample_rate(obs_get_audio());
    //audio_output_get_channels(obs_get_audio());

    return(dgsd);
}

static void deepgram_source_destroy(void *data)
{
	blog(LOG_WARNING, "Running deepgram_source_destroy.");

    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;

    if (dgsd->endpoint != NULL) {
        obs_source_remove_audio_capture_callback(dgsd->audio_source, audio_capture, dgsd);
        blog(LOG_WARNING, "Going to delete the websocket connection.");
        delete dgsd->endpoint;
    }

    //delete dgsd->endpoint;
    //bfree(dgsd->context);
    //bfree(dgsd->audio_source_name);
	//bfree(dgsd);
}

static void deepgram_source_render(void *data, gs_effect_t *effect)
{
}

static uint32_t deepgram_source_width(void *data)
{
	return 100;
}

static uint32_t deepgram_source_height(void *data)
{
	return 100;
}

struct sources_and_parent {
	obs_property_t *sources;
	obs_source_t *parent;
};

static bool add_sources(void *data, obs_source_t *source)
{
	struct sources_and_parent *info = (sources_and_parent *) data;
	uint32_t caps = obs_source_get_output_flags(source);

	if (source == info->parent)
		return true;
	if ((caps & OBS_SOURCE_AUDIO) == 0)
		return true;

	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(info->sources, name, name);
	blog(LOG_WARNING, name);

	return true;
}

static obs_properties_t *deepgram_source_properties(void *data)
{
	blog(LOG_WARNING, "Running deepgram_source_properties.");

    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;
	obs_properties_t *properties = obs_properties_create();
	obs_source_t *parent = NULL;
	obs_property_t *property;

	obs_property_t *sources = obs_properties_add_list(
		properties, "audio_source_name", obs_module_text("Device"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(sources, obs_module_text("None"), "none");
	struct sources_and_parent info = {sources, parent};
	obs_enum_sources(add_sources, &info);

	UNUSED_PARAMETER(data);
	return properties;
}

static void deepgram_source_defaults(obs_data_t *settings)
{
	blog(LOG_WARNING, "Running deepgram_defaults.");
	obs_data_set_default_string(settings, "audio_source_name", "none");
}

struct obs_source_info deepgram_source = {
	.id = "deepgram_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = deepgram_source_name,
	.create = deepgram_source_create,
	.destroy = deepgram_source_destroy,
	.update = deepgram_source_update,
    .video_render = deepgram_source_render,
    .get_width = deepgram_source_width,
    .get_height = deepgram_source_height,
	.get_defaults = deepgram_source_defaults,
	.get_properties = deepgram_source_properties
};
