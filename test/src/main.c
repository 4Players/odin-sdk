/*
 * 4Players ODIN Minimal Client Example
 *
 * Usage: odin_minimal <room_id> <access_key> <gateway_url>
 */

#define MINIAUDIO_IMPLEMENTATION
#define __STDC_FORMAT_MACROS

#include <inttypes.h>
#include <stdio.h>

#include "miniaudio.h"
#include "odin.h"

#define DEFAULT_ROOM_ID "default"
#define DEFAULT_GW_ADDR "https://gateway.odin.4players.io"

OdinRoom*        room;
OdinMediaStream* input_stream;
OdinMediaStream* output_streams[512];
size_t           output_streams_len = 0;
uint64_t         own_peer_id;

/**
 * @brief Custom struct used to send data over the network in this example.
 * @note  Please keep in mind that padding and alignment of structs may differ between platforms so make sure to take care of
 *        packing accordingly to assure compatibility.
 */
struct MyMessage {
    int32_t foo;
    int32_t bar;
};

/**
 * @brief Basic helper function to print formatted error messages to standard error I/O
 *
 * @param error    ODIN error code to format
 * @param text     Custom string to use as error text prefix
 */
void print_error(OdinReturnCode error, const char* text)
{
    char buffer[512];
    odin_error_format(error, buffer, sizeof(buffer));
    fprintf(stderr, "%s; %s\n", text, buffer);
}

/**
 * @brief Adds a media stream to the global list of output streams
 *
 * @param stream    ODIN media stream to insert
 */
void insert_output_stream(OdinMediaStream* stream)
{
    output_streams[output_streams_len] = stream;
    output_streams_len += 1;
}

/**
 * @brief Removes a media stream from the global list of output stream and destroys it
 *
 * @param index    Position of the ODIN media stream to remove
 */
void remove_output_stream(size_t index)
{
    OdinMediaStream *removed_stream = output_streams[index];
    output_streams_len -= 1;
    output_streams[index] = output_streams[output_streams_len];
    output_streams[output_streams_len] = NULL;
    odin_media_stream_destroy(removed_stream);
}

/**
 * @brief Handler for ODIN room event callbacks
 *
 * @param room     Pointer to ODIN room that triggered the event
 * @param event    Pointer to ODIN event the be handled
 * @param data     Extra data pointer passed into `odin_room_set_event_callback`
 */
void handle_odin_event(OdinRoom* room, const OdinEvent* event, void* data)
{
    if (event->tag == OdinEvent_ConnectionStateChanged) {
        printf("Connection state changed to %d\n", event->connection_state_changed);
    } else if (event->tag == OdinEvent_PeerJoined) {
        printf("Peer(%"PRIu64") joined\n", event->peer_joined.id);
    } else if (event->tag == OdinEvent_PeerLeft) {
        /*
         * Walk through our global list of output streams and destroy the ones owned by the peer
         */
        for (size_t i = 0; i < output_streams_len; ++i) {
            uint64_t stream_peer_id;
            odin_media_stream_peer_id(output_streams[i], &stream_peer_id);
            if (stream_peer_id == event->peer_left.id) {
                remove_output_stream(i);
            }
        }
        printf("Peer(%"PRIu64") left\n", event->peer_left.id);
    } else if (event->tag == OdinEvent_PeerUpdated) {
        printf("Peer(%"PRIu64") updated\n", event->peer_updated.id);
    } else if (event->tag == OdinEvent_MediaAdded) {
        /*
         * Add the new output stream to our global list for later playback mixing
         */
        if (event->media_added.peer_id != own_peer_id) {
            insert_output_stream(event->media_added.stream);
        }
        printf("Media(%d) added by Peer(%"PRIu64")\n", event->media_added.media_id, event->media_added.peer_id);
    } else if (event->tag == OdinEvent_MediaRemoved) {
        /*
         * Find the output stream in our global list and destroy it
         */
        for (size_t i = 0; i < output_streams_len; ++i) {
            uint16_t stream_media_id;
            odin_media_stream_media_id(output_streams[i], &stream_media_id);
            if (stream_media_id == event->media_removed.media_id) {
                remove_output_stream(i);
                break;
            }
        }
        printf("Media(%d) removed\n", event->media_removed.media_id);
    } else if (event->tag == OdinEvent_MessageReceived) {
        /*
         * Handle arbitrary data sent by other peers
         */
        if (event->message_received.peer_id != own_peer_id) {
            printf("Peer(%"PRIu64") sent a message with %zu bytes\n", event->message_received.peer_id, event->message_received.data_len);
        }
    }
}

/**
 * @brief Handler for miniaudio callbacks fired whenever data is ready to be delivered to or from the device
 *
 * @param device         Pointer to the relevant input/output device
 * @param output         Pointer to output buffer that will receive audio data for playback
 * @param input          Pointer to the buffer containing input data from a recording device
 * @param frame_count    Number of PCM frames to process
 */
void handle_audio_data(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
{
    if (device->type == ma_device_type_capture) {
        /*
         * Push audio buffer from miniaudio callback to ODIN input stream
         */
        int error = odin_audio_push_data(input_stream, (float*) input, frame_count);
        if (odin_is_error(error)) {
            print_error(error, "Failed to push audio data");
        }
    } else {
        /*
         * Fill the miniaudio output buffer with mixed data from available ODIN output streams
         */
        size_t samples_read = frame_count;
        int error = odin_audio_mix_streams(room, output_streams, output_streams_len, output, &samples_read, OdinChannelLayout_Mono);
        if (odin_is_error(error)) {
            print_error(error, "Failed to read mixed audio data");
        }
    }
}

int main(int argc, char* argv[])
{
    char*     room_id;
    char*     gateway_url;
    char*     access_key;
    char      gen_access_key[128];
    char      room_token[256];
    ma_device input_device;
    ma_device output_device;
    uint32_t  error;

    /*
     * Grab the user-specified room id or fallback to default
     */
    room_id = (argc > 1) ? argv[1] : DEFAULT_ROOM_ID;

    /*
     * Grab the user-specified access key or generate a test key if needed
     */
    if (argc > 2) {
        access_key = argv[2];
    } else {
        odin_access_key_generate(gen_access_key, sizeof(gen_access_key));
        access_key = gen_access_key;
        printf("Generated new access key '%s' for testing\n", access_key);
    }

    /*
     * Grab the user-specified gateway address or fallback to default
     */
    gateway_url = (argc > 3) ? argv[3] : DEFAULT_GW_ADDR;

    /*
     * Initialize the ODIN runtime
     */
    odin_startup();

    /*
     * Spawn a new token generator using the specified access key
     */
    OdinTokenGenerator* generator = odin_token_generator_create(access_key);
    if (!generator) {
        fprintf(stderr, "Failed to initialize token generator; invalid access key\n");
        return 1;
    }

    /*
     * Generate a new room token to access the ODIN network
     */
    error = odin_token_generator_create_token(generator, room_id, "", room_token, sizeof(room_token));
    if (odin_is_error(error)) {
        print_error(error, "Failed to generate room token");
        return 1;
    }

    /*
     * Destroy the token generator as we don't need it anymore
     */
    odin_token_generator_destroy(generator);

    /*
     * Create a new ODIN room in an unconnected state
     */
    room = odin_room_create();

    /*
     * Set a handler for ODIN events
     */
    odin_room_set_event_callback(room, handle_odin_event, NULL);

    /*
     * Establish a connection to the ODIN network and join the specified room without custom user data
     */
    printf("Joining room '%s' using '%s'\n", room_id, gateway_url);
    error = odin_room_join(room, gateway_url, room_token, NULL, 0, &own_peer_id);
    if (odin_is_error(error)) {
        print_error(error, "Failed to join room");
        return 1;
    }
    printf("Connection established and associated with peer id %"PRIu64"\n", own_peer_id);

    /*
     * Configure audio processing options for the room
     */
    OdinApmConfig apm_config = {
        .vad_enable              = true,
        .echo_canceller          = true,
        .high_pass_filter        = false,
        .pre_amplifier           = false,
        .noise_suppression_level = OdinNoiseSuppsressionLevel_Moderate,
        .transient_suppressor    = false,
    };
    odin_room_configure_apm(room, apm_config);

    /*
     * Create the input audio stream with a samplerate of 48 kHz
     */
    OdinAudioStreamConfig audio_config = {
        .sample_rate   = 48000,
        .channel_count = 1,
    };
    input_stream = odin_audio_stream_create(audio_config);

    /*
     * Add input audio stream to room which is effetively unmuting the mic
     */
    error = odin_room_add_media(room, input_stream);
    if (odin_is_error(error)) {
        print_error(error, "Failed to add media stream");
        return 1;
    }

    /*
     * Configure and startup miniaudio input device
     */
    ma_device_config input_config  = ma_device_config_init(ma_device_type_capture);
    input_config.capture.format    = ma_format_f32;
    input_config.capture.channels  = 1;
    input_config.sampleRate        = 48000;
    input_config.dataCallback      = handle_audio_data;
    ma_device_init(NULL, &input_config, &input_device);
    if (ma_device_start(&input_device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to open capture device\n");
        ma_device_uninit(&input_device);
    }

    /*
     * Configure and startup miniaudio output device
     */
    ma_device_config output_config  = ma_device_config_init(ma_device_type_playback);
    output_config.playback.format   = ma_format_f32;
    output_config.playback.channels = 1;
    output_config.sampleRate        = 48000;
    output_config.dataCallback      = handle_audio_data;
    ma_device_init(NULL, &output_config, &output_device);
    if (ma_device_start(&output_device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to open playback device\n");
        ma_device_uninit(&output_device);
    }

    /*
     * Send some arbitrary data to other peers in the same room
     */
    struct MyMessage msg = {
        .foo = 1,
        .bar = 2,
    };
    odin_room_send_message(room, NULL, 0, (uint8_t*) &msg, sizeof(msg));

    /*
     * Wait for user input
     */
    printf("\n--- Press RETURN to leave room and exit ---\n");
    getchar();

    /*
     * Destroy the input audio stream.
     */
    odin_media_stream_destroy(input_stream);

    /*
     * Shutdown miniaudio input/output devices
     */
    ma_device_stop(&input_device);
    ma_device_stop(&output_device);

    /*
     * Leave the room and close the connection
     */
    printf("Leaving room '%s' and closing connection to server\n", room_id);
    odin_room_destroy(room);
    printf("Connection closed\n");

    /*
     * Shutdown the ODIN runtime
     */
    odin_shutdown();

    return 0;
}
