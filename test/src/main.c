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
#define DEFAULT_GW_ADDR "gateway.odin.4players.io"

OdinRoomHandle room;
OdinMediaStreamHandle input_stream;
OdinMediaStreamHandle output_streams[512];
size_t output_streams_len = 0;

/**
 * @brief Custom struct used to send data over the network in this example.
 * @note  Please keep in mind that padding and alignment of structs may differ between platforms so make sure to take care of
 *        packing accordingly to assure compatibility.
 */
struct MyMessage
{
    int32_t foo;
    int32_t bar;
};

/**
 * @brief Basic helper function to print formatted error messages to standard error I/O
 *
 * @param error    ODIN error code to format
 * @param text     Custom string to use as error text prefix
 */
void print_error(OdinReturnCode error, const char *text)
{
    char buffer[512];
    odin_error_format(error, buffer, sizeof(buffer));
    fprintf(stderr, "%s; %s\n", text, buffer);
}

/**
 * @brief Basic helper function to get the internal media ID from a specified handle
 *
 * @param handle    The media stream handle to get the ID from
 */
uint16_t get_media_id_from_handle(OdinMediaStreamHandle handle)
{
    uint16_t media_id;
    int error = odin_media_stream_media_id(handle, &media_id);
    return odin_is_error(error) ? 0 : media_id;
}

/**
 * @brief Returns a human-readable string representation for a specified connection state
 *
 * @param state    The room connection state to translate
 */
const char *get_name_from_connection_state(OdinRoomConnectionState state)
{
    switch (state)
    {
    case OdinRoomConnectionState_Connecting:
        return "connecting";
    case OdinRoomConnectionState_Connected:
        return "connected";
    case OdinRoomConnectionState_Disconnecting:
        return "disconnecting";
    case OdinRoomConnectionState_Disconnected:
        return "disconnected";
    default:
        return "unknown";
    }
}

/**
 * @brief Returns a human-readable string representation for a specified connection state change reason
 *
 * @param state    The room connection state change reason to translate
 */
const char *get_name_from_connection_state_change_reason(OdinRoomConnectionStateChangeReason reason)
{
    switch (reason)
    {
    case OdinRoomConnectionStateChangeReason_ClientRequested:
        return "client request";
    case OdinRoomConnectionStateChangeReason_ServerRequested:
        return "server request";
    case OdinRoomConnectionStateChangeReason_ConnectionLost:
        return "timeout";
    default:
        return "unknown";
    }
}

/**
 * @brief Reads an ODIN access key from the specified file
 *
 * @param file_name     Name of the file to read
 * @param access_key    Buffer to store access key
 */
int read_access_key_file(const char *file_name, char *access_key)
{
    FILE *file;
    if (NULL == (file = fopen(file_name, "r")))
    {
        return -1;
    }
    fgets(access_key, 128, file);
    int error = ferror(file);
    fclose(file);
    return error;
}

/**
 * @brief Writes an ODIN access key to the specified file
 *
 * @param file_name     Name of the file to write
 * @param access_key    The access key to write
 */
int write_access_key_file(const char *file_name, const char *access_key)
{
    FILE *file;
    if (NULL == (file = fopen(file_name, "w")))
    {
        return -1;
    }
    fputs(access_key, file);
    int error = ferror(file);
    fclose(file);
    return error;
}

/**
 * @brief Adds a media stream to the global list of output streams
 *
 * @param stream    ODIN media stream to insert
 */
void insert_output_stream(OdinMediaStreamHandle stream)
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
    output_streams_len -= 1;
    output_streams[index] = output_streams[output_streams_len];
    output_streams[output_streams_len] = 0;
}

/**
 * @brief Handler for ODIN room event callbacks
 *
 * @param room     Handle identifier of the ODIN room that triggered the event
 * @param event    Pointer to ODIN event the be handled
 * @param data     Extra data pointer passed into `odin_room_set_event_callback`
 */
void handle_odin_event(OdinRoomHandle room, const OdinEvent *event, void *data)
{
    if (event->tag == OdinEvent_RoomConnectionStateChanged)
    {
        /*
         * Handle connection timeout and reconnect as we need to create a new input stream
         */
        if (event->room_connection_state_changed.reason == OdinRoomConnectionStateChangeReason_ConnectionLost)
        {
            input_stream = 0;
            if (event->room_connection_state_changed.state == OdinRoomConnectionState_Connected)
            {
                OdinAudioStreamConfig audio_config = {.sample_rate = 48000, .channel_count = 1};
                input_stream = odin_audio_stream_create(audio_config);
                odin_room_add_media(room, input_stream);
            }
        }

        const char *connection_state_name = get_name_from_connection_state(event->room_connection_state_changed.state);
        const char *connection_state_reason = get_name_from_connection_state_change_reason(event->room_connection_state_changed.reason);
        printf("Connection state changed to '%s' due to %s\n", connection_state_name, connection_state_reason);

        if (event->room_connection_state_changed.reason == OdinRoomConnectionStateChangeReason_ServerRequested)
        {
            exit(1);
        }
    }
    else if (event->tag == OdinEvent_Joined)
    {
        printf("Room '%s' owned by '%s' joined successfully as Peer(%" PRIu64 ") with user ID '%s'\n", event->joined.room_id, event->joined.customer, event->joined.own_peer_id, event->joined.own_user_id);
    }
    else if (event->tag == OdinEvent_PeerJoined)
    {
        printf("Peer(%" PRIu64 ") joined with user ID '%s'\n", event->peer_joined.peer_id, event->peer_joined.user_id);
    }
    else if (event->tag == OdinEvent_PeerLeft)
    {
        /*
         * Walk through our global list of output streams and destroy the ones owned by the peer
         */
        for (size_t i = 0; i < output_streams_len; ++i)
        {
            uint64_t stream_peer_id;
            odin_media_stream_peer_id(output_streams[i], &stream_peer_id);
            if (stream_peer_id == event->peer_left.peer_id)
            {
                remove_output_stream(i);
            }
        }

        printf("Peer(%" PRIu64 ") left\n", event->peer_left.peer_id);
    }
    else if (event->tag == OdinEvent_PeerUserDataChanged)
    {
        printf("Peer(%" PRIu64 ") user data updated with %zu bytes\n", event->peer_user_data_changed.peer_id, event->peer_user_data_changed.peer_user_data_len);
    }
    else if (event->tag == OdinEvent_RoomUserDataChanged)
    {
        printf("Room user data updated with %zu bytes\n", event->room_user_data_changed.room_user_data_len);
    }
    else if (event->tag == OdinEvent_MediaAdded)
    {
        /*
         * Add the new output stream to our global list for later playback mixing
         */
        insert_output_stream(event->media_added.media_handle);

        uint16_t media_id = get_media_id_from_handle(event->media_added.media_handle);
        printf("Media(%d) added by Peer(%" PRIu64 ")\n", media_id, event->media_added.peer_id);
    }
    else if (event->tag == OdinEvent_MediaRemoved)
    {
        /*
         * Find the output stream in our global list and destroy it
         */
        for (size_t i = 0; i < output_streams_len; ++i)
        {
            if (output_streams[i] == event->media_removed.media_handle)
            {
                remove_output_stream(i);
                break;
            }
        }

        uint16_t media_id = get_media_id_from_handle(event->media_removed.media_handle);
        printf("Media(%d) removed by Peer(%" PRIu64 ")\n", media_id, event->media_removed.peer_id);
    }
    else if (event->tag == OdinEvent_MediaActiveStateChanged)
    {
        printf("Peer(%" PRIu64 ") %s sending data on Media(%d)\n", event->media_active_state_changed.peer_id, event->media_active_state_changed.active ? "started" : "stopped", get_media_id_from_handle(event->media_active_state_changed.media_handle));
    }
    else if (event->tag == OdinEvent_MessageReceived)
    {
        printf("Peer(%" PRIu64 ") sent a message with %zu bytes\n", event->message_received.peer_id, event->message_received.data_len);
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
void handle_audio_data(ma_device *device, void *output, const void *input, ma_uint32 frame_count)
{
    if (device->type == ma_device_type_capture && input_stream)
    {
        /*
         * Push audio buffer from miniaudio callback to ODIN input stream
         */
        int error = odin_audio_push_data(input_stream, input, frame_count);
        if (odin_is_error(error))
        {
            print_error(error, "Failed to push audio data");
        }
    }
    else if (device->type == ma_device_type_playback)
    {
        if (true /* wether or not to handle invidual streams */)
        {
            /**
             * Walk through list of ODIN output streams to read and mix their data into the miniaudio output buffer
             */
            float *samples = malloc(frame_count * sizeof(float));
            for (size_t i = 0; i < output_streams_len; i++)
            {

                int error = odin_audio_read_data(output_streams[i], samples, frame_count, OdinChannelLayout_Mono);
                if (odin_is_error(error))
                {
                    print_error(error, "Failed to read audio data from media handle");
                }
                for (size_t i = 0; i < frame_count; ++i)
                {
                    ((float *)output)[i] += samples[i];
                }
            }
            odin_audio_process_reverse(room, (float *)output, frame_count, OdinChannelLayout_Mono);
            free(samples);
        }
        else
        {
            /*
             * Keep things simple and fill the miniaudio output buffer with mixed data from available ODIN output streams
             */
            size_t samples_read = frame_count;
            int error = odin_audio_mix_streams(room, output_streams, output_streams_len, output, &samples_read, OdinChannelLayout_Mono);
            if (odin_is_error(error))
            {
                print_error(error, "Failed to read mixed audio data");
            }
        }
    }
}

int main(int argc, char *argv[])
{
    char *room_id;
    char *gateway_url;
    char *access_key;
    char gen_access_key[128];
    char room_token[512];
    ma_device input_device;
    ma_device output_device;
    OdinReturnCode error;

    /*
     * Grab the user-specified room id or fallback to default
     */
    room_id = (argc > 1) ? argv[1] : DEFAULT_ROOM_ID;

    /*
     * Grab the user-specified access key or generate a local test key if needed
     *
     * ===== IMPORTANT =====
     * Your access key is the unique authentication key to be used to generate room tokens for accessing the ODIN
     * server network. Think of it as your individual username and password combination all wrapped up into a single
     * non-comprehendible string of characters, and treat it with the same respect. For your own security, we strongly
     * recommend that you NEVER put an access key in your client-side code.
     */
    if (argc > 2)
    {
        access_key = argv[2];
    }
    else
    {
        if (0 != read_access_key_file("odin_access_key", gen_access_key))
        {
            odin_access_key_generate(gen_access_key, sizeof(gen_access_key));
            write_access_key_file("odin_access_key", gen_access_key);
        }
        access_key = gen_access_key;
        printf("Using access key '%s' for testing\n", access_key);
    }

    /*
     * Grab the user-specified gateway address or fallback to default
     */
    gateway_url = (argc > 3) ? argv[3] : DEFAULT_GW_ADDR;

    /*
     * Initialize the ODIN runtime
     */
    printf("Initializing ODIN runtime %s\n", ODIN_VERSION);
    odin_startup(ODIN_VERSION);

    /*
     * Spawn a new token generator using the specified access key
     */
    OdinTokenGenerator *generator = odin_token_generator_create(access_key);
    if (!generator)
    {
        fprintf(stderr, "Failed to initialize token generator; invalid access key\n");
        return 1;
    }

    /*
     * Generate a new room token to access the ODIN network
     */
    error = odin_token_generator_create_token(generator, room_id, "My User ID", room_token, sizeof(room_token));
    if (odin_is_error(error))
    {
        print_error(error, "Failed to generate room token");
        return 1;
    }

    /*
     * Destroy the token generator as we don't need it anymore
     */
    odin_token_generator_destroy(generator);

    /*
     * Create a new ODIN room pointer in an unconnected state
     */
    room = odin_room_create();

    /*
     * Set a handler for ODIN events
     */
    error = odin_room_set_event_callback(room, handle_odin_event, NULL);
    if (odin_is_error(error))
    {
        print_error(error, "Failed to set room event callback");
    }

    /*
     * Set some initial user data for our peer using JSON
     */
    char *user_data = "{\"name\":\"Console Client\"}";
    error = odin_room_update_user_data(room, OdinUserDataTarget_Peer, (uint8_t *)user_data, strlen(user_data));
    if (odin_is_error(error))
    {
        print_error(error, "Failed to set user data");
    }

    /*
     * Establish a connection to the ODIN network and join the specified room
     */
    printf("Joining room '%s' using '%s'\n", room_id, gateway_url);
    error = odin_room_join(room, gateway_url, room_token);
    if (odin_is_error(error))
    {
        print_error(error, "Failed to join room");
        return 1;
    }

    /*
     * Configure audio processing options for the room
     */
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
    error = odin_room_configure_apm(room, apm_config);
    if (odin_is_error(error))
    {
        print_error(error, "Failed to configure room audio processing options");
        return 1;
    }

    /*
     * Create the input audio stream with a samplerate of 48 kHz
     */
    OdinAudioStreamConfig audio_config = {
        .sample_rate = 48000,
        .channel_count = 1,
    };
    input_stream = odin_audio_stream_create(audio_config);

    /*
     * Add input audio stream to room which is effectively enabling the microphone input
     */
    error = odin_room_add_media(room, input_stream);
    if (odin_is_error(error))
    {
        print_error(error, "Failed to add media stream");
        return 1;
    }

    /*
     * Configure and startup miniaudio input device
     */
    ma_device_config input_config = ma_device_config_init(ma_device_type_capture);
    input_config.capture.format = ma_format_f32;
    input_config.capture.channels = 1;
    input_config.sampleRate = 48000;
    input_config.dataCallback = handle_audio_data;
    ma_device_init(NULL, &input_config, &input_device);
    if (ma_device_start(&input_device) != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to open capture device\n");
        ma_device_uninit(&input_device);
    }
    else
    {
        printf("Using capture device '%s'\n", input_device.capture.name);
    }

    /*
     * Configure and startup miniaudio output device
     */
    ma_device_config output_config = ma_device_config_init(ma_device_type_playback);
    output_config.playback.format = ma_format_f32;
    output_config.playback.channels = 1;
    output_config.sampleRate = 48000;
    output_config.dataCallback = handle_audio_data;
    ma_device_init(NULL, &output_config, &output_device);
    if (ma_device_start(&output_device) != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to open playback device\n");
        ma_device_uninit(&output_device);
    }
    else
    {
        printf("Using playback device '%s'\n", output_device.playback.name);
    }

    /*
     * Send some arbitrary data to all peers in the room
     */
    struct MyMessage msg = {
        .foo = 1,
        .bar = 2,
    };
    error = odin_room_send_message(room, NULL, 0, (uint8_t *)&msg, sizeof(msg));
    if (odin_is_error(error))
    {
        print_error(error, "Failed to send message");
    }

    /*
     * Wait for user input
     */
    printf("\n--- Press RETURN to leave room and exit ---\n");
    getchar();

    /*
     * Shutdown miniaudio input/output devices
     */
    ma_device_uninit(&input_device);
    ma_device_uninit(&output_device);

    /*
     * Destroy the input audio stream.
     */
    odin_media_stream_destroy(input_stream);

    /*
     * Leave the room and close the connection
     */
    printf("Leaving room '%s' and closing connection to server\n", room_id);
    odin_room_close(room);
    printf("Connection closed\n");

    /*
     * Release the room handle
     */
    odin_room_destroy(room);

    /*
     * Shutdown the ODIN runtime
     */
    odin_shutdown();

    return 0;
}
