/*
 * 4Players ODIN Minimal Client Example
 *
 * Usage: odin_minimal -r <room_id> -s <server_url> -k <access_key>
 */

#define MINIAUDIO_IMPLEMENTATION
#define __STDC_FORMAT_MACROS

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "argparse.h"
#include "miniaudio.h"
#include "odin.h"

OdinRoomHandle room;
OdinMediaStreamHandle input_stream;
OdinMediaStreamHandle output_streams[512];
size_t output_streams_len = 0;

/**
 * @brief Default room to join if none is specified.
 */
char *room_id = "default";

/**
 * @brief Default gateway/server to connect to if none is specified.
 */
char *server_url = "gateway.odin.4players.io";

/**
 * @brief Initial arbitrary user data to set for our own peer when joining the room (can be changed afterwards).
 */
char *user_data = "{\"name\":\"Console Client\"}";

/**
 * @brief Default user ID to use during authentication (usually refers to an existing record in your particular service).
 */
char *user_id = "My User ID";

/**
 * @brief Default access key to use if none is specified (using `NULL` will auto-generate one).
 */
char *access_key = NULL;

/**
 * @brief Default room token to use if none is specified (using `NULL` will auto-generate one).
 */
char *room_token = NULL;

/**
 * @brief Default audience for ODIN room tokens (use `OdinTokenAudience_Sfu` for on-premise setups without a gateway).
 */
int token_audience = OdinTokenAudience_Gateway;

/*
 * @brief Numeric index of the enumerated audio playback device to use (using `0` will pick the default device).
 */
int audio_output_device = 0;

/*
 * @brief Numeric index of the enumerated audio capture device to use (using `0` will pick the default device).
 */
int audio_input_device = 0;

/**
 * @brief Audio input configuration.
 */
OdinAudioStreamConfig audio_input_config = {
    .sample_rate = 48000,
    .channel_count = 1,
};

/**
 * @brief Audio output configuration.
 */
OdinAudioStreamConfig audio_output_config = {
    .sample_rate = 48000,
    .channel_count = 2,
};

/**
 * @brief Room audio processing configuration.
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
 * @Brief Custom struct to store information about available audio devices from miniaudio.
 */
typedef struct
{
    ma_device_info *output_devices;
    ma_uint32 output_devices_count;
    ma_device_info *input_devices;
    ma_uint32 input_devices_count;
} AudioDeviceList;

/**
 * @brief Basic helper function to print formatted error messages to standard error I/O.
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
 * @brief Basic helper function to get the internal media ID from a specified handle.
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
 * @brief Basic helper function to get the room ID from a specified handle.
 *
 * @param handle    The room handle to get the ID from
 */
const char *get_room_id_from_handle(OdinRoomHandle handle)
{
    char room_id[512];
    int error = odin_room_id(handle, room_id, sizeof(room_id));
    return odin_is_error(error) ? NULL : strdup(room_id);
}

/**
 * @brief Returns a human-readable string representation for a specified connection state.
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
 * @brief Returns a human-readable string representation for a specified connection state change reason.
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
 * @brief Reads an ODIN access key from the specified file.
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
 * @brief Writes an ODIN access key to the specified file.
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
 * @brief Fills an `AudioDeviceList` struct with information about the available audio devices.
 *
 * @param devices      Pointer to a struct which will be filled with device information
 */
int fill_audio_devices(AudioDeviceList *devices)
{
    ma_context context;
    ma_device_info *output_devices;
    ma_uint32 output_devices_count;
    ma_device_info *input_devices;
    ma_uint32 input_devices_count;
    ma_result result = ma_context_init(NULL, 0, NULL, &context);
    if (result == MA_SUCCESS)
    {
        if ((result = ma_context_get_devices(&context, &output_devices, &output_devices_count, &input_devices, &input_devices_count)) == MA_SUCCESS)
        {
            devices->output_devices = (ma_device_info *)malloc(sizeof(ma_device_info) * output_devices_count);
            memcpy(devices->output_devices, output_devices, sizeof(ma_device_info) * output_devices_count);
            devices->output_devices_count = output_devices_count;
            devices->input_devices = (ma_device_info *)malloc(sizeof(ma_device_info) * input_devices_count);
            memcpy(devices->input_devices, input_devices, sizeof(ma_device_info) * input_devices_count);
            devices->input_devices_count = input_devices_count;
        }
        ma_context_uninit(&context);
    }
    return result;
}

/**
 * @brief Frees the memory allocated for the device info arrays in an `AudioDeviceList` struct.
 *
 * @param devices      Pointer to a struct which had its info arrays previously allocated with malloc
 */
void free_audio_devices(AudioDeviceList *devices)
{
    if (devices->output_devices != NULL)
    {
        free(devices->output_devices);
        devices->output_devices = NULL;
    }
    devices->output_devices_count = 0;
    if (devices->input_devices != NULL)
    {
        free(devices->input_devices);
        devices->input_devices = NULL;
    }
    devices->input_devices_count = 0;
}

/**
 * @brief Explicitly enables/disables a given boolean or leaves it untouched.
 *
 * @param option    The option value to change
 * @param action    What to do with the option (0 - disable, 1 - ignore, 2 - enable)
 */
void adjust_apm_option(bool *option, int action)
{
    *option = (action == 2) || (action == 1 && *option);
}

/**
 * @brief Adds a media stream to the global list of output streams.
 *
 * @param stream    ODIN media stream to insert
 */
void insert_output_stream(OdinMediaStreamHandle stream)
{
    output_streams[output_streams_len] = stream;
    output_streams_len += 1;
}

/**
 * @brief Removes a media stream from the global list of output stream and destroys it.
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
 * @brief Handler for ODIN room event callbacks.
 *
 * @param room     Handle identifier of the ODIN room that triggered the event
 * @param event    Pointer to ODIN event the be handled
 * @param data     Extra data pointer passed into `odin_room_set_event_callback`
 */
void handle_odin_event(OdinRoomHandle room, const OdinEvent *event, void *data)
{
    if (event->tag == OdinEvent_RoomConnectionStateChanged)
    {
        const char *connection_state_name = get_name_from_connection_state(event->room_connection_state_changed.state);
        const char *connection_state_reason = get_name_from_connection_state_change_reason(event->room_connection_state_changed.reason);

        // Print new room connection state to console
        printf("Connection state changed to '%s' due to %s\n", connection_state_name, connection_state_reason);

        // Handle connection timeouts and reconnects as we need to create a new input stream
        if (event->room_connection_state_changed.reason == OdinRoomConnectionStateChangeReason_ConnectionLost)
        {
            input_stream = 0;
            if (event->room_connection_state_changed.state == OdinRoomConnectionState_Connected)
            {
                input_stream = odin_audio_stream_create(audio_input_config);
                odin_room_add_media(room, input_stream);
            }
        }

        // Stop client if the room handle gets disconnected (e.g. due to room close, kick/ban or connection issues)
        if (event->room_connection_state_changed.state == OdinRoomConnectionState_Disconnected)
        {
            if (event->room_connection_state_changed.reason != OdinRoomConnectionStateChangeReason_ClientRequested)
            {
                exit(1);
            }
        }
    }
    else if (event->tag == OdinEvent_Joined)
    {
        const char *room_id = event->joined.room_id;
        const char *customer = event->joined.customer;
        const char *own_user_id = event->joined.own_user_id;
        uint64_t peer_id = event->joined.own_peer_id;

        // Print information about joined room to the console
        printf("Room '%s' owned by '%s' joined successfully as Peer(%" PRIu64 ") with user ID '%s'\n", room_id, customer, peer_id, own_user_id);
    }
    else if (event->tag == OdinEvent_PeerJoined)
    {
        const char *user_id = event->peer_joined.user_id;
        uint64_t peer_id = event->peer_joined.peer_id;
        size_t peer_user_data_len = event->peer_joined.peer_user_data_len;

        // Print information about the peer to the console
        printf("Peer(%" PRIu64 ") joined with user ID '%s'\n", peer_id, user_id);

        // Print information about the peers user data to the console
        printf("Peer(%" PRIu64 ") has user data with %zu bytes\n", peer_id, peer_user_data_len);
    }
    else if (event->tag == OdinEvent_PeerLeft)
    {
        uint64_t peer_id = event->peer_left.peer_id;

        // Walk through our global list of output streams and destroy the remaining ones owned by the peer just to be sure
        for (size_t i = 0; i < output_streams_len; ++i)
        {
            uint64_t stream_peer_id;
            odin_media_stream_peer_id(output_streams[i], &stream_peer_id);
            if (stream_peer_id == peer_id)
            {
                remove_output_stream(i);
            }
        }

        // Print information about the peer to the console
        printf("Peer(%" PRIu64 ") left\n", peer_id);
    }
    else if (event->tag == OdinEvent_PeerUserDataChanged)
    {
        uint64_t peer_id = event->peer_user_data_changed.peer_id;
        size_t peer_user_data_len = event->peer_user_data_changed.peer_user_data_len;

        // Print information about the peers user data to the console
        printf("Peer(%" PRIu64 ") user data updated with %zu bytes\n", peer_id, peer_user_data_len);
    }
    else if (event->tag == OdinEvent_MediaAdded)
    {
        uint64_t peer_id = event->media_added.peer_id;
        uint16_t media_id = get_media_id_from_handle(event->media_added.media_handle);

        // Add the new output stream to our global list for later playback mixing
        insert_output_stream(event->media_added.media_handle);

        // Print information about the media to the console
        printf("Media(%d) added by Peer(%" PRIu64 ")\n", media_id, peer_id);
    }
    else if (event->tag == OdinEvent_MediaRemoved)
    {
        uint64_t peer_id = event->media_removed.peer_id;
        uint16_t media_id = get_media_id_from_handle(event->media_removed.media_handle);

        // Find the output stream in our global list and destroy it
        for (size_t i = 0; i < output_streams_len; ++i)
        {
            if (output_streams[i] == event->media_removed.media_handle)
            {
                remove_output_stream(i);
                break;
            }
        }

        // Print information about the media to the console
        printf("Media(%d) removed by Peer(%" PRIu64 ")\n", media_id, peer_id);
    }
    else if (event->tag == OdinEvent_MediaActiveStateChanged)
    {
        uint16_t media_id = get_media_id_from_handle(event->media_active_state_changed.media_handle);
        uint64_t peer_id = event->media_active_state_changed.peer_id;
        const char *state = event->media_active_state_changed.active ? "started" : "stopped";

        // Print information about the media activity update to the console
        printf("Peer(%" PRIu64 ") %s sending data on Media(%d)\n", peer_id, state, media_id);
    }
    else if (event->tag == OdinEvent_MessageReceived)
    {
        uint64_t peer_id = event->message_received.peer_id;
        size_t data_len = event->message_received.data_len;

        // Print information about the data we've received to the console
        printf("Peer(%" PRIu64 ") sent a message with %zu bytes\n", peer_id, data_len);
    }
}

/**
 * @brief Handler for miniaudio callbacks fired whenever data is ready to be delivered to or from the device.
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
        int sample_count = frame_count * device->capture.channels;
        odin_audio_push_data(input_stream, input, sample_count);
    }
    else if (device->type == ma_device_type_playback && output_streams_len)
    {
        /**
         * Walk through list of ODIN output streams to read and mix their data into the miniaudio output buffer
         */
        int sample_count = frame_count * device->playback.channels;
        float *samples = malloc(sample_count * sizeof(float));
        for (size_t i = 0; i < output_streams_len; i++)
        {
            odin_audio_read_data(output_streams[i], samples, sample_count);
            for (size_t i = 0; i < sample_count; ++i)
            {
                ((float *)output)[i] += samples[i];
            }
        }
        odin_audio_process_reverse(room, (float *)output, frame_count);
        free(samples);
    }
}

/**
 * @brief Prints the ODIN core SDK version and terminates the program.
 *
 * @param self           Pointer to the argparse struct
 * @param option         Pointer to the argparse option struct
 */
int cmd_show_version(struct argparse *self, const struct argparse_option *option)
{
    printf("%s\n", ODIN_VERSION);
    exit(0);
}

/**
 * @brief Prints a list of available audio devices and terminates the program.
 *
 * @param self           Pointer to the argparse struct
 * @param option         Pointer to the argparse option struct
 */
int cmd_list_audio_devices(struct argparse *self, const struct argparse_option *option)
{
    AudioDeviceList audio_devices;
    if (fill_audio_devices(&audio_devices) != MA_SUCCESS)
    {
        printf("Failed to retrieve audio device information\n");
        exit(1);
    }
    printf("Playback Devices\n");
    for (int i = 0; i < audio_devices.output_devices_count; ++i)
    {
        printf("    %u: %s\n", i + 1, audio_devices.output_devices[i].name);
    }
    printf("\n");
    printf("Capture Devices\n");
    for (int i = 0; i < audio_devices.input_devices_count; ++i)
    {
        printf("    %u: %s\n", i + 1, audio_devices.input_devices[i].name);
    }
    printf("\n");
    free_audio_devices(&audio_devices);
    exit(0);
}

/**
 * @brief The entry point of the program.
 *
 * @param argc           The number of command-line arguments passed to the program
 * @param argv           An array of pointers to strings, where each string is one of the command-line arguments
 */
int main(int argc, const char *argv[])
{
    char gen_access_key[128];
    char gen_room_token[512];
    ma_device input_device;
    ma_device output_device;
    AudioDeviceList devices;
    OdinReturnCode error;

    /*
     * Use miniaudio to retrieve a list of available audio devices
     */
    if (fill_audio_devices(&devices) != MA_SUCCESS)
    {
        printf("Failed to retrieve audio device information\n");
        return 1;
    }

    /**
     * Parse optional command-line arguments nd adjust settings accordingly
     *
     * Rules for boolean arguments:
     *  0 = Explicitly disable
     *  1 = No change
     *  2 = Explicitly enable
     */
    static const char *const usages[] = {
        "odin_minimal [options]",
        NULL,
    };
    int opt_apm_use_voice_activity_detection = 1;
    int opt_apm_use_volume_gate = 1;
    int opt_apm_use_echo_canceller = 1;
    int opt_apm_use_high_pass_filter = 1;
    int opt_apm_use_pre_amplifier = 1;
    int opt_apm_use_transient_suppressor = 1;
    int opt_apm_use_gain_controller = 1;
    int opt_apm_noise_suppression_level = apm_config.noise_suppression_level;
    struct argparse argparse;
    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_BOOLEAN('v', "version", NULL, "show version number and exit", cmd_show_version, 0, OPT_NONEG),
        OPT_STRING('r', "room-id", &room_id, "room to join (default: default)", NULL, 0, 0),
        OPT_STRING('s', "server-url", &server_url, "server url (default: gateway.odin.4players.io)", NULL, 0, 0),
        OPT_STRING('d', "peer-user-data", &user_data, "peer user data to set when joining the room", NULL, 0, 0),
        OPT_GROUP("Authorization"),
        OPT_STRING('t', "room-token", &room_token, "room token to use for authorization", NULL, 0, 0),
        OPT_STRING('k', "access-key", &access_key, "access key to use for local token generation", NULL, 0, 0),
        OPT_STRING('u', "user-id", &user_id, "identifier to use for local token generation", NULL, 0, 0),
        OPT_BOOLEAN('b', "bypass-gateway", &token_audience, "bypass gateway and connect to sfu directly", NULL, 0, OPT_NONEG),
        OPT_GROUP("Audio Devices"),
        OPT_BOOLEAN('a', "audio-devices", NULL, "show available audio devices and exit", cmd_list_audio_devices, 0, OPT_NONEG),
        OPT_INTEGER('\0', "output-device", &audio_output_device, "playback device to use", NULL, 0, 0),
        OPT_INTEGER('\0', "output-sample-rate", &audio_output_config.sample_rate, "playback sample rate in Hz", NULL, 0, 0),
        OPT_INTEGER('\0', "output-channel-count", &audio_output_config.channel_count, "playback channel count (1-2)", NULL, 0, 0),
        OPT_INTEGER('\0', "input-device", &audio_input_device, "capture device to use", NULL, 0, 0),
        OPT_INTEGER('\0', "input-sample-rate", &audio_input_config.sample_rate, "capture sample rate in Hz", NULL, 0, 0),
        OPT_INTEGER('\0', "input-channel-count", &audio_input_config.channel_count, "capture channel count (1-2)", NULL, 0, 0),
        OPT_GROUP("Audio Processing"),
        OPT_BOOLEAN('\0', "voice-activity-detection", &opt_apm_use_voice_activity_detection, "enable or disable speech detection algorithm", NULL, 0, 0),
        OPT_BOOLEAN('\0', "volume-gate", &opt_apm_use_volume_gate, "enable or disable input volume gate", NULL, 0, 0),
        OPT_FLOAT('\0', "volume-gate-dbfs-attack", &apm_config.volume_gate_attack_loudness, "input volume (dbFS) for gate to engage", NULL, 0, 0),
        OPT_FLOAT('\0', "volume-gate-dbfs-release", &apm_config.volume_gate_release_loudness, "input volume (dbFS) for gate to disengage", NULL, 0, 0),
        OPT_INTEGER('\0', "noise-suppression-level", &opt_apm_noise_suppression_level, "aggressiveness of noise suppression (0-5)", NULL, 0, 0),
        OPT_BOOLEAN('\0', "echo-canceller", &opt_apm_use_echo_canceller, "enable or disable echo cancellation", NULL, 0, 0),
        OPT_BOOLEAN('\0', "high-pass-filter", &opt_apm_use_high_pass_filter, "enable or disable high-pass filtering", NULL, 0, 0),
        OPT_BOOLEAN('\0', "pre-amplifier", &opt_apm_use_pre_amplifier, "enable or disable pre-amplification", NULL, 0, 0),
        OPT_BOOLEAN('\0', "transient-suppressor", &opt_apm_use_transient_suppressor, "enable or disable transient suppression", NULL, 0, 0),
        OPT_BOOLEAN('\0', "gain-controller", &opt_apm_use_gain_controller, "enable or disable automatic gain control", NULL, 0, 0),
        OPT_END(),
    };
    argparse_init(&argparse, options, usages, 0);
    argparse_describe(&argparse, "\n4Players ODIN Minimal Client Example", "\nUse the 'no-' prefix to explicitly disable options (e.g. --no-volume-gate).\n");
    argparse_parse(&argparse, argc, argv);
    adjust_apm_option(&apm_config.voice_activity_detection, opt_apm_use_voice_activity_detection);
    adjust_apm_option(&apm_config.volume_gate, opt_apm_use_volume_gate);
    adjust_apm_option(&apm_config.echo_canceller, opt_apm_use_echo_canceller);
    adjust_apm_option(&apm_config.high_pass_filter, opt_apm_use_high_pass_filter);
    adjust_apm_option(&apm_config.transient_suppressor, opt_apm_use_transient_suppressor);
    adjust_apm_option(&apm_config.gain_controller, opt_apm_use_gain_controller);
    apm_config.noise_suppression_level = fmax(OdinNoiseSuppressionLevel_None, fmin(OdinNoiseSuppressionLevel_VeryHigh, opt_apm_noise_suppression_level));
    audio_output_config.sample_rate = fmax(8000, fmin(192000, audio_output_config.sample_rate));
    audio_output_config.channel_count = fmax(1, fmin(2, audio_output_config.channel_count));
    audio_output_device = fmax(0, fmin(devices.output_devices_count, audio_output_device));
    audio_input_config.sample_rate = fmax(8000, fmin(192000, audio_input_config.sample_rate));
    audio_input_config.channel_count = fmax(1, fmin(2, audio_input_config.channel_count));
    audio_input_device = fmax(0, fmin(devices.input_devices_count, audio_input_device));

    /*
     * Configure and startup miniaudio output device
     */
    ma_device_config output_config = ma_device_config_init(ma_device_type_playback);
    if (audio_output_device > 0)
    {
        output_config.playback.pDeviceID = &devices.output_devices[audio_output_device - 1].id;
    }
    output_config.playback.format = ma_format_f32;
    output_config.playback.channels = audio_output_config.channel_count;
    output_config.sampleRate = audio_output_config.sample_rate;
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
     * Configure and startup miniaudio input device
     */
    ma_device_config input_config = ma_device_config_init(ma_device_type_capture);
    if (audio_input_device > 0)
    {
        input_config.capture.pDeviceID = &devices.input_devices[audio_input_device - 1].id;
    }
    input_config.capture.format = ma_format_f32;
    input_config.capture.channels = audio_input_config.channel_count;
    input_config.sampleRate = audio_input_config.sample_rate;
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
     * Generate a room token (JWT) to authenticate and join an ODIN room
     *
     * ===== IMPORTANT =====
     * Token generation should always be done on the server side, to prevent sensitive information from being leaked to
     * unauthorized parties. This functionality is provided for testing and demonstration purposes only in this client.
     *
     * Your access key is the unique authentication key to be used to generate room tokens for accessing the ODIN
     * server network. Think of it as your individual username and password combination all wrapped up into a single
     * non-comprehendible string of characters, and treat it with the same respect.
     *
     * Production code should NEVER generate tokens or contain your access key on the client side!
     */
    if (room_token == NULL)
    {
        if (access_key == NULL)
        {
            if (0 != read_access_key_file("odin_access_key", gen_access_key))
            {
                odin_access_key_generate(gen_access_key, sizeof(gen_access_key));
                write_access_key_file("odin_access_key", gen_access_key);
            }
            access_key = gen_access_key;
            printf("Using access key '%s' for testing\n", access_key);
        }

        OdinTokenGenerator *generator = odin_token_generator_create(access_key);
        if (!generator)
        {
            fprintf(stderr, "Failed to initialize token generator; invalid access key\n");
            return 1;
        }

        OdinTokenOptions token_options = {
            .customer = "<no customer>",
            .audience = token_audience,
            .lifetime = 300,
        };
        error = odin_token_generator_create_token_ex(generator, room_id, user_id, &token_options, gen_room_token, sizeof(gen_room_token));
        if (odin_is_error(error))
        {
            print_error(error, "Failed to generate room token");
            return 1;
        }
        room_token = gen_room_token;

        odin_token_generator_destroy(generator);
    }

    /*
     * Initialize the ODIN runtime based on the global output config
     */
    printf("Initializing ODIN runtime %s\n", ODIN_VERSION);
    odin_startup_ex(ODIN_VERSION, audio_output_config);

    /*
     * Create a new local ODIN room handle in an unconnected state
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
     * Set some initial user data for our peer
     */
    error = odin_room_update_peer_user_data(room, (uint8_t *)user_data, strlen(user_data));
    if (odin_is_error(error))
    {
        print_error(error, "Failed to set user data");
    }

    /*
     * Apply audio processing configuration for the room
     */
    error = odin_room_configure_apm(room, apm_config);
    if (odin_is_error(error))
    {
        print_error(error, "Failed to configure room audio processing options");
        return 1;
    }

    /*
     * Establish a connection to the ODIN network and join the specified room
     */
    printf("Joining room '%s' using '%s'\n", room_id, server_url);
    error = odin_room_join(room, server_url, room_token);
    if (odin_is_error(error))
    {
        print_error(error, "Failed to join room");
        return 1;
    }

    /*
     * Create a new local input audio stream handle based on the global input config
     */
    input_stream = odin_audio_stream_create(audio_input_config);

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
     * Cleanup list of previously retrieved audio devices
     */
    free_audio_devices(&devices);

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
