![ODIN](https://www.4players.io/images/odin/banner.jpg)

[![Releases](https://img.shields.io/github/release/4Players/odin-sdk)](https://github.com/4Players/odin-sdk/releases)
[![Documentation](https://img.shields.io/badge/docs-4Players.io-orange)](https://www.4players.io/developers/)
[![Twitter](https://img.shields.io/badge/twitter-@ODIN4Players-blue)](https://twitter.com/ODIN4Players)

# 4Players ODIN SDK

ODIN is a versatile cross-platform Software Development Kit (SDK) engineered to seamlessly integrate real-time voice chat into multiplayer games, applications, and websites. Regardless of whether you're employing a native application or your preferred web browser, ODIN simplifies the process of maintaining connections with your significant contacts. Through its intuitive interface and robust functionality, ODIN enhances interactive experiences, fostering real-time engagement and collaboration across various digital platforms.

You can choose between a managed cloud and a self-hosted solution. Let [4Players GmbH](https://www.4players.io/) deal with the setup, administration and bandwidth costs or run our [server software](https://github.com/4Players/odin-server) on your own infrastructure allowing you complete control and customization of your deployment environment.

- [Supported Platforms](#supported-platforms)
- [Getting Started](#getting-started)
- [Usage](#usage)
  - [Quick Start](#quick-start)
  - [Authentication](#authentication)
  - [Event Handling](#event-handling)
  - [Media Streams](#media-streams)
  - [Audio Processing](#audio-processing)
  - [User Data](#user-data)
  - [Messages](#messages)
- [Testing](#testing)
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
    odin_startup(ODIN_VERSION);

    OdinRoomHandle room = odin_room_create();
    odin_room_join(room, "<SERVER_URL>", "<TOKEN>");

    getchar();

    return 0;
}
```

### Authentication

To enter a room, an authentication token is requisite. ODIN employs the creation of signed JSON Web Tokens (JWT) to ensure a secure authentication pathway. These tokens encapsulate the details of the room(s) you wish to join alongside a customizable identifier for the user, which can be leveraged to reference an existing record within your specific service.

```c
char token[512];

OdinTokenGenerator *generator = odin_token_generator_create("<ACCESS_KEY>");

odin_token_generator_create_token_ex(generator, "<ROOM_ID>", "<USER_ID>", token, sizeof(token));
```

As ODIN is fully user agnostic, [4Players GmbH](https://www.4players.io/) does not store any of this information on its servers.

Tokens are signed employing an Ed25519 key pair derived from your distinctive access key. Think of an access key as a singular, unique authentication credential, crucial for generating room tokens to access the ODIN server network. It essentially combines the roles of a username and password into a singular, unobtrusive string of characters, necessitating a comparable degree of protection. For bolstered security, it is strongly recommended to refrain from incorporating an access key in your client-side code. We've created a very basic [Node.js](https://www.4players.io/odin/examples/token-server/) server here, to showcase how to issue ODIN tokens to your client apps without exposing your access key.

### Event Handling

The ODIN API operates on an event-driven paradigm. In order to manage events, it's necessary to create a callback function in the following manner:

```c
void handle_odin_event(OdinRoomHandle room, const OdinEvent *event, void *data)
{
   switch (event->tag)
   {
      case OdinEvent_RoomConnectionStateChanged:
         // Triggered when the room's connectivity state transitions
         break;
      case OdinEvent_Joined:
         // Triggered post successful room entry, rendering the initial state fully accessible
         break;
      case OdinEvent_RoomUserDataChanged:
         // Triggered when the room's arbitrary user data changed
         break;
      case OdinEvent_PeerJoined:
         // Triggered when a new user enters the room
         break;
      case OdinEvent_PeerLeft:
         // Triggered when a user exits the room
         break;
      case OdinEvent_PeerUserDataChanged:
         // Triggered when a peers's arbitrary user data changed
         break;
      case OdinEvent_MediaAdded:
         // Triggered when a peer introduces a media stream into the room
         break;
      case OdinEvent_MediaRemoved:
         // Triggered when a peer withdraws a media stream from the room
         break;
      case OdinEvent_MediaActiveStateChanged:
         // Triggered on change of media stream activity (e.g. user started/stopped talking)
         break;
      case OdinEvent_MessageReceived:
         // Triggered upon receipt of a message containing arbitrary data from a peer
         break;
      default:
         // Optionally handle unexpected events
         break;
    }
}
```

To register your callback function as an handler for ODIN events for a room, use the following command:

```c
odin_room_set_event_callback(room, handle_odin_event, NULL);
```

### Media Streams

Each peer within an ODIN room has the capability to attach media streams for the transmission of voice data. The snippet below illustrates the procedure to establish a new input media stream:

```c
OdinAudioStreamConfig config = {
    .sample_rate = 48000,
    .channel_count = 1,
};

OdinMediaStreamHandle stream = odin_audio_stream_create(config);

odin_room_add_media(room, stream);
```

For the handling of audio data through input/output media streams, employ the `odin_audio_push_data` and `odin_audio_read_data` functions. These functions enable the conveyance of audio data from your local microphone to your audio engine, and the playback of audio data received from other peers, respectively. For a comprehensive working example, refer to the [Testing](#testing) section below, which employs the [miniaudio](https://miniaud.io) library for cross-platform audio capture and playback.

### Audio Processing

Each ODIN room handle is equipped with a dedicated Audio Processing Module (APM) responsible for executing a spectrum of audio filters including, but not limited to, echo cancellation, noise suppression, and sophisticated voice activity detection. This module is designed to accommodate on-the-fly adjustments, empowering you to fine-tune audio settings in real time to suit evolving conditions. The snippet below demonstrates how you might alter the APM settings:

```c
OdinApmConfig apm_config = {
    .voice_activity_detection = true,
    .voice_activity_detection_attack_probability = 0.9,
    .voice_activity_detection_release_probability = 0.8,
    .volume_gate = false,
    .volume_gate_attack_loudness = -30,
    .volume_gate_release_loudness = -40,
    .echo_canceller = true,
    .high_pass_filter = false,
    .pre_amplifier = false,
    .noise_suppression_level = OdinNoiseSuppressionLevel_Moderate,
    .transient_suppressor = false,
    .gain_controller = true,
};

odin_room_configure_apm(room, apm_config);
```

The ODIN APM provides the following features:

#### Voice Activity Detection (VAD)

When enabled, ODIN will analyze the audio input signal using smart voice detection algorithm to determine the presence of speech. You can define both the probability required to start and stop transmitting.

#### Input Volume Gate

When enabled, the volume gate will measure the volume of the input audio signal, thus deciding when a user is speaking loud enough to transmit voice data. You can define both the root mean square power (dBFS) for when the gate should engage and disengage.

#### Acoustic Echo Cancellation (AEC)

When enabled the echo canceller will try to subtract echoes, reverberation, and unwanted added sounds from the audio input signal. Note, that you need to process the reverse audio stream, also known as the loopback data to be used in the ODIN echo canceller.

#### Noise Suppression

When enbabled, the noise suppressor will remove distracting background noise from the input audio signal. You can control the aggressiveness of the suppression. Increasing the level will reduce the noise level at the expense of a higher speech distortion.

#### High-Pass Filter (HPF)

When enabled, the high-pass filter will remove low-frequency content from the input audio signal, thus making it sound cleaner and more focused.

#### Preamplifier

When enabled, the preamplifier will boost the signal of sensitive microphones by taking really weak audio signals and making them louder.

#### Transient Suppression

When enabled, the transient suppressor will try to detect and attenuate keyboard clicks.

#### Automatic Gain Control (AGC)

When enabled, the gain controller will bring the input audio signal to an appropriate range when it's either too loud or too quiet.

### User Data

Each peer within a room is associated with a unique user data represented as a byte array (`uint8_t *`). This data is synchronized automatically across all peers, facilitating the storage of bespoke information for every individual peer and, if necessary, on a global scale for the room itself. Peers have the autonomy to modify their respective user data at any juncture, inclusive of before room entry to set an initial user data value.

```c
char *user_data = "{\"foo\":\"bar\"}";

odin_room_update_user_data(room, OdinUserDataTarget_Peer, (uint8_t *)user_data, strlen(user_data));
```

### Messages

ODIN allows you to send arbitrary to every other peer in the room or even individual targets. Just like user data, a message is a byte array (`uint8_t *`).

**Note:** Messages are always sent to all targets in the room, even when they moved out of proximity using setPosition.

```c
struct MyMessage msg = {
   .foo = 1,
   .bar = 2,
};

odin_room_send_message(room, NULL, 0, (uint8_t *)&msg, sizeof(msg));
```

## Testing

In addition to the latest binaries and C header files, this repository also contains a simple test client in the `test` sub-directory. Please note, that the configure process will try to download, verify and extract dependencies (e.g. [miniaudio](https://miniaud.io)), which are specified in the `CMakeLists.txt` file. [miniaudio](https://miniaud.io) is used to provide basic audio capture and playback functionality in the test client.

### Configuring and Building

1. Create a build directory:  
   `mkdir -p build && cd build`

2. Generate build scripts for your preferred build system:

   - For _make_ ...  
     `cmake ../`
   - ... or for _ninja_ ...  
     `cmake -GNinja ../`

3. Build the test client:
   - Using _make_ ...  
     `make`
   - ... or _ninja_ ...  
     `ninja`

On Windows, calling `cmake` from the build directory will create a _Visual Studio_ solution, which can be built using the following command:

```bat
msbuild odin_minimal.sln
```

### Using the Test Client

The test client accepts several arguments to control its functions, but the following three options are particularly crucial for its intended purpose:

```text
odin_minimal -r <room_id> -k <access_key> -s <server_url>
```

The `-r` argument (or `--room-id`) is used to specify the name of the room to join. If no room name is provided, the client will automatically join a room called **default**.

The `-k` argument (or `--access-key`) is used to specify an access key for generating tokens. If no access key is provided, the test client will auto-generate a key and display it in the console. An access key is a unique authentication key used to generate room tokens for accessing the 4Players ODIN server network. It is important to use the same access key for all clients that wish to join the same ODIN room. For more information about access keys, please refer to our [documentation](https://developers.4players.io/odin/guides/access-keys/).

The `-s` argument (or `--server-url`) allows you to specify an alternate ODIN server address. This address can be either the URL to an ODIN gateway or an ODIN server. You may need to specify an alternate server if you are hosting your own fleet of ODIN servers. If you do not specify an ODIN server URL, the test client will use the default gateway, which is located at **https://gateway.odin.4players.io**.

**Note:** You can use the `--help` argument to get a full list of options provided by the console client.

## Resources

- [Documentation](https://www.4players.io/developers/)
- [Examples](https://www.4players.io/odin/examples/)
- [Frequently Asked Questions](https://www.4players.io/odin/faq/)
- [Pricing](https://www.4players.io/odin/pricing/)

## Troubleshooting

Contact us through the listed methods below to receive answers to your questions and learn more about ODIN.

### Discord

Join our official Discord server to chat with us directly and become a part of the 4Players ODIN community.

[![Join us on Discord](https://developers.4players.io/images/join_discord.png)](https://4np.de/discord)

### Twitter

Have a quick question? Tweet us at [@ODIN4Players](https://twitter.com/ODIN4Players) and we’ll help you resolve any issues.

### Email

Don’t use Discord or Twitter? Send us an [email](mailto:odin@4players.io) and we’ll get back to you as soon as possible.
