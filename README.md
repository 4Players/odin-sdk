![ODIN](https://www.4players.io/images/odin/banner.jpg)

[![Releases](https://img.shields.io/github/release/4Players/odin-sdk)](https://github.com/4Players/odin-sdk/releases)
[![Documentation](https://img.shields.io/badge/docs-4Players.io-orange)](https://docs.4players.io/voice)
[![Twitter](https://img.shields.io/badge/twitter-@ODIN4Players-blue)](https://twitter.com/ODIN4Players)

# 4Players ODIN SDK

ODIN is a versatile cross-platform Software Development Kit (SDK) engineered to seamlessly integrate real-time voice chat into multiplayer games, applications, and websites. Regardless of whether you're employing a native application or your preferred web browser, ODIN simplifies the process of maintaining connections with your significant contacts. Through its intuitive interface and robust functionality, ODIN enhances interactive experiences, fostering real-time engagement and collaboration across various digital platforms.

You can choose between a managed cloud and a self-hosted solution. Let [4Players GmbH](https://www.4players.io/) deal with the setup, administration and bandwidth costs or run our [server software](https://github.com/4Players/odin-server) on your own infrastructure allowing you complete control and customization of your deployment environment.

- [Supported Platforms](#supported-platforms)
- [Getting Started](#getting-started)
- [Usage](#usage)
  - [Quick Start](#quick-start)
  - [Authentication](#authentication)
  - [Connection Pooling](#connection-pooling)
  - [Audio Encoding and Decoding](#audio-encoding-and-decoding)
  - [Audio Pipelines & Effects](#audio-pipelines-and-effects)
- [Resources](#resources)
- [Troubleshooting](#troubleshooting)

## Supported Platforms

The current release of ODIN is shipped with native pre-compiled binaries for the following platforms:

| Platform | x86_64             | aarch64            | x86                | armv7a             |
|----------|--------------------|--------------------|--------------------|--------------------|
| Windows  | :white_check_mark: | :white_check_mark: | :white_check_mark: |                    |
| Linux    | :white_check_mark: | :white_check_mark: | :white_check_mark: |                    |
| macOS    | :white_check_mark: | :white_check_mark: |                    |                    |
| Android  | :white_check_mark: | :white_check_mark: | :white_check_mark: | :white_check_mark: |
| iOS      | :white_check_mark: | :white_check_mark: |                    |                    |

Support for gaming consoles is available upon request and covers Microsoft Xbox, Sony PlayStation 4/5 and Nintendo Switch.

If your project requires support for any additional platform, please [contact us](https://www.4players.io/company/contact_us/).

## Getting Started

To check out the SDK for development, clone the git repo into a working directory of your choice.

This repository uses [LFS](https://git-lfs.github.com) (large file storage) to manage pre-compiled binaries. Note that a standard clone of the repository might only retrieve the metadata about these files managed with LFS. In order to retrieve the actual data with LFS, please follow these steps:

1. Clone the repository:  
   `git clone https://github.com/4Players/odin-sdk.git`

2. Cache the actual LFS data on your local machine:  
   `git lfs fetch`

3. Replaces the metadata in the binary files with their actual contents:  
   `git lfs checkout`

## Usage

### Quick Start

The following code snippet illustrates how to join a designated room on a specified server using a room token acquired externally:

```c
#include <stdio.h>
#include "odin.h"

int main(int argc, const char *argv[])
{
   odin_initialize(ODIN_VERSION);

   OdinConnectionPool *pool;
   OdinConnectionPoolSettings settings = {
      .on_datagram = NULL,
      .on_rpc = NULL,
      .user_data = NULL
   };
   odin_connection_pool_create(settings, &pool);

   OdinRoom *room;
   odin_room_create(pool, "<SERVER_URL>", "<TOKEN>", &room);

   getchar();

   odin_room_free(room);
   odin_connection_pool_free(pool);
   odin_shutdown();

   return 0;
}
```

### Authentication

To enter a room, an authentication token is requisite. ODIN employs the creation of signed JSON Web Tokens (JWT) to ensure a secure authentication pathway. These tokens encapsulate the details of the room(s) you wish to join alongside a customizable identifier for the user, which can be leveraged to reference an existing record within your specific service.

```cpp
OdinTokenGenerator *generator;
odin_token_generator_create("<ACCESS_KEY>", &generator);

char token[512];
odin_token_generator_sign(generator, payload, token, sizeof(token));

odin_token_generator_free(generator);
```

The minimal payload of an ODIN token looks like this:

```jsonc
{
   "rid": "foo",      // customer-speficied room ID
   "uid": "bar",      // customer-speficied user ID as reference
   "exp": 1669852860, // token expiration date
   "nbf": 1669852800  // token activation time  
}
```

As ODIN is fully user agnostic, [4Players GmbH](https://www.4players.io/) does not store any of this information on its servers.

Tokens are signed employing an Ed25519 key pair derived from your distinctive access key. Think of an access key as a singular, unique authentication credential, crucial for generating room tokens to access the ODIN server network. It essentially combines the roles of a username and password into a singular, unobtrusive string of characters, necessitating a comparable degree of protection. For bolstered security, it is strongly recommended to refrain from incorporating an access key in your client-side code. We've created a very basic [Node.js](https://www.4players.io/odin/examples/token-server/) server here, to showcase how to issue ODIN tokens to your client apps without exposing your access key.

### Connection Pooling

ODIN uses connection pooling to manage all network communication and event routing between your application and the ODIN server infrastructure. A connection pool allows you to reuse connections across multiple rooms and ensures thread-safe event dispatching.

```cpp
OdinConnectionPool *pool;
OdinConnectionPoolSettings settings = {
    .on_datagram = &on_datagram, // for voice data
    .on_rpc      = &on_rpc,      // for signaling and events
    .user_data   = &state        // user-defined context passed to callbacks
};
odin_connection_pool_create(settings, &pool);
```

The `on_datagram` and `on_rpc` callbacks handle incoming data and control messages respectively.

#### Voice Data Handling

Voice data is delivered via the `on_datagram` callback. You should forward these datagrams to the appropriate ODIN decoder:

```cpp
void on_datagram(uint64_t room_ref, uint16_t media_id, const uint8_t *bytes, uint32_t bytes_length, void *user_data) {
   const auto state = reinterpret_cast<State *>(user_data);
   assert(odin_room_get_ref(state->room.get()) == room_ref);

   if (auto it = state->decoders.find(media_id); it != state->decoders.end()) {
      odin_decoder_push(it->second.ptr.get(), bytes, bytes_length);
   }
}
```

#### Event Handling

Control and event messages are delivered through the `on_rpc` callback. These messages are encoded using the [MessagePack](https://msgpack.org) format for compact and efficient transmission. To decode and handle them, you can use libraries like [msgpack-c](https://github.com/msgpack/msgpack-c) for low-level access or [nlohmann::json](https://github.com/nlohmann/json) with MessagePack support for a more convenient, high-level JSON-like interface - ideal for prototyping and rapid development.

```cpp
void on_rpc(uint64_t room_ref, const uint8_t *bytes, uint32_t bytes_length, void *user_data) {
   const auto state = reinterpret_cast<State *>(user_data);
   assert(odin_room_get_ref(state->room.get()) == room_ref);

   try {
      nlohmann::json rpc = nlohmann::json::from_msgpack(bytes, bytes + bytes_length);
      std::cout << "event: " << rpc.dump() << std::endl;
   } catch (const std::exception &err) {
      std::cerr << "error: " << err.what() << std::endl;
   }
}
```

### Audio Encoding and Decoding

ODIN separates audio transmission into two core components: *encoders* for outgoing streams and *decoders* for incoming ones. These components manage real-time audio conversion and transport between devices using a shared audio pipeline interface. They each encapsulate an Opus codec for compression/decompression as well as an ingress or egress resampler for automatic sample rate conversion.

Audio encoders and decoders are built to integrate tightly with the rest of the ODIN runtime and offer high performance and low latency. They provide flexible hooks for custom processing and allow seamless integration of advanced features like voice activity detection (VAD) and audio enhancements (APM).

#### Encoders (Outgoing Audio)

An encoder prepares raw PCM audio captured from local sources (typically a microphone) and transforms it into compressed ODIN datagrams. This data is then ready for network transmission.

```cpp
OdinEncoder *encoder;
odin_encoder_create(sample_rate, stereo, &encoder);

const OdinPipeline *pipeline = odin_encoder_get_pipeline(encoder);
```

Push captured audio into the encoder:

```cpp
odin_encoder_push(encoder, samples, sample_count); // samples must be float, interleaved, in [-1.0, 1.0]
```

Poll for encoded datagrams to send:

```cpp
for (;;) {
   uint8_t datagram[2048];
   uint32_t datagram_length = sizeof(datagram);
   switch (odin_encoder_pop(encoder, &media_ids, media_ids_length, datagram, &datagram_length)) {
      case ODIN_ERROR_SUCCESS:
         odin_room_send_datagram(room, datagram, datagram_length);
         break;
      case ODIN_ERROR_NO_DATA:
         return;
      default:
         std::cerr << "something went wrong" << std::endl;
   };
}
```

**Note:** You can assign the same encoder output to multiple rooms by specifying up to four media stream IDs.

#### Decoders (Incoming Audio)

A decoder receives and processes datagrams from remote peers. Each incoming media stream typically maps to one decoder instance:

```cpp
OdinDecoder *decoder;
odin_decoder_create(media_id, sample_rate, stereo, &decoder);

const OdinPipeline *pipeline = odin_decoder_get_pipeline(decoder);
```

Feed received datagrams into the decoder:

```cpp
odin_decoder_push(decoder, bytes, length);
```

Fetch decoded audio samples for playback:

```cpp
float samples[FRAME_SIZE];
bool is_silent;
odin_decoder_pop(decoder, samples, FRAME_SIZE, &is_silent);
```

**Note:** Decoders should be created and destroyed dynamically in response to peer update events such as `MediaStarted` or `MediaStopped`.

### Audio Pipelines and Effects

Each encoder and decoder in ODIN exposes an internal audio pipeline - a flexible and extensible processing chain that allows real-time manipulation of audio streams. Effects are applied in sequence to all audio flowing through the encoder or decoder. The pipeline automatically manages effect ordering internally, ensuring a consistent and logical execution flow.

#### VAD (Voice Activity Detection) Effects

The VAD module helps determine when a user is actively speaking. It provides two independent mechanisms:

- **Voice Activity Detection:** \
   When enabled, ODIN will analyze the audio input signal using smart voice detection algorithm to determine the presence of speech. You can define both the probability required to start and stop transmitting.
- **Voice Volume Gate:** \
   When enabled, the volume gate will measure the volume of the input audio signal, thus deciding when a user is speaking loud enough to transmit voice data. You can define both the root mean square power (dBFS) for when the gate should engage and disengage.

```cpp
uint32_t vad_id;
odin_pipeline_insert_vad_effect(pipeline, index, &vad_id);

OdinVadConfig config = {
   .voice_activity = {
      .enabled = true,
      .attack_threshold = 0.9f,
      .release_threshold = 0.8f
   },
   .volume_gate = {
      .enabled = true,
      .attack_threshold = -30,
      .release_threshold = -40
   }
};
odin_pipeline_set_vad_config(pipeline, vad_id, &config);
```

#### APM (Audio Processing Module) Effects

The APM module uses a variety of smart enhancement algorithms for enhancing audio clarity in noisy environments. It provides the following features:

- **Acoustic Echo Cancellation (AEC):** \
   When enabled the echo canceller will try to subtract echoes, reverberation, and unwanted added sounds from the audio input signal. Note, that you need to process the reverse audio stream, also known as the loopback data to be used in the ODIN echo canceller.
- **Noise Suppression:** \
   When enbabled, the noise suppressor will remove distracting background noise from the input audio signal. You can control the aggressiveness of the suppression. Increasing the level will reduce the noise level at the expense of a higher speech distortion.
- **High-Pass Filter (HPF):** \
   When enabled, the transient suppressor will try to detect and attenuate keyboard clicks.
- **Transient Suppression:** \
   When enabled, the high-pass filter will remove low-frequency content from the input audio signal, thus making it sound cleaner and more focused.
- **Automatic Gain Control (AGC):** \
   When enabled, the gain controller will bring the input audio signal to an appropriate range when it's either too loud or too quiet.

```cpp
uint32_t apm_id;
odin_pipeline_insert_apm_effect(pipeline, index, playback_sample_rate, stereo, &apm_id);

OdinApmConfig config = {
    .echo_canceller = true,
    .high_pass_filter = true,
    .transient_suppressor = true,
    .noise_suppression_level = ODIN_NOISE_SUPPRESSION_LEVEL_MODERATE,
    .gain_controller_version = ODIN_GAIN_CONTROLLER_VERSION_V2
};
odin_pipeline_set_apm_config(pipeline, apm_id, &config);
```

You can also push loopback (reverse) audio into the pipeline to support echo cancellation:

```cpp
odin_pipeline_update_apm_playback(pipeline, apm_id, reverse_samples, sample_count, delay_ms);
```

#### Custom Effects

ODIN allows you to define and insert your own audio processing functions. These run inline on the sample stream:

```cpp
void my_effect_callback(float *samples, uint32_t sample_count, bool *is_silent, const void *user_data) {
   // modify samples or observe silence state
}

uint32_t effect_id;
odin_pipeline_insert_custom_effect(pipeline, 2, my_effect_callback, user_data, &effect_id);
```

You can remove or reorder effects dynamically as needed. All modifications are thread-safe and take effect immediately.

### End-to-End Encryption (Cipher)

ODIN supports end-to-end encryption (E2EE) through the use of a pluggable `OdinCipher` module. This enables you to secure all datagrams, messages and peer user data with a shared room password - without relying on the server as a trust anchor.

```cpp
OdinCipher *cipher = odin_crypto_create(ODIN_CRYPTO_VERSION);
odin_crypto_set_password(cipher, (const uint8_t *)"secret", strlen("secret"));

odin_room_create_ex(pool, "<SERVER_URL>", "<TOKEN>", NULL, user_data, user_data_length, position, cipher, &room);
```

The encryption system uses a master key derived from the password, then derives peer-specific session keys with random salts. These salts are exchanged in-room so that all participants can reconstruct each other's peer keys. The master key never leaves the client and there are no long-term keys stored or distributed. Keys are rotated automatically after every 1 million packets or 2 GiB of traffic. The system is designed to minimize passive and active compromise by external actors. If the password is kept secure, so is your data.

## Resources

- [Documentation](https://docs.4players.io/voice/)
- [Frequently Asked Questions](https://docs.4players.io/voice/faq/)
- [Pricing](https://odin.4players.io/pricing/)

## Troubleshooting

Contact us through the listed methods below to receive answers to your questions and learn more about ODIN.

### Discord

Join our [official Discord server](https://4np.de/discord) to chat with us directly and become a part of the 4Players ODIN community.

### Email

Don’t use Discord or X? Send us an [email](mailto:odin@4players.io) and we’ll get back to you as soon as possible.
