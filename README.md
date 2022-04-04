# 4Players ODIN SDK

ODIN is a cross-platform software development kit (SDK) that enables developers to integrate real-time chat technology into multiplayer games, apps and websites.

[Online Documentation](https://developers.4players.io/odin)

**:warning: Important Notice:**

> Please note that ODIN is currently in **Beta** and features are being added over time.

## Supported Platforms

The current release of ODIN is shipped with native pre-compiled binaries for the following platforms:

| Platform | x86_64             | aarch64            |
| -------- | ------------------ | ------------------ |
| Windows  | :white_check_mark: |                    |
| macOS    | :white_check_mark: | :white_check_mark: |
| Linux    | :white_check_mark: | :white_check_mark: |
| Android  | :white_check_mark: | :white_check_mark: |
| iOS      | :white_check_mark: | :white_check_mark: |

Support for gaming consoles is planned for early 2022 and will cover Microsoft Xbox, Sony PlayStation 4/5 and Nintendo Switch.

If your project requires support for any additional platform, please [contact us](https://www.4players.io/contact-us/).

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

The test client accepts three optional arguments:

```bat
odin_minimal <room_id> <access_key> <gateway_url>
```

The first argument allows you to specify a room name to join. If no `room_id` is specified, the client will join a room called **default**.

The second argument is an optional `access_key`. If none is provided, the test client will auto-generate a key and print it to the console. An access key is your unique authentication key, which is used to generate room tokens for accessing the 4Players ODIN server network. All clients that want to join the same ODIN room, need to use a token generated from either the same access key or another access key of the same project. To learn more about access keys, please refer to our [documentation](https://developers.4players.io/odin/guides/access-keys/).

The third and last argument allows you to specify an alternate ODIN `gateway_url`, which might be necessary if you're hosting your own fleet of ODIN servers. Otherwise, just use our default gateway, which is **https://gateway.odin.4players.io**.

## Troubleshooting

Contact us through the listed methods below to receive answers to your questions and learn more about ODIN.

### Discord

Join our official Discord server to chat with us directly and become a part of the 4Players ODIN community.

[![Join us on Discord](https://developers.4players.io/images/join_discord.png)](https://discord.gg/9yzdJNUGZS)

### Twitter

Have a quick question? Tweet us at [@4PlayersBiz](https://twitter.com/4PlayersBiz) and we’ll help you resolve any issues.

### Email

Don’t use Discord or Twitter? Send us an [email](mailto:odin@4players.io) and we’ll get back to you as soon as possible.
