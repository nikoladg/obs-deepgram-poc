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

typedef websocketpp::client<websocketpp::config::asio_tls_client> client;
typedef std::shared_ptr<boost::asio::ssl::context> context_ptr;

class connection_metadata {
public:
    typedef websocketpp::lib::shared_ptr<connection_metadata> ptr;

    connection_metadata(int id, websocketpp::connection_hdl hdl, std::string uri)
      : m_id(id)
      , m_hdl(hdl)
      , m_status("Connecting")
      , m_uri(uri)
      , m_server("N/A")
    {}

    void on_open(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Open";

        client::connection_ptr con = c->get_con_from_hdl(hdl);
        m_server = con->get_response_header("Server");
    }

    void on_fail(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Failed";

        client::connection_ptr con = c->get_con_from_hdl(hdl);
        m_server = con->get_response_header("Server");
        m_error_reason = con->get_ec().message();
    }
    
    void on_close(client * c, websocketpp::connection_hdl hdl) {
        m_status = "Closed";
        client::connection_ptr con = c->get_con_from_hdl(hdl);
        std::stringstream s;
        s << "close code: " << con->get_remote_close_code() << " (" 
          << websocketpp::close::status::get_string(con->get_remote_close_code()) 
          << "), close reason: " << con->get_remote_close_reason();
        m_error_reason = s.str();
    }

    void on_message(websocketpp::connection_hdl, client::message_ptr msg) {
        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
    		blog(LOG_WARNING, "%s", msg->get_payload().c_str());
            m_messages.push_back("<< " + msg->get_payload());
        } else {
            m_messages.push_back("<< " + websocketpp::utility::to_hex(msg->get_payload()));
        }
    }

    websocketpp::connection_hdl get_hdl() const {
        return m_hdl;
    }
    
    int get_id() const {
        return m_id;
    }
    
    std::string get_status() const {
        return m_status;
    }

    void record_sent_message(std::string message) {
        m_messages.push_back(">> " + message);
    }

    friend std::ostream & operator<< (std::ostream & out, connection_metadata const & data);
private:
    int m_id;
    websocketpp::connection_hdl m_hdl;
    std::string m_status;
    std::string m_uri;
    std::string m_server;
    std::string m_error_reason;
    std::vector<std::string> m_messages;
};

std::ostream & operator<< (std::ostream & out, connection_metadata const & data) {
    out << "> URI: " << data.m_uri << "\n"
        << "> Status: " << data.m_status << "\n"
        << "> Remote Server: " << (data.m_server.empty() ? "None Specified" : data.m_server) << "\n"
        << "> Error/close reason: " << (data.m_error_reason.empty() ? "N/A" : data.m_error_reason) << "\n";
    out << "> Messages Processed: (" << data.m_messages.size() << ") \n";

    std::vector<std::string>::const_iterator it;
    for (it = data.m_messages.begin(); it != data.m_messages.end(); ++it) {
        out << *it << "\n";
    }

    return out;
}

class websocket_endpoint {
public:
    websocket_endpoint () : m_next_id(0) {
        m_endpoint.clear_access_channels(websocketpp::log::alevel::all);
        m_endpoint.clear_error_channels(websocketpp::log::elevel::all);

        m_endpoint.init_asio();
		m_endpoint.set_tls_init_handler(bind(&on_tls_init));
        m_endpoint.start_perpetual();

        m_thread = websocketpp::lib::make_shared<websocketpp::lib::thread>(&client::run, &m_endpoint);
    }

    ~websocket_endpoint() {
        m_endpoint.stop_perpetual();
        
        for (con_list::const_iterator it = m_connection_list.begin(); it != m_connection_list.end(); ++it) {
            if (it->second->get_status() != "Open") {
                // Only close open connections
                continue;
            }
            
            std::cout << "> Closing connection " << it->second->get_id() << std::endl;
            
            websocketpp::lib::error_code ec;
            m_endpoint.close(it->second->get_hdl(), websocketpp::close::status::going_away, "", ec);
            if (ec) {
                std::cout << "> Error closing connection " << it->second->get_id() << ": "  
                          << ec.message() << std::endl;
            }
        }
        
        m_thread->join();
    }

static context_ptr on_tls_init() {
    // establishes a SSL connection
    context_ptr ctx = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);

    try {
        ctx->set_options(boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv2 |
                         boost::asio::ssl::context::no_sslv3 |
                         boost::asio::ssl::context::single_dh_use);
    } catch (std::exception &e) {
        std::cout << "Error in context pointer: " << e.what() << std::endl;
    }
    return ctx;
}

    int connect(std::string const & uri) {
        websocketpp::lib::error_code ec;
		blog(LOG_WARNING, "About to get connection.");
        client::connection_ptr con = m_endpoint.get_connection(uri, ec);
        if (ec) {
            std::cout << "> Connect initialization error: " << ec.message() << std::endl;
            return -1;
        }

		// my guess is just I guess to do this later, right before the connect
		bool test = con->is_server();
		if (test) {
			blog(LOG_WARNING, "is server");
		} else {
			blog(LOG_WARNING, "is not server");
		}
		blog(LOG_WARNING, "About to try to append some header...");
		con->replace_header("Authorization", "Token 1eb45f404f76787f1eb8eb440505982264744776");
		blog(LOG_WARNING, "Successfully appended header, I guess.");

        int new_id = m_next_id++;
        connection_metadata::ptr metadata_ptr = websocketpp::lib::make_shared<connection_metadata>(new_id, con->get_handle(), uri);
        m_connection_list[new_id] = metadata_ptr;

        con->set_open_handler(websocketpp::lib::bind(
            &connection_metadata::on_open,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));
        con->set_fail_handler(websocketpp::lib::bind(
            &connection_metadata::on_fail,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));
        con->set_close_handler(websocketpp::lib::bind(
            &connection_metadata::on_close,
            metadata_ptr,
            &m_endpoint,
            websocketpp::lib::placeholders::_1
        ));
        con->set_message_handler(websocketpp::lib::bind(
            &connection_metadata::on_message,
            metadata_ptr,
            websocketpp::lib::placeholders::_1,
            websocketpp::lib::placeholders::_2
        ));

		blog(LOG_WARNING, "About to call the real inner connect function.");
        m_endpoint.connect(con);
		blog(LOG_WARNING, "Done calling the real inner connect function.");

        return new_id;
    }

    void close(int id, websocketpp::close::status::value code, std::string reason) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.close(metadata_it->second->get_hdl(), code, reason, ec);
        if (ec) {
            std::cout << "> Error initiating close: " << ec.message() << std::endl;
        }
    }

    void send_text(int id, std::string message) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.send(metadata_it->second->get_hdl(), message, websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cout << "> Error sending message: " << ec.message() << std::endl;
            return;
        }
        
        metadata_it->second->record_sent_message(message);
    }

    void send_binary(int id, void const * message, size_t len) {
        websocketpp::lib::error_code ec;
        
        con_list::iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            std::cout << "> No connection found with id " << id << std::endl;
            return;
        }
        
        m_endpoint.send(metadata_it->second->get_hdl(), message, len, websocketpp::frame::opcode::binary, ec);
        if (ec) {
            std::cout << "> Error sending message: " << ec.message() << std::endl;
            return;
        }
        
        metadata_it->second->record_sent_message("binary");
    }

    connection_metadata::ptr get_metadata(int id) const {
        con_list::const_iterator metadata_it = m_connection_list.find(id);
        if (metadata_it == m_connection_list.end()) {
            return connection_metadata::ptr();
        } else {
            return metadata_it->second;
        }
    }
private:
    typedef std::map<int,connection_metadata::ptr> con_list;

    client m_endpoint;
    websocketpp::lib::shared_ptr<websocketpp::lib::thread> m_thread;

    con_list m_connection_list;
    int m_next_id;
};

struct deepgram_source_data {
	obs_source_t *context;
	websocket_endpoint *endpoint;
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
        dgsd->audio_source_name = bstrdup(audio_source_name);
        if (strcmp(audio_source_name, "none") != 0) {
			websocket_endpoint *endpoint = new websocket_endpoint();
			blog(LOG_WARNING, "About to try to connect.");
			int id = endpoint->connect("wss://api.deepgram.com/v1/listen?encoding=linear16&sample_rate=44100&channels=1");
			blog(LOG_WARNING, "I think we connected?");
			dgsd->endpoint = endpoint;
			dgsd->endpoint_id = id;

            obs_source_t *audio_source = obs_get_source_by_name(audio_source_name);
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
	//bfree(dgsd->audio_source_name);
	//bfree(dgsd->ws_client);
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
