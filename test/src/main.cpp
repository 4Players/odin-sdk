/*
 * 4Players ODIN Voice Client Example
 *
 * Usage: odin_client -r <room_id> -s <server_url> -k <access_key>
 *
 * Copyright (c) 4Players GmbH. Licensed under the MIT License; see LICENSE-MIT.
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
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
#include "utils.hpp"

/**
 * Atomic shared-pointer storage across C++20 standard-library implementations.
 * Apple libc++ currently lacks std::atomic<std::shared_ptr<T>>, so only this
 * compatibility wrapper needs to know about the legacy atomic accessors.
 */
template <typename T> class AtomicSharedPtr {
public:
  explicit AtomicSharedPtr(std::shared_ptr<T> value)
      : value_(std::move(value)) {}

  std::shared_ptr<T> load(std::memory_order order) const {
#if defined(__cpp_lib_atomic_shared_ptr) &&                                    \
    __cpp_lib_atomic_shared_ptr >= 201711L
    return this->value_.load(order);
#else
    return std::atomic_load_explicit(&this->value_, order);
#endif
  }

  void store(std::shared_ptr<T> value, std::memory_order order) {
#if defined(__cpp_lib_atomic_shared_ptr) &&                                    \
    __cpp_lib_atomic_shared_ptr >= 201711L
    this->value_.store(std::move(value), order);
#else
    std::atomic_store_explicit(&this->value_, std::move(value), order);
#endif
  }

private:
#if defined(__cpp_lib_atomic_shared_ptr) &&                                    \
    __cpp_lib_atomic_shared_ptr >= 201711L
  std::atomic<std::shared_ptr<T>> value_;
#else
  std::shared_ptr<T> value_;
#endif
};

/**
 * Global namespace for application-wide variables.
 */
namespace global {
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
OdinViConfig vi_effect_config = {
    .enabled = true,
    .attenuation_limit_db = 100.0f,
};
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
      ("disable-apm", "disable built-in audio processing module effects")
      // --enable-vi
      ("enable-vi", "enable built-in voice isolation effects");
  options.add_options("Audio Device")
      // --audio-devices
      ("a,audio-devices", "show available audio devices and exit")
      // --output-device <number>
      ("output-device", "playback device to use",
       cxxopts::value<int>()->default_value("0"))
      // --output-sample-rate <number>
      ("output-sample-rate", "playback sample rate in Hz",
       cxxopts::value<uint32_t>()->default_value("48000"))
      // --output-channels <number>
      ("output-channels", "playback channel count (1-2)",
       cxxopts::value<int>()->default_value("2"))
      // --input-device <number>
      ("input-device", "capture device to use",
       cxxopts::value<int>()->default_value("0"))
      // --input-sample-rate <number>
      ("input-sample-rate", "capture sample rate in Hz",
       cxxopts::value<uint32_t>()->default_value("48000"))
      // --input-channels <number>
      ("input-channels", "capture channel count (1-2)",
       cxxopts::value<int>()->default_value("1"));

  parse_arguments(options, argc, argv);

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
}

struct CustomEffectContext {
  uint64_t peer_id;
  bool is_silent;
};

/**
 * Custom pipeline effect callback to track peer talk status.
 */
static void custom_effect_talk_status(float *, uint32_t, bool *is_silent,
                                      const void *user_data) {
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

struct MediaState {
  std::shared_ptr<Encoder> encoder;
  std::unordered_map<api::PeerId, std::shared_ptr<Decoder>> decoders;
};

/**
 * Global application state.
 */
struct State {
  std::atomic<OdinRoom *> room = nullptr;
  OdinCipher *cipher = nullptr;
  api::PeerId own_peer_id = 0;

  ma_device playback_device = {};
  ma_device capture_device = {};
  bool playback_device_initialized = false;
  bool capture_device_initialized = false;

  uint32_t playback_sample_rate = 48000;
  bool playback_stereo = true;
  uint32_t capture_sample_rate = 48000;
  bool capture_stereo = false;

  AtomicSharedPtr<const MediaState> media{std::make_shared<const MediaState>()};

  std::shared_ptr<const MediaState> load_media() const {
    return this->media.load(std::memory_order_acquire);
  }

  void store_media(std::shared_ptr<const MediaState> next) {
    this->media.store(std::move(next), std::memory_order_release);
  }

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
                           uint32_t playback_device_sample_rate_hz,
                           int playback_device_channel_count,
                           int capture_device_idx,
                           uint32_t capture_device_sample_rate_hz,
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
  const auto media = state->load_media();

  if (device->type == ma_device_type_capture) {
    auto input_count = frame_count * device->capture.channels;
    const auto room = state->room.load(std::memory_order_acquire);

    if (media->encoder && room) {
      odin_encoder_push(media->encoder->ptr.get(),
                        reinterpret_cast<const float *>(input), input_count);
      for (;;) {
        uint8_t datagram[2048];
        uint32_t datagram_length = sizeof(datagram);
        switch (odin_encoder_pop(media->encoder->ptr.get(), datagram,
                                 &datagram_length)) {
        case ODIN_ERROR_SUCCESS:
          CHECK(odin_room_send_datagram(room, datagram, datagram_length));
          break;
        case ODIN_ERROR_NO_DATA:
          return;
        default:
          LOG_ERROR("failed to encode audio datagram to send");
          return;
        };
      }
    }
  } else if (device->type == ma_device_type_playback) {
    auto output_count = frame_count * device->playback.channels;
    auto *output_begin = reinterpret_cast<float *>(output);
    auto output_end = output_begin + output_count;
    thread_local std::vector<float> samples;
    samples.resize(output_count);

    for (const auto &[media_id, decoder] : media->decoders) {
      odin_decoder_pop(decoder->ptr.get(), samples.data(), output_count,
                       nullptr);
      std::transform(output_begin, output_end, samples.data(), output_begin,
                     std::plus<>());
    }

    if (media->encoder && media->encoder->apm_effect_id != 0 &&
        global::apm_effect_config.echo_canceller) {
      odin_pipeline_update_apm_playback(
          odin_encoder_get_pipeline(media->encoder->ptr.get()),
          media->encoder->apm_effect_id, output_begin, output_count, 10);
    }
  }
}

/**
 * Constructs a local state object and enumerates available audio devices.
 */
State::State() {
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

  this->store_media(std::make_shared<const MediaState>());
}

/**
 * Handles successful join to a room and configures an encoder for outgoing
 * audio.
 */
void State::on_room_joined(const std::string &room_id,
                           const std::string &customer, api::PeerId peer_id) {
  LOG_INFO("room '{}' owned by '{}' joined successfully as peer {}", room_id,
           customer, peer_id);

  this->own_peer_id = peer_id;
  this->configure_encoder(peer_id);
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

  auto next = std::make_shared<MediaState>(*this->load_media());
  next->decoders.erase(peer_id);
  this->store_media(std::move(next));
}

/**
 * Creates and configures an audio encoder for a specific peer. It retrieves
 * the encoder's processing pipeline and inserts built-in effects for speech
 * detection (VAD) and advanced audio processing (APM) as well as a custom
 * effect to track talk status for the local peer.
 */
void State::configure_encoder(const api::PeerId peer_id) {
  OdinEncoder *encoder;
  CHECK(odin_encoder_create(peer_id, this->capture_sample_rate,
                            this->capture_stereo, &encoder));
  const OdinPipeline *pipeline = odin_encoder_get_pipeline(encoder);

  uint32_t apm_effect_id;
  if (!has_argument("disable-apm")) {
    CHECK(odin_pipeline_insert_apm_effect(
        pipeline, odin_pipeline_get_effect_count(pipeline),
        this->playback_sample_rate, this->playback_stereo, &apm_effect_id));
    CHECK(odin_pipeline_set_apm_config(pipeline, apm_effect_id,
                                       &global::apm_effect_config));
  } else {
    apm_effect_id = 0;
  }

  uint32_t vi_effect_id;
  if (has_argument("enable-vi")) {
    CHECK(odin_pipeline_insert_vi_effect(
        pipeline, odin_pipeline_get_effect_count(pipeline), &vi_effect_id));
    CHECK(odin_pipeline_set_vi_config(pipeline, vi_effect_id,
                                       &global::vi_effect_config));
  } else {
    vi_effect_id = 0;
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

  auto encoder_state = std::make_shared<Encoder>(
      Encoder{OpaquePtr<OdinEncoder>(encoder, &odin_encoder_free),
              vad_effect_id,
              apm_effect_id,
              {peer_id, true}});

  CHECK(odin_pipeline_insert_custom_effect(
      pipeline, odin_pipeline_get_effect_count(pipeline),
      custom_effect_talk_status, static_cast<const void *>(&encoder_state->ctx),
      nullptr));

  auto next = std::make_shared<MediaState>(*this->load_media());
  next->encoder = std::move(encoder_state);
  this->store_media(std::move(next));
}

/**
 * Creates and configures an audio decoder for a specific peer. It retrieves
 * the decoder's processing pipeline and inserts a custom effect to track talk
 * status for the peer.
 */
void State::configure_decoder(const api::PeerId peer_id) {
  OdinDecoder *decoder;
  CHECK(odin_decoder_create(this->playback_sample_rate, this->playback_stereo,
                            &decoder));
  const OdinPipeline *pipeline = odin_decoder_get_pipeline(decoder);

  auto decoder_state = std::make_shared<Decoder>(Decoder{
      OpaquePtr<OdinDecoder>(decoder, &odin_decoder_free), {peer_id, true}});

  CHECK(odin_pipeline_insert_custom_effect(
      pipeline, 0, custom_effect_talk_status,
      static_cast<const void *>(&decoder_state->ctx), nullptr));

  auto next = std::make_shared<MediaState>(*this->load_media());
  next->decoders.insert_or_assign(peer_id, std::move(decoder_state));
  this->store_media(std::move(next));
}

/**
 * Sends a remote procedure call (RPC) command to the server by serializing the
 * given command object to JSON and transmitting it.
 */
void State::send_rpc(api::client::Command cmd) {
  nlohmann::json rpc = cmd;
  LOG_DEBUG("sending rpc: {}", rpc.dump());

  try {
    const auto room_handle = this->room.load(std::memory_order_acquire);
    if (!room_handle) {
      LOG_WARNING("unable to send rpc; room is not connected");
      return;
    }
    CHECK(odin_room_send_rpc(room_handle, rpc.dump().data()));
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
                                uint32_t playback_device_sample_rate_hz,
                                int playback_device_channel_count,
                                int capture_device_idx,
                                uint32_t capture_device_sample_rate_hz,
                                int capture_device_channels_count) {
  this->playback_sample_rate = playback_device_sample_rate_hz;
  this->playback_stereo = std::clamp(playback_device_channel_count, 1, 2) == 2;
  this->capture_sample_rate = capture_device_sample_rate_hz;
  this->capture_stereo = std::clamp(capture_device_channels_count, 1, 2) == 2;

  if (global::playback_devices.size()) {
    auto config = ma_device_config_init(ma_device_type_playback);
    if (playback_device_idx > 0 &&
        static_cast<std::size_t>(playback_device_idx) <=
            global::playback_devices.size()) {
      config.playback.pDeviceID =
          &global::playback_devices[static_cast<std::size_t>(
                                        playback_device_idx - 1)]
               .id;
    }
    config.playback.format = ma_format_f32;
    config.playback.channels = this->playback_stereo ? 2 : 1;
    config.sampleRate = this->playback_sample_rate;
    config.dataCallback = handle_audio_data;
    config.pUserData = this;

    auto result = ma_device_init(nullptr, &config, &this->playback_device);
    if (result == MA_SUCCESS &&
        (result = ma_device_start(&this->playback_device)) != MA_SUCCESS) {
      ma_device_uninit(&this->playback_device);
    }
    if (result != MA_SUCCESS) {
      LOG_ERROR("failed to open audio playback device; {}",
                ma_result_description(result));
    } else {
      this->playback_device_initialized = true;
      LOG_INFO("using audio playback device: {}",
               this->playback_device.playback.name);
    }
  } else {
    LOG_WARNING("no audio playback device available");
  }

  if (global::capture_devices.size()) {
    auto config = ma_device_config_init(ma_device_type_capture);
    if (capture_device_idx > 0 &&
        static_cast<std::size_t>(capture_device_idx) <=
            global::capture_devices.size()) {
      config.capture.pDeviceID =
          &global::capture_devices[static_cast<std::size_t>(capture_device_idx -
                                                            1)]
               .id;
    }
    config.capture.format = ma_format_f32;
    config.capture.channels = this->capture_stereo ? 2 : 1;
    config.sampleRate = this->capture_sample_rate;
    config.dataCallback = handle_audio_data;
    config.pUserData = this;

    auto result = ma_device_init(nullptr, &config, &this->capture_device);
    if (result == MA_SUCCESS &&
        (result = ma_device_start(&this->capture_device)) != MA_SUCCESS) {
      ma_device_uninit(&this->capture_device);
    }
    if (result != MA_SUCCESS) {
      LOG_ERROR("failed to open audio capture device; {}",
                ma_result_description(result));
    } else {
      this->capture_device_initialized = true;
      LOG_INFO("using audio capture device: {}",
               this->capture_device.capture.name);
    }
  } else {
    LOG_WARNING("no audio capture device available");
  }
}

/**
 * Stops and uninitializes all audio devices that were successfully
 * initialized before.
 */
void State::stop_audio_devices() {
  if (this->playback_device_initialized) {
    this->playback_device_initialized = false;
    ma_device_uninit(&this->playback_device);
  }
  if (this->capture_device_initialized) {
    this->capture_device_initialized = false;
    ma_device_uninit(&this->capture_device);
  }
}

/**
 * Callback invoked when a voice datagram is received from the room. This
 * function is registered with the ODIN room to handle incoming audio data.
 * It loads the current media snapshot, looks up the decoder for the source
 * peer and pushes the datagram into it for decoding and playback.
 */
void on_datagram(OdinRoom *, const OdinDatagramProperties *properties,
                 const uint8_t *bytes, uint32_t bytes_length, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  const auto media = state->load_media();
  if (auto it = media->decoders.find(properties->peer_id);
      it != media->decoders.end()) {
    CHECK(odin_decoder_push(it->second->ptr.get(), bytes, bytes_length));
  }
}

/**
 * Callback invoked when an RPC message is received from the room. This
 * function is registered with the ODIN connection pool to handle incoming RPC
 * messages. It parses the JSON payload, converts it to a server event variant
 * and dispatches it to the appropriate handler.
 */
void on_rpc(OdinRoom *, const char *text, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
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
                   [](const api::server::PeerChanged &) {
                     // unused
                   },
                   [](const api::server::NewReconnectToken &) {
                     // unused
                   },
                   [](const api::server::MessageReceived &) {
                     // unused
                   },
                   [state](const api::server::RoomStatusChanged &u) { // TODO
                     state->on_room_status_changed(u.status);
                   },
                   [](const api::server::Error &u) { // TODO
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
    if (password.size() > std::numeric_limits<uint32_t>::max()) {
      LOG_CRITICAL("master password is too long");
    }
    LOG_INFO("configuring ODIN cipher with master password");
    odin_crypto_set_password(cipher,
                             reinterpret_cast<const uint8_t *>(password.data()),
                             static_cast<uint32_t>(password.size()));
  }

  /**
   * Start playback/capture audio devices.
   */
  state.start_audio_devices(get_argument<int>("output-device"),
                            get_argument<uint32_t>("output-sample-rate"),
                            get_argument<int>("output-channels"),
                            get_argument<int>("input-device"),
                            get_argument<uint32_t>("input-sample-rate"),
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
    room_token = generate_token(token_generator, room_id, user_id);
    token_generator.reset();
  } else {
    room_token = get_argument<std::string>("room-token");
  }

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
      .on_socket = nullptr,
      .user_data = reinterpret_cast<void *>(&state),
  };
  // Publish the cipher before room creation can start delivering callbacks.
  // Audio callbacks wait for the atomic room publication before sending.
  state.cipher = cipher;
  CHECK(odin_room_create(gateway.data(), authentication.dump().data(), &events,
                         cipher, &room));
  OpaquePtr<OdinRoom> room_owner(room, &odin_room_free);
  state.room.store(room, std::memory_order_release);

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

  /**
   * Cleanup
   */
  state.room.store(nullptr, std::memory_order_release);
  room_owner.reset();
  state.store_media(std::make_shared<const MediaState>());

  /**
   * Shutdown the ODIN Voice runtime.
   */
  odin_shutdown();

  return EXIT_SUCCESS;
}
