/*
 * 4Players ODIN Voice Client Example
 *
 * Usage: odin_client -r <room_id> -s <server_url> -k <access_key>
 */

#include <algorithm>
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
#define ODIN_DEFAULT_USER_DATA "{\"name\":\"Console Client\"}"

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
      // --server-url <string>
      ("s,server-url", "server url",
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_GW_ADDR))
      // --room-id <string>
      ("r,room-id", "room to join",
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_ROOM_ID))
      // --password <string>
      ("p,password", "master password to enable end-to-end-encryption",
       cxxopts::value<std::string>())
      // --peer-user-data <string>
      ("d,peer-user-data", "peer user data to set when joining the room",
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_USER_DATA));
  options.add_options("Authorization")
      // --bypass-gateway
      ("b,bypass-gateway", "bypass gateway and connect to sfu directly")
      // --room-token <string>
      ("t,room-token", "room token to use for authorization",
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
  uint16_t media_id;
  bool is_silent;
};

/**
 * Custom pipeline effect callback to track peer talk status.
 */
static void custom_effect_talk_status(float *samples, uint32_t samples_count,
                                      bool *is_silent, const void *user_data) {
  auto ctx = static_cast<CustomEffectContext *>(const_cast<void *>(user_data));
  if (ctx->is_silent != *is_silent) {
    LOG_INFO("peer {} {} talking on media {}", ctx->peer_id,
             ctx->is_silent ? "started" : "stopped", ctx->media_id);
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

  std::unordered_map<uint16_t, Encoder> encoders;
  std::unordered_map<uint16_t, Decoder> decoders;
  std::unordered_map<uint64_t, std::unordered_set<uint16_t>> peer_medias;

  State();

  void handle_room_status_changes(const api::server::RoomStatusChanged event);
  void handle_room_updates(const api::server::RoomUpdated event);
  void handle_peer_updates(const api::server::PeerUpdated event);

  void on_room_joined(const api::Room room, const uint64_t own_peer_id,
                      const std::vector<uint16_t> own_media_ids);
  void on_room_left(const std::string &reason);
  void on_peer_joined(const api::Peer peer);
  void on_peer_left(const uint64_t peer_id);
  void on_media_started(const api::Media media, const uint64_t peer_id);
  void on_media_stopped(const uint16_t media_id, const uint64_t peer_id);

  void configure_encoder(const uint64_t peer_id, const uint16_t media_id);
  void configure_decoder(const uint64_t peer_id, const uint16_t media_id);

  void send_rpc(api::client::Command);

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

    for (const auto &[media_id, encoder] : state->encoders) {
      odin_encoder_push(encoder.ptr.get(),
                        reinterpret_cast<const float *>(input), input_count);
      for (;;) {
        uint8_t datagram[2048];
        uint32_t datagram_length = sizeof(datagram);
        switch (odin_encoder_pop(encoder.ptr.get(), &media_id, 1, datagram,
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

    if (global::apm_effect_config.echo_canceller) {
      for (const auto &[media_id, encoder] : state->encoders) {
        odin_pipeline_update_apm_playback(
            odin_encoder_get_pipeline(encoder.ptr.get()), encoder.apm_effect_id,
            output_begin, output_count, 10);
      }
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
 * Handles room connection state changes and clears all encoder/decoder and
 * peer-media state on room leave.
 */
void State::handle_room_status_changes(
    const api::server::RoomStatusChanged event) {
  if (event.status == "Joined")
    return;

  this->encoders.clear();
  this->decoders.clear();
  this->peer_medias.clear();
}

/**
 * Dispatches and handles a batch of room-related server events.
 */
void State::handle_room_updates(const api::server::RoomUpdated event) {
  for (const auto &update : event.updates) {
    std::visit(api::visitor{
                   [&](const api::server::RoomUpdateJoined &u) {
                     this->on_room_joined(u.room, u.own_peer_id, u.media_ids);
                   },
                   [&](const api::server::RoomUpdateLeft &u) {
                     this->on_room_left(u.reason);
                   },
                   [&](const api::server::RoomUpdatePeerJoined &u) {
                     this->on_peer_joined(u.peer);
                   },
                   [&](const api::server::RoomUpdatePeerLeft &u) {
                     this->on_peer_left(u.peer_id);
                   },
                   [&](const api::server::RoomUpdateUserDataChanged &u) {
                     // unused
                   },
               },

               update);
  }
}

/**
 * Dispatches and handles individual peer-related server events.
 */
void State::handle_peer_updates(const api::server::PeerUpdated event) {
  std::visit(api::visitor{
                 [&](const api::server::PeerUpdateMediaStarted &u) {
                   this->on_media_started(u.media, u.peer_id);
                 },
                 [&](const api::server::PeerUpdateMediaStopped &u) {
                   this->on_media_stopped(u.media_id, u.peer_id);
                 },
                 [&](const api::server::PeerUpdateUserDataChanged &u) {
                   // unused
                 },
                 [&](const api::server::PeerUpdateTagsChanged &u) {
                   // unused
                 },
             },
             event);
}

/**
 * Handles successful join to a room, processes existing peers, notifies the
 * server to start the local media stream and configures an encoder for
 * outgoing audio.
 */
void State::on_room_joined(const api::Room room, const uint64_t own_peer_id,
                           const std::vector<uint16_t> own_media_ids) {
  uint32_t room_name_length = 256;
  std::string room_name(room_name_length, '\0');
  CHECK(odin_room_get_name(this->room.get(), &room_name[0], &room_name_length));
  room_name.resize(room_name_length);

  for (const auto &peer : room.peers) {
    this->on_peer_joined(peer);
  }
  LOG_INFO("room '{}' owned by '{}' joined successfully as peer {}", room_name,
           room.customer, own_peer_id);

  auto media_id = own_media_ids.front();
  this->send_rpc(api::client::StartMedia{media_id, {{"kind", "audio"}}});

  this->configure_encoder(own_peer_id, media_id);
}

void State::on_room_left(const std::string &reason) {
  LOG_INFO("room left; {}", reason);
  exit(EXIT_SUCCESS);
}

/**
 * Handles a new peer joining the room. This also initializes decoders for
 * any active media streams they already have and checks for crypto password
 * mismatches.
 */
void State::on_peer_joined(const api::Peer peer) {
  LOG_INFO("peer {} joined with user id '{}'", peer.id, peer.user_id);

  for (const auto &media : peer.medias) {
    this->on_media_started(media, peer.id);
  }

  if (ODIN_CRYPTO_PEER_STATUS_PASSWORD_MISSMATCH ==
      odin_crypto_get_peer_status(this->cipher, peer.id)) {
    LOG_WARNING(
        "unable to communicate with peer {}; master passwords doe not match",
        peer.id);
  }
}

/**
 * Handles a peer leaving the room. This also stops all media streams associated
 * with the peer and removes their decoders.
 */
void State::on_peer_left(const uint64_t peer_id) {
  LOG_INFO("peer {} left", peer_id);

  const auto peer = this->peer_medias.find(peer_id);
  if (peer == this->peer_medias.end())
    return;
  const auto &media_ids = peer->second;
  while (media_ids.begin() != media_ids.end()) {
    this->on_media_stopped(*media_ids.begin(), peer_id);
  }

  this->peer_medias.erase(peer);
}

/**
 * Handles the start of a media stream from a remote peer.
 */
void State::on_media_started(const api::Media media, const uint64_t peer_id) {
  LOG_INFO("media {} started by peer {}", media.id, peer_id);

  this->configure_decoder(peer_id, media.id);

  auto [peer, _] = this->peer_medias.insert({peer_id, {}});
  peer->second.insert(media.id);
}

/**
 * Handles the stop of a media stream previously started by a remote peer.
 */
void State::on_media_stopped(const uint16_t media_id, const uint64_t peer_id) {
  LOG_INFO("media {} stopped by peer {}", media_id, peer_id);

  this->decoders.erase(media_id);
  this->peer_medias.find(peer_id)->second.erase(media_id);
}

/**
 * Creates and configures an audio encoder for a specific peer and media stream.
 * It retrieves the encoder's processing pipeline and inserts built-in effects
 * for speech detection (VAD) and advanced audio processing (APM) as well as a
 * custom effect to track talk status for the local peer.
 */
void State::configure_encoder(const uint64_t peer_id, const uint16_t media_id) {
  OdinEncoder *encoder;
  CHECK(odin_encoder_create(this->capture_device.sampleRate,
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

  auto [e, inserted] = this->encoders.insert(
      {media_id, Encoder{OpaquePtr<OdinEncoder>(encoder, &odin_encoder_free),
                         vad_effect_id,
                         apm_effect_id,
                         {peer_id, media_id, true}}});
  assert(inserted);

  odin_pipeline_insert_custom_effect(
      pipeline, odin_pipeline_get_effect_count(pipeline),
      custom_effect_talk_status, static_cast<const void *>(&e->second.ctx),
      nullptr);
}

/**
 * Creates and configures an audio decoder for a specific peer and media stream.
 * It retrieves the decoder's processing pipeline and inserts a custom effect to
 * track talk status for the peer.
 */
void State::configure_decoder(const uint64_t peer_id, const uint16_t media_id) {
  OdinDecoder *decoder;
  CHECK(odin_decoder_create(media_id, this->playback_device.sampleRate,
                            this->playback_device.playback.channels == 2,
                            &decoder));
  const OdinPipeline *pipeline = odin_decoder_get_pipeline(decoder);

  auto [d, inserted] = this->decoders.insert(
      {media_id, Decoder{OpaquePtr<OdinDecoder>(decoder, &odin_decoder_free),
                         {peer_id, media_id, true}}});
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
    auto bytes = nlohmann::json::to_msgpack(rpc);
    CHECK(odin_room_send_rpc(this->room.get(), bytes.data(), bytes.size()));
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
                           const std::string &audience,
                           const std::string &room_id,
                           const std::string &user_id) {
  nlohmann::json claims;
  auto nbf = time(nullptr);
  auto exp = nbf + 300; /* 5 minutes */

  claims["rid"] = room_id;
  claims["uid"] = user_id;
  claims["aud"] = audience;
  claims["nbf"] = nbf;
  claims["exp"] = exp;

  std::string token(1024, '\0');
  uint32_t token_length = token.size();
  CHECK(odin_token_generator_sign(token_generator.get(), claims.dump().c_str(),
                                  &token[0], &token_length));
  token.resize(token_length);

  return token;
}

void on_datagram(uint64_t room_ref, uint16_t media_id, const uint8_t *bytes,
                 uint32_t bytes_length, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  assert(odin_room_get_ref(state->room.get()) == room_ref);
  if (auto it = state->decoders.find(media_id); it != state->decoders.end()) {
    CHECK(odin_decoder_push(it->second.ptr.get(), bytes, bytes_length));
  }
}

/**
 * Callback invoked when an RPC message is received from the room. This function
 * is registered with the ODIN connection pool to handle incoming RPC datagrams.
 * It verifies the room reference, deserializes the MessagePack payload into
 * JSON, converts it to a server event variant and dispatches it to the
 * appropriate handler.
 */
void on_rpc(uint64_t room_ref, const uint8_t *bytes, uint32_t bytes_length,
            void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  assert(odin_room_get_ref(state->room.get()) == room_ref);
  try {
    nlohmann::json rpc =
        nlohmann::json::from_msgpack(bytes, bytes + bytes_length);
    LOG_DEBUG("received rpc: {}", rpc.dump());

    auto event = rpc.get<api::server::Event>();
    std::visit(api::visitor{
                   [state](const api::server::RoomUpdated &u) {
                     state->handle_room_updates(u);
                   },
                   [state](const api::server::PeerUpdated &u) {
                     state->handle_peer_updates(u);
                   },
                   [state](const api::server::RoomStatusChanged &u) {
                     state->handle_room_status_changes(u);
                   },
                   [state](const api::server::MessageReceived &u) {
                     // unused
                   },
                   [state](const api::server::CommandFinished &u) {
                     if (u.error.has_value())
                       LOG_ERROR("rpc failed; {}", u.error.value());
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
  spdlog::set_level(spdlog::level::info);
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

    room_token = generate_token(
        token_generator, has_argument("bypass-gateway") ? "sfu" : "gateway",
        get_argument<std::string>("room-id"),
        get_argument<std::string>("user-id"));
    token_generator.reset();
  } else {
    room_token = get_argument<std::string>("room-token");
  }
  LOG_DEBUG("using room token: {}", room_token);

  /**
   * Spawn a new connection pool for transparent connection management.
   */
  OdinConnectionPool *connection_pool;
  OdinConnectionPoolSettings settings = {
      .on_datagram = &on_datagram,
      .on_rpc = &on_rpc,
      .user_data = reinterpret_cast<void *>(&state),
  };
  CHECK(odin_connection_pool_create(settings, &connection_pool));

  auto url = get_argument<std::string>("server-url");
  LOG_INFO("connecting to: {}", url);

  /*
   * Create a new ODIN room pointer and establish an encrypted connection to
   * the ODIN network using the given cipher and join the specified room.
   */
  float position[3] = {0.0, 0.0, 0.0};
  auto user_data = get_argument<std::string>("peer-user-data");
  OdinRoom *room;
  CHECK(odin_room_create_ex(connection_pool, url.data(), room_token.data(),
                            nullptr,
                            reinterpret_cast<const uint8_t *>(user_data.data()),
                            user_data.length(), position, cipher, &room));
  state.room = {room, odin_room_free};
  state.cipher = cipher;

  /**
   * Wait for user input.
   */
  std::cout << "--- Press RETURN to leave room and exit ---" << std::endl;
  getchar();

  /**
   * Disconnect from the room.
   */
  LOG_INFO("leaving room and closing connection to server");
  odin_room_close(room);

  /**
   * Release the connection pool.
   */
  odin_connection_pool_free(connection_pool);

  /**
   * Stop playback/capture audio devices.
   */
  state.stop_audio_devices();

  /*`
   * Shutdown the ODIN Voice runtime.
   */
  odin_shutdown();

  return EXIT_SUCCESS;
}
