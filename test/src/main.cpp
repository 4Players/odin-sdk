
/*
 * 4Players ODIN Voice Client Example
 *
 * Usage: odin_client -r <room_id> -s <server_url> -k <access_key>
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <odin.h>
#include <odin_crypto.h>

#include "api.hpp"

#define ODIN_ACCESS_KEY_FILE "odin_access_key.txt"
#define ODIN_DEFAULT_GW_ADDR "gateway.odin.4players.io"
#define ODIN_DEFAULT_ROOM_ID "default"
#define ODIN_DEFAULT_USER_ID "My User ID"

template <class T> using OpaquePtr = std::unique_ptr<T, void (*)(T *)>;

/**
 * Custom macros that support formatting with variadic arguments.
 */
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARNING(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...)                                                      \
  do {                                                                         \
    spdlog::critical(__VA_ARGS__);                                             \
    std::exit(EXIT_FAILURE);                                                   \
  } while (0)

/**
 * Custom macro to execute the given expression (which should be a valid API
 * call from the ODIN Voice SDK and checks its result. It is intended for
 * scenarios where a failure is considered critical.
 */
#define CHECK(expr)                                                            \
  {                                                                            \
    OdinError error = expr;                                                    \
    if (error != ODIN_ERROR_SUCCESS) {                                         \
      LOG_CRITICAL(#expr " failed: {}", odin_error_get_last_error());          \
    }                                                                          \
  }

/**
 * Global namespace for application-wide variables.
 */
namespace global {
std::optional<cxxopts::ParseResult> arguments;
std::vector<ma_device_info> playback_devices;
std::vector<ma_device_info> capture_devices;
OdinVadConfig vad_effect_config = {
    .voice_activity = {.enabled = true,
                       .attack_threshold = 0.9f,
                       .release_threshold = 0.8f},
    .volume_gate = {.enabled = false,
                    .attack_threshold = -30.0,
                    .release_threshold = -40.0},
};
OdinApmConfig apm_effect_config = {
    .echo_canceller = true,
    .high_pass_filter = false,
    .transient_suppressor = false,
    .noise_suppression_level = ODIN_NOISE_SUPPRESSION_LEVEL_MODERATE,
    .gain_controller_version = ODIN_GAIN_CONTROLLER_VERSION_V2};
} // namespace global

/**
 * Helper function to set up and parse specified command-line options using
 * the `cxxopts` library. The parsed result is stored in a global variable.
 */
void init_arguments(int argc, char *argv[]) {
  cxxopts::Options options(PROJECT_NAME, PROJECT_DESCRIPTION);
  options.add_options()
      // --help
      ("h,help", "display available options")
      // --version
      ("v,version", "show version number and exit")
      // --debug
      ("d,debug", "show verbosity output")
      // --server-url <string>
      ("s,server-url", "server url",
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_GW_ADDR))
      // --room-id <string>
      ("r,room-id", "room to join",
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_ROOM_ID))
      // --password <string>
      ("p,password", "master password to enable end-to-end-encryption",
       cxxopts::value<std::string>());
  options.add_options("Authorization")
      // --bypass-gateway
      ("b,bypass-gateway", "bypass gateway and connect to sfu directly")
      // --room-token <string>
      ("t,room-token", "string to use for authorization",
       cxxopts::value<std::string>())
      // --access-key <string>
      ("k,access-key", "access key to use for local token generation",
       cxxopts::value<std::string>())
      // --user-id <string>
      ("u,user-id", "user identifier to use for local token generation",
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_USER_ID));
  options.add_options("Audio Processing")
      // --disable-vad
      ("disable-vad", "disable built-in voice activity detection effects")
      // --disable-apm
      ("disable-apm", "disable built-in audio processing module effects");
  options.add_options("Audio Device")
      // --audio-devices
      ("a,audio-devices", "show available audio devices and exit")
      // --output-device <number>
      ("output-device", "playback device to use",
       cxxopts::value<int>()->default_value("0"))
      // --output-sample-rate <number>
      ("output-sample-rate", "playback sample rate in Hz",
       cxxopts::value<int>()->default_value("48000"))
      // --output-channels <number>
      ("output-channels", "playback channel count (1-2)",
       cxxopts::value<int>()->default_value("2"))
      // --input-device <number>
      ("input-device", "capture device to use",
       cxxopts::value<int>()->default_value("0"))
      // --input-sample-rate <number>
      ("input-sample-rate", "capture sample rate in Hz",
       cxxopts::value<int>()->default_value("48000"))
      // --input-channels <number>
      ("input-channels", "capture channel count (1-2)",
       cxxopts::value<int>()->default_value("1"));

  try {
    global::arguments.emplace(options.parse(argc, argv));
  } catch (cxxopts::exceptions::exception e) {
    std::cerr << "Error: " << e.what() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (global::arguments->count("audio-devices")) {
    std::cout << "Playback Devices:" << std::endl;
    std::cout << "    0: Default" << std::endl;
    for (std::size_t i = 0; i < global::playback_devices.size(); ++i) {
      std::cout << "    " << i + 1 << ": " << global::playback_devices[i].name
                << std::endl;
    }
    std::cout << std::endl;

    std::cout << "Capture Devices:" << std::endl;
    std::cout << "    0: Default" << std::endl;
    for (std::size_t i = 0; i < global::capture_devices.size(); ++i) {
      std::cout << "    " << i + 1 << ": " << global::capture_devices[i].name
                << std::endl;
    }
    std::cout << std::endl;

    exit(EXIT_SUCCESS);
  }

  if (global::arguments->count("version")) {
    std::cout << PROJECT_NAME << " (SDK " << ODIN_VERSION ")" << std::endl;
    exit(EXIT_SUCCESS);
  }

  if (global::arguments->count("help")) {
    std::cout << options.help() << std::endl;
    exit(EXIT_SUCCESS);
  }
}

/**
 * Queries the globally stored command-line arguments to determine if the
 * specified option is present.
 */
bool has_argument(std::string name) {
  return !!global::arguments->count(name.data());
}

/**
 * Template function to fetch the value associated with the given argument
 * name from the globally parsed command-line options and convert it to the
 * specified type `T`.
 */
template <typename T> T get_argument(std::string name) {
  return (*global::arguments)[name].as<T>();
}

struct CustomEffectContext {
  uint64_t peer_id;
  bool is_silent;
};

/**
 * Custom pipeline effect callback to track peer talk status.
 */
static void custom_effect_talk_status(float *samples, uint32_t samples_count,
                                      bool *is_silent, const void *user_data) {
  auto ctx = static_cast<CustomEffectContext *>(const_cast<void *>(user_data));
  if (ctx->is_silent != *is_silent) {
    LOG_INFO("peer {} {} talking", ctx->peer_id,
             ctx->is_silent ? "started" : "stopped");
  }
  ctx->is_silent = *is_silent;
}

struct Encoder {
  OpaquePtr<OdinEncoder> ptr;
  uint32_t vad_effect_id;
  uint32_t apm_effect_id;
  CustomEffectContext ctx;
};

struct Decoder {
  OpaquePtr<OdinDecoder> ptr;
  CustomEffectContext ctx;
};

/**
 * Global application state.
 */
struct State {
  OpaquePtr<OdinRoom> room;
  OdinCipher *cipher;

  ma_device playback_device;
  ma_device capture_device;

  std::optional<Encoder> encoder;
  std::unordered_map<api::PeerId, Decoder> decoders;

  State();

  void on_room_status_changed(const std::string &status);
  void on_room_joined(const std::string &room_id, const std::string &customer,
                      api::PeerId own_peer_id);
  void on_room_left(const std::string &reason);
  void on_peer_joined(const api::PeerId peer_id, const std::string &user_id);
  void on_peer_left(const api::PeerId peer_id);

  void configure_encoder(const api::PeerId peer_id);
  void configure_decoder(const api::PeerId peer_id);

  void send_rpc(const api::client::Command);

  void start_audio_devices(int playback_device_idx,
                           int playback_device_sample_rate_hz,
                           int playback_device_channel_count,
                           int capture_device_idx,
                           int capture_device_sample_rate_hz,
                           int capture_device_channels_count);
  void stop_audio_devices();
};

/**
 * Custom audio callback invoked by the audio device for capture/playback. This
 * function is registered as the data callback for both miniaudio capture and
 * playback devices.
 */
void handle_audio_data(ma_device *device, void *output, const void *input,
                       ma_uint32 frame_count) {
  auto state = reinterpret_cast<State *>(device->pUserData);
  if (device->type == ma_device_type_capture) {
    auto input_count = frame_count * device->capture.channels;

    if (state->encoder.has_value()) {
      odin_encoder_push(state->encoder->ptr.get(),
                        reinterpret_cast<const float *>(input), input_count);
      for (;;) {
        uint8_t datagram[2048];
        uint32_t datagram_length = sizeof(datagram);
        switch (odin_encoder_pop(state->encoder->ptr.get(), datagram,
                                 &datagram_length)) {
        case ODIN_ERROR_SUCCESS:
          CHECK(odin_room_send_datagram(state->room.get(), datagram,
                                        datagram_length));
          break;
        case ODIN_ERROR_NO_DATA:
          return;
        default:
          LOG_ERROR("failed to encode audio datagram to send");
        };
      }
    }
  } else if (device->type == ma_device_type_playback) {
    auto output_count = frame_count * device->playback.channels;
    auto *output_begin = reinterpret_cast<float *>(output);
    auto output_end = output_begin + output_count;
    std::vector<float> samples(output_count);

    for (const auto &[media_id, decoder] : state->decoders) {
      odin_decoder_pop(decoder.ptr.get(), samples.data(), output_count,
                       nullptr);
      std::transform(output_begin, output_end, samples.data(), output_begin,
                     std::plus<>());
    }

    if (state->encoder.has_value() &&
        global::apm_effect_config.echo_canceller) {
      odin_pipeline_update_apm_playback(
          odin_encoder_get_pipeline(state->encoder->ptr.get()),
          state->encoder->apm_effect_id, output_begin, output_count, 10);
    }
  }
}

/**
 * Constructs a local state object and enumerates available audio devices.
 */
State::State() : room(nullptr, &odin_room_free), cipher(nullptr) {
  ma_context context;
  ma_device_info *playback_devices = nullptr;
  ma_uint32 playback_devices_count = 0;
  ma_device_info *capture_devices = nullptr;
  ma_uint32 capture_devices_count = 0;

  if (MA_SUCCESS == ma_context_init(nullptr, 0, nullptr, &context)) {
    if (MA_SUCCESS == ma_context_get_devices(
                          &context, &playback_devices, &playback_devices_count,
                          &capture_devices, &capture_devices_count)) {
      global::playback_devices.assign(
          playback_devices, playback_devices + playback_devices_count);
      global::capture_devices.assign(capture_devices,
                                     capture_devices + capture_devices_count);
    }
  }
}

/**
 * Handles room connection state changes and clears all encoders/decoders on
 * room leave.
 */
void State::on_room_status_changed(const std::string &status) {
  if (status == "joined")
    return;

  this->encoder.reset();
  this->decoders.clear();
}

/**
 * Handles successful join to a room and configures an encoder for outgoing
 * audio.
 */
void State::on_room_joined(const std::string &room_id,
                           const std::string &customer,
                           api::PeerId own_peer_id) {
  LOG_INFO("room '{}' owned by '{}' joined successfully as peer {}", room_id,
           customer, own_peer_id);

  this->configure_encoder(own_peer_id);
}

/**
 * Closes the application when a room connection was closed by the server.
 */
void State::on_room_left(const std::string &reason) {
  LOG_INFO("room left; {}", reason);
  exit(EXIT_SUCCESS);
}

/**
 * Handles a new peer joining the room. This also initializes decoders and
 * checks for crypto password mismatches.
 */
void State::on_peer_joined(const api::PeerId peer_id,
                           const std::string &user_id) {
  LOG_INFO("peer {} joined with user id '{}'", peer_id, user_id);

  if (ODIN_CRYPTO_PEER_STATUS_PASSWORD_MISSMATCH ==
      odin_crypto_get_peer_status(this->cipher, peer_id)) {
    LOG_WARNING(
        "unable to communicate with peer {}; master passwords doe not match",
        peer_id);
  }

  this->configure_decoder(peer_id);
}

/**
 * Handles a peer leaving the room. This also stops all media streams
 * associated with the peer and removes their decoders.
 */
void State::on_peer_left(const api::PeerId peer_id) {
  LOG_INFO("peer {} left", peer_id);

  this->decoders.erase(peer_id);
}

/**
 * Creates and configures an audio encoder for a specific peer. It retrieves
 * the encoder's processing pipeline and inserts built-in effects for speech
 * detection (VAD) and advanced audio processing (APM) as well as a custom
 * effect to track talk status for the local peer.
 */
void State::configure_encoder(const api::PeerId peer_id) {
  OdinEncoder *encoder;
  CHECK(odin_encoder_create(peer_id, this->capture_device.sampleRate,
                            this->capture_device.capture.channels == 2,
                            &encoder));
  const OdinPipeline *pipeline = odin_encoder_get_pipeline(encoder);

  uint32_t apm_effect_id;
  if (!has_argument("disable-apm")) {
    CHECK(odin_pipeline_insert_apm_effect(
        pipeline, odin_pipeline_get_effect_count(pipeline),
        this->playback_device.sampleRate,
        this->playback_device.playback.channels == 2, &apm_effect_id));
    CHECK(odin_pipeline_set_apm_config(pipeline, apm_effect_id,
                                       &global::apm_effect_config));
  } else {
    apm_effect_id = 0;
  }

  uint32_t vad_effect_id;
  if (!has_argument("disable-vad")) {
    CHECK(odin_pipeline_insert_vad_effect(
        pipeline, odin_pipeline_get_effect_count(pipeline), &vad_effect_id));
    CHECK(odin_pipeline_set_vad_config(pipeline, vad_effect_id,
                                       &global::vad_effect_config));
  } else {
    vad_effect_id = 0;
  }

  this->encoder.emplace(
      Encoder{OpaquePtr<OdinEncoder>(encoder, &odin_encoder_free),
              vad_effect_id,
              apm_effect_id,
              {peer_id, true}});

  odin_pipeline_insert_custom_effect(
      pipeline, odin_pipeline_get_effect_count(pipeline),
      custom_effect_talk_status, static_cast<const void *>(&this->encoder->ctx),
      nullptr);
}

/**
 * Creates and configures an audio decoder for a specific peer. It retrieves
 * the decoder's processing pipeline and inserts a custom effect to track talk
 * status for the peer.
 */
void State::configure_decoder(const api::PeerId peer_id) {
  OdinDecoder *decoder;
  CHECK(odin_decoder_create(this->playback_device.sampleRate,
                            this->playback_device.playback.channels == 2,
                            &decoder));
  const OdinPipeline *pipeline = odin_decoder_get_pipeline(decoder);

  auto [d, inserted] = this->decoders.insert(
      {peer_id, Decoder{OpaquePtr<OdinDecoder>(decoder, &odin_decoder_free),
                        {peer_id, true}}});
  assert(inserted);

  odin_pipeline_insert_custom_effect(pipeline, 0, custom_effect_talk_status,
                                     static_cast<const void *>(&d->second.ctx),
                                     nullptr);
}

/**
 * Sends a remote procedure call (RPC) command to the server. It serializes
 * the given command object to JSON, converts it to MessagePack format and
 * transmits it.
 */
void State::send_rpc(api::client::Command cmd) {
  nlohmann::json rpc = cmd;
  LOG_DEBUG("sending rpc: {}", rpc.dump());

  try {
    CHECK(odin_room_send_rpc(this->room.get(), rpc.dump().data()));
  } catch (const std::exception &e) {
    LOG_WARNING("failed to encode outgoing rpc; {}", e.what());
  }
}

/**
 * Initializes and starts the audio playback and capture devices according
 * to the provided device indices, sample rates, and channel counts. It uses
 * the global device lists to look up the desired device IDs.
 */
void State::start_audio_devices(int playback_device_idx,
                                int playback_device_sample_rate_hz,
                                int playback_device_channel_count,
                                int capture_device_idx,
                                int capture_device_sample_rate_hz,
                                int capture_device_channels_count) {
  if (global::playback_devices.size()) {
    auto config = ma_device_config_init(ma_device_type_playback);
    if (playback_device_idx > 0 &&
        playback_device_idx < global::playback_devices.size()) {
      config.playback.pDeviceID =
          &global::playback_devices[playback_device_idx - 1].id;
    }
    config.playback.format = ma_format_f32;
    config.playback.channels = std::clamp(playback_device_channel_count, 1, 2);
    config.sampleRate = playback_device_sample_rate_hz;
    config.dataCallback = handle_audio_data;
    config.pUserData = this;

    auto result = ma_device_init(nullptr, &config, &this->playback_device);
    if ((result = ma_device_start(&this->playback_device)) != MA_SUCCESS) {
      LOG_ERROR("failed to open audio playback device; {}",
                ma_result_description(result));
      ma_device_uninit(&this->playback_device);
    } else {
      LOG_INFO("using audio playback device: {}",
               this->playback_device.playback.name);
    }
  } else {
    LOG_WARNING("no audio capture device available");
  }

  if (global::capture_devices.size()) {
    auto config = ma_device_config_init(ma_device_type_capture);
    if (capture_device_idx > 0 &&
        capture_device_idx < global::capture_devices.size()) {
      config.capture.pDeviceID =
          &global::capture_devices[capture_device_idx - 1].id;
    }
    config.capture.format = ma_format_f32;
    config.capture.channels = std::clamp(capture_device_channels_count, 1, 2);
    config.sampleRate = capture_device_sample_rate_hz;
    config.dataCallback = handle_audio_data;
    config.pUserData = this;

    auto result = ma_device_init(nullptr, &config, &this->capture_device);
    if ((result = ma_device_start(&this->capture_device)) != MA_SUCCESS) {
      LOG_ERROR("failed to open audio capture device; {}",
                ma_result_description(result));
      ma_device_uninit(&this->capture_device);
    } else {
      LOG_INFO("using audio capture device: {}",
               this->capture_device.capture.name);
    }
  } else {
    LOG_WARNING("no audio capture device available");
  }
}

/**
 * Stops and uninitializes all audio devices. This is safe to call even if
 * one or both devices were never successfully initialized; in that case,
 * `ma_device_uninit` will simply be a no-op.
 */
void State::stop_audio_devices() {
  ma_device_uninit(&this->playback_device);
  ma_device_uninit(&this->capture_device);
}

/**
 * Reads an ODIN access key from the specified file if it exists.
 */
std::error_code read_access_key_file(const std::filesystem::path &path,
                                     std::string &data) {
  std::ifstream file(path, std::ios::binary);
  if (file) {
    std::ostringstream ss;
    ss << file.rdbuf();
    if (!file.good() && !file.eof()) {
      return std::make_error_code(std::errc::io_error);
    }
    data = ss.str();
  }
  return std::error_code{};
}

/**
 * Writes an ODIN access key to the specified file.
 */
std::error_code write_access_key_file(const std::filesystem::path &path,
                                      const std::string &data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return std::make_error_code(std::errc::no_such_file_or_directory);
  }
  file.write(data.data(), data.size());
  if (!file) {
    return std::make_error_code(std::errc::io_error);
  }
  return std::error_code{};
}

/**
 * Creates an `OdinTokenGenerator` instance. If the provided access key is
 * non-empty, it is used to create the token generator. Otherwise, a new
 * access key is generated (and stored in the provided string) during
 * creation.
 */
OpaquePtr<OdinTokenGenerator> get_token_generator(std::string &access_key) {
  OdinTokenGenerator *token_generator;
  if (!access_key.empty()) {
    CHECK(odin_token_generator_create(access_key.data(), &token_generator));
  } else {
    CHECK(odin_token_generator_create(nullptr, &token_generator));
    char out_access_key[128];
    uint32_t out_access_key_length = sizeof(out_access_key) - 1;
    CHECK(odin_token_generator_get_access_key(token_generator, out_access_key,
                                              &out_access_key_length));
    access_key = std::string(out_access_key, out_access_key_length);
  }

  return {token_generator, &odin_token_generator_free};
}

/**
 * Constructs a JSON payload with the audience, room ID, user ID and
 * validity timestamps, then signs it using the provided token generator to
 * produce a JWT for authentication in the ODIN network.
 */
std::string generate_token(OpaquePtr<OdinTokenGenerator> &token_generator,
                           const std::string &room_id,
                           const std::string &user_id) {
  auto nbf = time(nullptr);
  auto exp = nbf + 300; /* 5 minutes */

  nlohmann::json claims = {
      {"rid", room_id},
      {"uid", user_id},
      {"nbf", nbf},
      {"exp", exp},
  };

  if (has_argument("bypass-gateway")) {
    claims.update({
        {"adr", get_argument<std::string>("server-url")},
        {"aud", "sfu"},
        {"cid", "<no_customer>"},
    });
  }

  std::string token(1024, '\0');
  uint32_t token_length = token.size();
  CHECK(odin_token_generator_sign(token_generator.get(), claims.dump().c_str(),
                                  &token[0], &token_length));
  token.resize(token_length);

  return token;
}

/**
 * Callback invoked when a voice datagram is received from the room. This
 * function is registered with the ODIN room to handle incoming audio data.
 * It verifies the room reference, looks up the decoder for the source peer
 * and pushes the datagram into it for decoding and playback.
 */
void on_datagram(OdinRoom *room, const OdinDatagramProperties *properties,
                 const uint8_t *bytes, uint32_t bytes_length, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  assert(state->room.get() == room);
  if (auto it = state->decoders.find(properties->peer_id);
      it != state->decoders.end()) {
    CHECK(odin_decoder_push(it->second.ptr.get(), bytes, bytes_length));
  }
}

/**
 * Callback invoked when an RPC message is received from the room. This
 * function is registered with the ODIN connection pool to handle incoming RPC
 * datagrams. It verifies the room reference, deserializes the MessagePack
 * payload into JSON, converts it to a server event variant and dispatches it
 * to the appropriate handler.
 */
void on_rpc(OdinRoom *room, const char *text, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  assert(state->room.get() == room);
  try {
    nlohmann::json rpc = nlohmann::json::parse(text);
    LOG_DEBUG("received rpc: {}", rpc.dump());

    auto event = rpc.get<api::server::Event>();
    std::visit(api::visitor{
                   [state](const api::server::Joined &u) {
                     state->on_room_joined(u.room_id, u.customer,
                                           u.own_peer_id);
                   },
                   [state](const api::server::Left &u) {
                     state->on_room_left(u.reason);
                   },
                   [state](const api::server::PeerJoined &u) {
                     state->on_peer_joined(u.peer_id, u.user_id);
                   },
                   [state](const api::server::PeerLeft &u) {
                     state->on_peer_left(u.peer_id);
                   },
                   [state](const api::server::PeerChanged &u) {
                     // unused
                   },
                   [state](const api::server::NewReconnectToken &u) {
                     // unused
                   },
                   [state](const api::server::MessageReceived &u) {
                     // unused
                   },
                   [state](const api::server::RoomStatusChanged &u) { // TODO
                     state->on_room_status_changed(u.status);
                   },
                   [state](const api::server::Error &u) { // TODO
                     LOG_ERROR("server error: {}", u.message);
                   },
               },
               event);

  } catch (const std::exception &e) {
    LOG_WARNING("failed to decode incoming rpc; {}", e.what());
  }
}

/**
 * The entry point of the program.
 */
int main(int argc, char *argv[]) {
  State state;

  /**
   * Parse command-line options into globally available arguments.
   */
  init_arguments(argc, argv);

  /**
   * Create and configure a default logger instance using the
   * multi-threaded, colored sink from the `spdlog` logging library.
   */
  spdlog::set_default_logger(spdlog::stdout_color_mt(PROJECT_NAME));
  spdlog::set_pattern("[%T.%e] %n: %^%v%$");
#ifndef NDEBUG
  spdlog::set_level(spdlog::level::trace);
#else
  spdlog::set_level(has_argument("debug") ? spdlog::level::debug
                                          : spdlog::level::info);
#endif

  /**
   * Initialize the ODIN Voice runtime.
   */
  LOG_INFO("initializing ODIN Voice runtime {}", ODIN_VERSION);
  CHECK(odin_initialize(ODIN_VERSION));

  /**
   * Create an optional ODIN cipher for end-to-end-encryption and configure it
   * if a master password was specified via command-line.
   */
  OdinCipher *cipher = odin_crypto_create(ODIN_CRYPTO_VERSION);
  if (has_argument("password")) {
    auto password = get_argument<std::string>("password");
    LOG_INFO("configuring ODIN cipher with password '{}'", password);
    odin_crypto_set_password(cipher,
                             reinterpret_cast<const uint8_t *>(password.data()),
                             password.length());
  }

  /**
   * Start playback/capture audio devices.
   */
  state.start_audio_devices(get_argument<int>("output-device"),
                            get_argument<int>("output-sample-rate"),
                            get_argument<int>("output-channels"),
                            get_argument<int>("input-device"),
                            get_argument<int>("input-sample-rate"),
                            get_argument<int>("input-channels"));

  /**
   * Grab command-line arguments.
   */
  auto room_id = get_argument<std::string>("room-id");
  auto user_id = get_argument<std::string>("user-id");
  auto gateway = get_argument<std::string>("server-url");

  /**
   * Generate a room token (JWT) to authenticate and join an ODIN room.
   *
   * ====== IMPORTANT ======
   * Token generation should always be done on the server side, to prevent
   * sensitive information from being leaked to unauthorized parties. This
   * functionality is provided in this client for testing and demonstration
   * purposes only.
   *
   * Your access key is the unique authentication key to be used to generate
   * room tokens for accessing the ODIN server network. Think of it as your
   * individual username and password combination all wrapped up into a
   * single non-comprehendible string of characters, and treat it with the
   * same respect.
   *
   * ======== TL;DR ========
   * Production code should NEVER EVER generate tokens for authentication or
   * ship your access key on the client side!
   */
  std::string room_token;
  if (!has_argument("room-token")) {
    std::string access_key;
    if (!has_argument("access-key")) {
      if (auto e = read_access_key_file(ODIN_ACCESS_KEY_FILE, access_key); e) {
        LOG_WARNING("failed to read existing access key from '{}'; {}",
                    ODIN_ACCESS_KEY_FILE, e.message());
      }
    } else {
      access_key = get_argument<std::string>("access-key");
    }

    auto token_generator = get_token_generator(access_key);
    if (auto e = write_access_key_file(ODIN_ACCESS_KEY_FILE, access_key); e) {
      LOG_WARNING("failed to write access key to '{}'; {}",
                  ODIN_ACCESS_KEY_FILE, e.message());
    }
    LOG_DEBUG("using access key: {}", access_key);

    room_token = generate_token(token_generator, room_id, user_id);
    token_generator.reset();
  } else {
    room_token = get_argument<std::string>("room-token");
  }
  LOG_DEBUG("using room token: {}", room_token);

  /**
   * Build custom authentication string.
   */
  nlohmann::json authentication = nlohmann::json::object({
      // mandatory room token
      {"token", room_token},
      // optional room id in caase the token contains multiple room ids
      {"room_id", room_id},
      // optional list of channel masks
      {"channel_masks",
       {
           // only want to hear peer 1 on channels 1, 3 and 5 (000...00010101)
           {1, 0x15},
       }},
      // optional peer user data
      {"user_data", {{"foo", "bar"}, {"time", std::time(0)}}},
  });

  /*
   * Create a new ODIN room pointer and establish an encrypted connection to
   * the ODIN network using the given cipher and join the specified room.
   */
  OdinRoom *room;
  OdinRoomEvents events{
      .on_datagram = &on_datagram,
      .on_rpc = &on_rpc,
      .user_data = reinterpret_cast<void *>(&state),
  };
  CHECK(odin_room_create(gateway.data(), authentication.dump().data(), &events,
                         cipher, &room));
  state.room = {room, odin_room_free};
  state.cipher = cipher;

  /**
   * Wait for user input.
   */
  std::cout << "--- Press RETURN to leave room and exit ---" << std::endl;
  getchar();

  /**
   * Stop playback/capture audio devices.
   */
  state.stop_audio_devices();

  /**
   * Disconnect from the room.
   */
  LOG_INFO("leaving room and closing connection to server");
  odin_room_close(room);

  /*`
   * Shutdown the ODIN Voice runtime.
   */
  odin_shutdown();

  return EXIT_SUCCESS;
}
