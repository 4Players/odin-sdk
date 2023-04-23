![ODIN](https://www.4players.io/images/odin/banner.jpg)

[![Releases](https://img.shields.io/github/release/4Players/odin-sdk)](https://github.com/4Players/odin-sdk/releases)
[![Documentation](https://img.shields.io/badge/docs-4Players.io-orange)](https://www.4players.io/developers/)
[![Twitter](https://img.shields.io/badge/twitter-@ODIN4Players-blue)](https://twitter.com/ODIN4Players)

# 4Players ODIN SDK

ODIN is a cross-platform software development kit (SDK) that enables developers to integrate real-time chat technology into multiplayer games, apps and websites. Whether using a native app or your favorite web browser, ODIN makes it easy for you to stay connected with those who matter most.

[Online Documentation](https://www.4players.io/developers/)

## Supported Platforms

The current release of ODIN is shipped with native pre-compiled binaries for the following platforms:

| Platform | x86                | x86_64             | aarch64            |
|----------|--------------------|--------------------|--------------------|
| Windows  | :white_check_mark: | :white_check_mark: | :white_check_mark: |
| Linux    | :white_check_mark: | :white_check_mark: | :white_check_mark: |
| macOS    |                    | :white_check_mark: | :white_check_mark: |
| Android  |                    | :white_check_mark: | :white_check_mark: |
| iOS      |                    | :white_check_mark: | :white_check_mark: |

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

## Troubleshooting

Contact us through the listed methods below to receive answers to your questions and learn more about ODIN.

### Discord

Join our official Discord server to chat with us directly and become a part of the 4Players ODIN community.

[![Join us on Discord](https://developers.4players.io/images/join_discord.png)](https://4np.de/discord)

### Twitter

Have a quick question? Tweet us at [@4PlayersBiz](https://twitter.com/4PlayersBiz) and we’ll help you resolve any issues.

### Email

Don’t use Discord or Twitter? Send us an [email](mailto:odin@4players.io) and we’ll get back to you as soon as possible.
