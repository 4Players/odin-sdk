/* Copyright (c) 4Players GmbH. All rights reserved. */

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Supported channel layouts in audio functions.
 */
typedef enum OdinChannelLayout {
    /**
     * Samples are sequential
     */
    OdinChannelLayout_Mono,
    /**
     * Channels are interleaved
     */
    OdinChannelLayout_Stereo,
} OdinChannelLayout;

/**
 * All valid connection states of an ODIN client.
 */
typedef enum OdinConnectionState {
    OdinConnectionState_Connecting,
    OdinConnectionState_Connected,
    OdinConnectionState_Disconnected,
} OdinConnectionState;

/**
 * Known types of a media stream.
 *
 * Note: Video streams are not supported yet.
 */
typedef enum OdinMediaStreamType {
    OdinMediaStreamType_Audio,
    OdinMediaStreamType_Video,
    OdinMediaStreamType_Invalid,
} OdinMediaStreamType;

/**
 * Valid levels for aggressiveness of the noise suppression. A higher level will reduce the noise
 * level at the expense of a higher speech distortion.
 */
typedef enum OdinNoiseSuppsressionLevel {
    OdinNoiseSuppsressionLevel_None,
    OdinNoiseSuppsressionLevel_Low,
    OdinNoiseSuppsressionLevel_Moderate,
    OdinNoiseSuppsressionLevel_High,
    OdinNoiseSuppsressionLevel_VeryHigh,
} OdinNoiseSuppsressionLevel;

/**
 * Valid audiences for ODIN room tokens.
 */
typedef enum OdinTokenAudience {
    OdinTokenAudience_None,
    OdinTokenAudience_Gateway,
    OdinTokenAudience_Sfu,
} OdinTokenAudience;

typedef struct OdinMediaStream OdinMediaStream;

typedef struct OdinRoom OdinRoom;

typedef struct OdinTokenGenerator OdinTokenGenerator;

typedef int32_t OdinErrorCode;

/**
 * All the different events emitted from an ODIN room.
 */
typedef enum OdinEventTag {
    OdinEvent_PeerJoined,
    OdinEvent_PeerLeft,
    OdinEvent_PeerUpdated,
    OdinEvent_MediaAdded,
    OdinEvent_MediaRemoved,
    OdinEvent_ConnectionStateChanged,
    OdinEvent_None,
} OdinEventTag;

typedef struct OdinEvent_PeerJoinedData {
    uint64_t id;
    const uint8_t *user_data;
    size_t user_data_len;
} OdinEvent_PeerJoinedData;

typedef struct OdinEvent_PeerLeftData {
    uint64_t id;
} OdinEvent_PeerLeftData;

typedef struct OdinEvent_PeerUpdatedData {
    uint64_t id;
    const uint8_t *user_data;
    size_t user_data_len;
} OdinEvent_PeerUpdatedData;

typedef struct OdinEvent_MediaAddedData {
    uint64_t peer_id;
    uint16_t media_id;
    struct OdinMediaStream *stream;
} OdinEvent_MediaAddedData;

typedef struct OdinEvent_MediaRemovedData {
    uint16_t media_id;
} OdinEvent_MediaRemovedData;

typedef struct OdinEvent {
    OdinEventTag tag;
    union {
        OdinEvent_PeerJoinedData peer_joined;
        OdinEvent_PeerLeftData peer_left;
        OdinEvent_PeerUpdatedData peer_updated;
        OdinEvent_MediaAddedData media_added;
        OdinEvent_MediaRemovedData media_removed;
        struct {
            enum OdinConnectionState connection_state_changed;
        };
    };
} OdinEvent;

/**
 * Per-room configuration of the ODIN audio processing module which provides
 * a variety of enhancement algorithms.
 */
typedef struct OdinApmConfig {
    /**
     * Enables or disables voice activity detection
     */
    bool vad_enable;
    /**
     * Enable or disable echo cancellation
     */
    bool echo_canceller;
    /**
     * Enable or disable high pass filtering
     */
    bool high_pass_filter;
    /**
     * Enable or disable the pre amplifier
     */
    bool pre_amplifier;
    /**
     * Set the aggressiveness of the suppression
     */
    enum OdinNoiseSuppsressionLevel noise_suppression_level;
    /**
     * Enable or disable the transient suppressor
     */
    bool transient_suppressor;
} OdinApmConfig;

/**
 * Audio stream configuration.
 */
typedef struct OdinAudioStreamConfig {
    /**
     * The number of samples per second in hertz (between 8000 and 192000)
     */
    uint32_t sample_rate;
    /**
     * The number of channels for the new audio stream (between 1 and 2)
     */
    uint8_t channel_count;
} OdinAudioStreamConfig;

/**
 * Options for ODIN room tokens.
 */
typedef struct OdinTokenOptions {
    /**
     * Customer identifier (you should _NOT_ set this unless connecting directly to an ODIN server)
     */
    const char *customer;
    /**
     * Audience of the token
     */
    enum OdinTokenAudience audience;
    /**
     * Token lifetime in seconds
     */
    uint64_t lifetime;
} OdinTokenOptions;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * Formats an ODIN error code into a human readable string representation for use in logging and
 * diagnostics. If `buf` is `NULL` this functions simply returns the required buffer length to
 * store the output buffer.
 */
size_t odin_error_format(OdinErrorCode error, char *buf, size_t buf_len);

/**
 * Checks whether the code returned from ODIN function calls represents an error or a result.
 * This is used to easier work with certain functions that might return an error or a valid
 * result like `odin_audio_data_len`.
 */
bool odin_is_error(OdinErrorCode code);

/**
 * Creates as a new room in an unconnected state. Please note, that this function will return
 * `NULL` when the internal ODIN client runtime has already been terminated using `odin_shutdown`.
 */
struct OdinRoom *odin_room_create(void);

/**
 * Destroys the given room and disconnects from the server.
 */
void odin_room_destroy(struct OdinRoom *room);

/**
 * Sets the event callback on the the specified `OdinRoom`. When a callback has already been set
 * previously, this will call the new callback with new medias and all previously recevied medias
 * will be dropped and stop receiving any data. Generally this should be called _once_ before
 * joining a room.
 */
OdinErrorCode odin_room_set_event_callback(struct OdinRoom *room,
                                           void (*callback)(struct OdinRoom *room, const struct OdinEvent *event, void *user_data),
                                           void *user_data);

/**
 * Takes an URL to an ODIN sgateway (e.g. `https://gateway.odin.4players.io`) and a signed room
 * token obtained externally that authorizes the client to connect to a specific room. Optionally
 * this function takes in `user_data` which will be used to set the initial user data of the peer.
 */
OdinErrorCode odin_room_join(struct OdinRoom *room,
                             const char *url,
                             const char *token,
                             const uint8_t *user_data,
                             size_t user_data_length);

/**
 * Deprecated and intended for internal use only.
 */
OdinErrorCode odin_room_join_direct(struct OdinRoom *room,
                                    const char *url,
                                    const char *room_id,
                                    const uint8_t *user_data,
                                    size_t user_data_length);

/**
 * Updates the user data for our own peer in the specified `OdinRoom`.
 */
OdinErrorCode odin_room_update_user_data(struct OdinRoom *room,
                                         const uint8_t *user_data,
                                         size_t user_data_length);

/**
 * Adds a specified `OdinMediaStream` to the room. Please note, that this can only be done _once_
 * on a given media, trying to do it more than once will return an error on subsequent calls to
 * this function.
 */
OdinErrorCode odin_room_add_media(struct OdinRoom *room_r, struct OdinMediaStream *media);

/**
 * Configures the ODIN audio processing module on the room with the specified config.
 */
OdinErrorCode odin_room_configure_apm(struct OdinRoom *room, struct OdinApmConfig config);

/**
 * Creates a new audio stream which can be added to a room and receive or send data over it.
 */
struct OdinMediaStream *odin_audio_stream_create(struct OdinAudioStreamConfig config);

/**
 * Creates a new video stream.
 *
 * Note: Video streams are not supported yet.
 */
struct OdinMediaStream *odin_video_stream_create(void);

/**
 * Destroys the specified `OdinMediaStream`, after which you will no longer be able to receive
 * or send any data over it.
 */
void odin_media_stream_destroy(struct OdinMediaStream *stream);

/**
 * Returns the media ID of the specified `OdinMediaStream`.
 */
OdinErrorCode odin_media_get_media_id(struct OdinMediaStream *stream);

/**
 * Returns the type of the specified media stream.
 *
 * Note: This function will always return `OdinMediaStreamType_Audio` at the moment.
 */
enum OdinMediaStreamType odin_media_stream_type(struct OdinMediaStream *stream);

/**
 * Sends data to the audio stream. The data has to be interleaved [-1, 1] float data.
 */
OdinErrorCode odin_audio_push_data(struct OdinMediaStream *stream, float *buf, size_t buf_len);

/**
 * Returns the number of available sample available in the audio buffer of the of the specified
 * `OdinMediaStream`.
 */
OdinErrorCode odin_audio_data_len(struct OdinMediaStream *stream);

/**
 * Reads audio data from the specified `OdinMediaStream`. This will return audio data in 48kHz
 * interleaved.
 *
 * Note: `out_channel_layout` is reserved for future use.
 */
OdinErrorCode odin_audio_read_data(struct OdinMediaStream *stream,
                                   float *out_buffer,
                                   size_t out_buffer_len,
                                   enum OdinChannelLayout out_channel_layout);

/**
 * Reads up to `out_buffer_len` samples from the given streams and mixes them into the `out_buffer`.
 * All audio streams will be read based on a 48khz sample rate so make sure to allocate the buffer
 * accordingly. After the call the `out_buffer_len` will contain the amount of samples that have
 * actually been read and mixed into `out_buffer`.
 *
 * `out_channel_layout` specifies the target channel layout of the `out_buffer`. This is either:
 *  - Mono, all samples are sequential
 *  - Stereo, channels are interleaved.
 *
 * If enabled this will also apply any audio processing to the output stream and feed back required
 * data to the internal audio processing pipeline which requires a final mix.
 */
OdinErrorCode odin_audio_mix_streams(struct OdinRoom *room,
                                     struct OdinMediaStream *const *streams,
                                     size_t stream_count,
                                     float *out_buffer,
                                     size_t *out_buffer_len,
                                     enum OdinChannelLayout out_channel_layout);

/**
 * Processes the reverse audio stream, also known as the loopback data to be used in the ODIN echo
 * canceller. This should only be done if you are _NOT_ using `odin_audio_mix_streams`.
 */
OdinErrorCode odin_audio_process_reverse(struct OdinRoom *room,
                                         float *buffer,
                                         size_t buffer_len,
                                         enum OdinChannelLayout out_channel_layout);

/**
 * Starts the internal ODIN client runtime. This is ref-counted so you need matching calls of
 * startup and shutdown in your application. A lot of the functions in the API require a running
 * ODIN runtime. With the only exception being the `access_key` and `token_generator` related
 * functions.
 */
void odin_startup(void);

/**
 * Terminates the internal ODIN runtime. This function _should_ be called before shutting down
 * the application. After calling the functions all `odin_*` methods will fail immediately.
 */
void odin_shutdown(void);

/**
 * Creates a new access key required to access the ODIN network. An access key is a 44 character
 * long Base64-String, which consists of a version, random bytes and a checksum.
 */
OdinErrorCode odin_access_key_generate(char *buf, size_t buf_len);

/**
 * Retreives the key ID from a specified access key. The key ID is included in room tokens,
 * making it possible to identify which public key must be used for verification.
 */
OdinErrorCode odin_access_key_id(const char *access_key, char *out_key_id, size_t out_key_id_len);

/**
 * Retreives the public key from a specified access key. The public key is based on the Ed25519
 * curve and must be submitted to _4Players_ so that a generated room token can be verified.
 */
OdinErrorCode odin_access_key_public_key(const char *access_key,
                                         char *out_public_key,
                                         size_t out_public_key_len);

/**
 * Retreives the secret key from a specified access key. The secret key is based on the Ed25519
 * curve and used to sign a generated room token to access the ODIN network.
 */
OdinErrorCode odin_access_key_secret_key(const char *access_key,
                                         char *out_secret_key,
                                         size_t out_secret_key_len);

/**
 * Creates a new token generator instance.
 */
struct OdinTokenGenerator *odin_token_generator_create(const char *access_key);

/**
 * Destroys an existing token generator instance.
 */
void odin_token_generator_destroy(struct OdinTokenGenerator *generator);

/**
 * Generates a signed JWT, which can be used by an ODIN client to join a room.
 */
OdinErrorCode odin_token_generator_create_token(struct OdinTokenGenerator *generator,
                                                const char *room_id,
                                                const char *user_id,
                                                char *out_token,
                                                size_t out_token_len);

/**
 * Generates a signed JWT such as `odin_token_generator_create_token` and allows passing a custom
 * set of `OdinTokenOptions` for advanced use-cases.
 */
OdinErrorCode odin_token_generator_create_token_ex(struct OdinTokenGenerator *generator,
                                                   const char *room_id,
                                                   const char *user_id,
                                                   const struct OdinTokenOptions *options,
                                                   char *out_token,
                                                   size_t out_token_len);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus
