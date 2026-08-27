/*
 * 4Players ODIN Position & Channel Mask Example
 *
 * Usage: odin_positions -r <room_id> -s <server_url> -k <access_key>
 *
 * This sample renders a small terminal UI where each client is placed on a 2D
 * grid. Move around to broadcast your 3D position with your voice packets and
 * toggle the channels you transmit on or listen to, to see how channel masks
 * cull traffic between peers. The grid is scaled to the server's culling
 * distance of 1.0 (drawn as a dotted ring around your avatar), so the level
 * of each peer fades with its distance until the server stops forwarding its
 * traffic entirely. Run multiple instances to see it in action; no audio
 * hardware is required, as each client transmits a generated test tone.
 *
 * Copyright (c) 4Players GmbH. Licensed under the MIT License; see LICENSE-MIT.
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <odin.h>

#include "api.hpp"
#include "utils.hpp"

#if defined(_WIN32)
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

/**
 * Dimensions of the 2D world the peers move around in; positions range from
 * -WORLD_EXTENT_X/Y to +WORLD_EXTENT_X/Y grid cells on the x/y axes. The
 * cell sizes map grid cells to world units, so the server's culling distance
 * of 1.0 spans 10 cells horizontally and 5 vertically (terminal characters
 * are roughly twice as tall as wide); peers farther apart stop receiving
 * each other's traffic.
 */
constexpr int WORLD_EXTENT_X = 19;
constexpr int WORLD_EXTENT_Y = 7;
constexpr float CELL_SIZE_X = 1.0f / 10.0f;
constexpr float CELL_SIZE_Y = 1.0f / 5.0f;

/**
 * Sample rate and tick interval used to pump generated audio through the
 * encoder and the received audio through the decoders.
 */
constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t TICK_MS = 20;
constexpr uint32_t SAMPLES_PER_TICK = SAMPLE_RATE / 1000 * TICK_MS;

/**
 * Global namespace for application-wide variables.
 */
namespace global {
std::atomic<bool> interrupted = false;
} // namespace global

/**
 * Per-peer bookkeeping with the decoder used to process incoming voice
 * packets, the channel mask of the most recent datagram and the measured
 * signal level.
 */
struct Peer {
  std::string user_id;
  OpaquePtr<OdinDecoder> decoder{nullptr, &odin_decoder_free};
  uint64_t active_channels = 0;
  float level = 0.0f;
  std::optional<OdinPosition> position;
  std::chrono::steady_clock::time_point last_datagram;
};

/**
 * Application state shared with the event callbacks, keeping track of our own
 * peer ID, the other peers in the room, the encoder used to transmit the test
 * tone and the current channel masks.
 */
struct State {
  std::mutex mutex;
  OdinRoom *room = nullptr;
  api::PeerId own_peer_id = 0;
  std::map<api::PeerId, Peer> peers;
  OpaquePtr<OdinEncoder> encoder{nullptr, &odin_encoder_free};
  OdinPosition position = {0.0f, 0.0f, 0.0f};
  uint64_t send_mask = 0x1;
  uint64_t listen_mask = ~0ull;
  bool muted = false;
  std::string status = "connecting";
};

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
       cxxopts::value<std::string>()->default_value(ODIN_DEFAULT_ROOM_ID));
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

  parse_arguments(options, argc, argv);
}

/**
 * Sends a remote procedure call (RPC) command to the server by serializing
 * the given command object to JSON.
 */
void send_rpc(OdinRoom *room, api::client::Command cmd) {
  nlohmann::json rpc = cmd;
  CHECK(odin_room_send_rpc(room, rpc.dump().data()));
}

/**
 * Applies the current listen mask to all known peers using the
 * `SetChannelMasks` RPC command. The server uses these masks to decide which
 * channels of each peer's voice packets are forwarded to us, culling all
 * other traffic before it even reaches this client.
 */
void apply_listen_mask(State &state) {
  std::vector<std::pair<api::PeerId, api::ChannelMask>> masks;
  for (const auto &[peer_id, peer] : state.peers) {
    masks.push_back({peer_id, state.listen_mask});
  }
  if (!masks.empty()) {
    send_rpc(state.room, api::client::SetChannelMasks{masks, true});
  }
}

/**
 * Applies the current position and send mask to the encoder. The channels a
 * voice packet is transmitted on are defined by the positions set on the
 * encoder, so this also controls which channels we broadcast on.
 */
void apply_position(State &state) {
  if (!state.encoder) {
    return;
  }
  CHECK(odin_encoder_clear_position(state.encoder.get(), ~state.send_mask));
  CHECK(odin_encoder_set_position(state.encoder.get(), state.send_mask,
                                  &state.position));
}

/**
 * Callback invoked when an RPC message is received from the room. This
 * function is registered with the ODIN room to keep track of the peers in
 * the room and to set up a decoder for each of them.
 */
void on_rpc(OdinRoom *, const char *text, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  try {
    auto event = nlohmann::json::parse(text).get<api::server::Event>();
    std::scoped_lock lock(state->mutex);
    std::visit(
        api::visitor{
            [state](const api::server::Joined &u) {
              state->own_peer_id = u.own_peer_id;
              state->status = "joined room '" + u.room_id + "' as peer " +
                              std::to_string(u.own_peer_id);

              // now that our own peer ID is known, create the encoder used
              // to transmit the test tone and place us at the origin
              OdinEncoder *encoder;
              CHECK(odin_encoder_create(u.own_peer_id, SAMPLE_RATE, false,
                                        &encoder));
              state->encoder = {encoder, &odin_encoder_free};
              apply_position(*state);
            },
            [state](const api::server::Left &u) {
              state->status = "left room; " + u.reason;
            },
            [state](const api::server::PeerJoined &u) {
              if (u.peer_id == state->own_peer_id) {
                return; // the server also announces ourselves; not a peer
              }
              OdinDecoder *decoder;
              CHECK(odin_decoder_create(SAMPLE_RATE, false, &decoder));
              auto &peer = state->peers[u.peer_id];
              peer.user_id = u.user_id;
              peer.decoder = {decoder, &odin_decoder_free};
              apply_listen_mask(*state);
            },
            [state](const api::server::PeerLeft &u) {
              state->peers.erase(u.peer_id);
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
            [state](const api::server::RoomStatusChanged &u) {
              state->status = "room status changed to '" + u.status + "'";
            },
            [state](const api::server::Error &u) {
              state->status = "server error: " + u.message;
            },
        },
        event);
  } catch (const std::exception &e) {
    LOG_WARNING("failed to decode incoming rpc; {}", e.what());
  }
}

/**
 * Callback invoked when a voice datagram is received from the room. This
 * function is registered with the ODIN room to handle incoming audio data.
 * It looks up the decoder of the source peer, pushes the datagram into it
 * and records the channel mask it was transmitted on.
 */
void on_datagram(OdinRoom *, const OdinDatagramProperties *properties,
                 const uint8_t *bytes, uint32_t bytes_length, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  std::scoped_lock lock(state->mutex);
  if (auto it = state->peers.find(properties->peer_id);
      it != state->peers.end() && it->second.decoder) {
    CHECK(odin_decoder_push(it->second.decoder.get(), bytes, bytes_length));
    it->second.active_channels = properties->channel_mask;
    it->second.last_datagram = std::chrono::steady_clock::now();
  }
}

/**
 * Platform-specific helpers to switch the terminal into raw mode and read
 * single keystrokes without blocking, using ANSI escape sequences for
 * rendering.
 */
namespace tui {

#if !defined(_WIN32)
termios saved_termios;
#endif
bool active = false;

void leave() {
  if (!active) {
    return;
  }
  active = false;
#if !defined(_WIN32)
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
#endif
  std::cout << "\x1b[?25h\x1b[?1049l" << std::flush; // show cursor, restore
}

void enter() {
#if defined(_WIN32)
  HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  GetConsoleMode(console, &mode);
  SetConsoleMode(console, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
  tcgetattr(STDIN_FILENO, &saved_termios);
  termios raw = saved_termios;
  raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
  std::cout << "\x1b[?1049h\x1b[?25l" << std::flush; // alt screen, hide cursor
  active = true;

  // make sure the terminal is restored even when the process exits through
  // a failed CHECK or any other call to exit()
  std::atexit(leave);
}

/**
 * Reads a single key without blocking; arrow keys are translated to WASD.
 * Returns 0 when no input is available.
 */
char read_key() {
#if defined(_WIN32)
  if (!_kbhit()) {
    return 0;
  }
  int c = _getch();
  if (c == 0 || c == 0xe0) {
    switch (_getch()) { // translate arrow keys
    case 72:
      return 'w';
    case 80:
      return 's';
    case 75:
      return 'a';
    case 77:
      return 'd';
    }
    return 0;
  }
  return static_cast<char>(c);
#else
  char c = 0;
  if (read(STDIN_FILENO, &c, 1) != 1) {
    return 0;
  }
  if (c == '\x1b') { // translate arrow key escape sequences
    char seq[2] = {0, 0};
    if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[' &&
        read(STDIN_FILENO, &seq[1], 1) == 1) {
      switch (seq[1]) {
      case 'A':
        return 'w';
      case 'B':
        return 's';
      case 'C':
        return 'd';
      case 'D':
        return 'a';
      }
    }
    return 0;
  }
  return c;
#endif
}

/**
 * Formats a channel mask as a fixed-width string listing the channel numbers
 * of all set bits, e.g. `1.3....8` for mask 0x85.
 */
std::string format_mask(uint64_t mask) {
  std::string out;
  for (int bit = 0; bit < 8; ++bit) {
    out += (mask >> bit) & 1 ? static_cast<char>('1' + bit) : '.';
  }
  return out;
}

/**
 * Renders the world grid and the peer table into an off-screen buffer and
 * writes it to the terminal in one go to avoid flickering.
 */
void render(State &state) {
  std::ostringstream out;
  out << "\x1b[H"; // move cursor home

  auto line = [&out](const std::string &text) {
    out << text << "\x1b[K\r\n"; // clear to end of line
  };

  line("ODIN Position & Channel Mask Example  |  " + state.status);
  line("move: WASD/arrows  send channels: 1-8  listen channels: shift+1-8  "
       "mute: m  quit: q");

  /**
   * Draw the world grid with our own avatar and all peers with a known
   * position placed in it.
   */
  const auto width = static_cast<std::size_t>(WORLD_EXTENT_X * 2 + 1);
  const auto height = static_cast<std::size_t>(WORLD_EXTENT_Y * 2 + 1);
  const auto to_cell = [](float value, int extent, float cell_size) {
    return std::clamp(static_cast<int>(std::lround(value / cell_size)), -extent,
                      extent);
  };
  const auto to_index = [](int value) {
    return static_cast<std::size_t>(value);
  };
  std::vector<std::string> grid(height, std::string(width, ' '));

  // mark the cells at the edge of the server's culling range around us
  for (int angle = 0; angle < 360; angle += 2) {
    float radians = static_cast<float>(angle) * 3.14159265f / 180.0f;
    int x = static_cast<int>(
        std::lround((state.position.x + std::cos(radians)) / CELL_SIZE_X));
    int y = static_cast<int>(
        std::lround((state.position.y + std::sin(radians)) / CELL_SIZE_Y));
    if (std::abs(x) <= WORLD_EXTENT_X && std::abs(y) <= WORLD_EXTENT_Y) {
      grid[to_index(y + WORLD_EXTENT_Y)][to_index(x + WORLD_EXTENT_X)] = '.';
    }
  }

  for (const auto &[peer_id, peer] : state.peers) {
    if (peer.position) {
      int x = to_cell(peer.position->x, WORLD_EXTENT_X, CELL_SIZE_X);
      int y = to_cell(peer.position->y, WORLD_EXTENT_Y, CELL_SIZE_Y);
      grid[to_index(y + WORLD_EXTENT_Y)][to_index(x + WORLD_EXTENT_X)] =
          static_cast<char>('0' + peer_id % 10);
    }
  }
  grid[to_index(to_cell(state.position.y, WORLD_EXTENT_Y, CELL_SIZE_Y) +
                WORLD_EXTENT_Y)]
      [to_index(to_cell(state.position.x, WORLD_EXTENT_X, CELL_SIZE_X) +
                WORLD_EXTENT_X)] = '@';

  line("+" + std::string(width, '-') + "+");
  for (const auto &row : grid) {
    line("|" + row + "|");
  }
  line("+" + std::string(width, '-') + "+");

  /**
   * Print our own transmit state followed by a table of all known peers.
   */
  std::ostringstream self;
  self << "you (peer " << state.own_peer_id << ")  position "
       << to_cell(state.position.x, WORLD_EXTENT_X, CELL_SIZE_X) << ","
       << to_cell(state.position.y, WORLD_EXTENT_Y, CELL_SIZE_Y)
       << "  sending on [" << format_mask(state.send_mask) << "]"
       << (state.muted ? "  MUTED" : "") << "  listening to ["
       << format_mask(state.listen_mask) << "]";
  line(self.str());
  line("");
  line("  peer  user                  channels  level             position");

  auto now = std::chrono::steady_clock::now();
  for (const auto &[peer_id, peer] : state.peers) {
    std::ostringstream row;
    bool culled = now - peer.last_datagram > std::chrono::seconds(1);
    row << "  " << std::left << std::setw(6) << peer_id << std::setw(22)
        << peer.user_id.substr(0, 20);
    if (culled) {
      row << std::setw(10) << "-" << std::setw(18) << "(no traffic)" << "-";
    } else {
      // attenuate the displayed level with the distance to the peer, fading
      // it out completely at the server's culling distance of 1.0
      float gain = 1.0f;
      if (peer.position) {
        float distance = std::hypot(peer.position->x - state.position.x,
                                    peer.position->y - state.position.y);
        gain = std::clamp(1.0f - distance, 0.0f, 1.0f);
      }
      int bars = std::clamp(
          static_cast<int>((20.0f * std::log10(std::max(peer.level * gain,
                                                        1e-6f)) +
                            60.0f) /
                           4.0f), // -60..0 dBFS mapped to 0..15
          0, 15);
      row << std::setw(10) << format_mask(peer.active_channels) << std::setw(18)
          << std::string(static_cast<std::size_t>(bars), '#');
      if (peer.position) {
        row << to_cell(peer.position->x, WORLD_EXTENT_X, CELL_SIZE_X) << ","
            << to_cell(peer.position->y, WORLD_EXTENT_Y, CELL_SIZE_Y);
      }
    }
    line(row.str());
  }

  out << "\x1b[J"; // clear rest of screen
  std::cout << out.str() << std::flush;
}

} // namespace tui

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
   * Create and configure a default logger instance; logging is reduced to
   * errors to not interfere with the terminal UI.
   */
  spdlog::set_default_logger(spdlog::stdout_color_mt(PROJECT_NAME));
  spdlog::set_level(spdlog::level::err);

  /**
   * Initialize the ODIN Voice runtime.
   */
  CHECK(odin_initialize(ODIN_VERSION));

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
  } else {
    room_token = get_argument<std::string>("room-token");
  }

  /**
   * Build custom authentication string.
   */
  nlohmann::json authentication = nlohmann::json::object({
      // mandatory room token
      {"token", room_token},
      // optional room id in case the token contains multiple room ids
      {"room_id", room_id},
  });

  /*
   * Create a new ODIN room pointer and establish a connection to the ODIN
   * network to join the specified room.
   */
  OdinRoom *room;
  OdinRoomEvents events{
      .on_datagram = &on_datagram,
      .on_rpc = &on_rpc,
      .on_socket = nullptr,
      .user_data = reinterpret_cast<void *>(&state),
  };
  {
    std::scoped_lock lock(state.mutex);
    CHECK(odin_room_create(gateway.data(), authentication.dump().data(),
                           &events, nullptr, &room));
    state.room = room;
  }

  /**
   * Process keyboard input and pump generated audio through the encoder and
   * received audio through the decoders at a fixed interval, redrawing the
   * terminal UI on every tick.
   */
  tui::enter();
  std::signal(SIGINT, [](int) { global::interrupted = true; });
  std::signal(SIGTERM, [](int) { global::interrupted = true; });

  float phase = 0.0f;
  bool exit = false;
  uint64_t tick = 0;
  auto next_tick = std::chrono::steady_clock::now();
  while (!exit && !global::interrupted) {
    // schedule ticks on an absolute timeline, so processing time does not
    // accumulate and the audio pump stays aligned with real time
    next_tick += std::chrono::milliseconds(TICK_MS);
    std::unique_lock lock(state.mutex);

    /**
     * Handle pending keystrokes to move around and adjust channel masks. The
     * shifted number keys (i.e. `!` for 1) toggle the channels to listen to.
     */
    for (char key; (key = tui::read_key()) != 0;) {
      switch (key) {
      case 'q':
        exit = true;
        break;
      case 'm':
        state.muted = !state.muted;
        break;
      case 'w':
      case 'a':
      case 's':
      case 'd': {
        float &x = state.position.x;
        float &y = state.position.y;
        x += CELL_SIZE_X * static_cast<float>((key == 'd') - (key == 'a'));
        y += CELL_SIZE_Y * static_cast<float>((key == 's') - (key == 'w'));
        x = std::clamp(x, -CELL_SIZE_X * WORLD_EXTENT_X,
                       CELL_SIZE_X * WORLD_EXTENT_X);
        y = std::clamp(y, -CELL_SIZE_Y * WORLD_EXTENT_Y,
                       CELL_SIZE_Y * WORLD_EXTENT_Y);
        apply_position(state);
      } break;
      default:
        if (key >= '1' && key <= '8') {
          state.send_mask ^= 1ull << (key - '1');
          if (state.send_mask == 0) {
            state.send_mask = 1ull << (key - '1'); // never send on nothing
          }
          apply_position(state);
        } else if (const char *shifted = strchr("!@#$%^&*", key); shifted) {
          state.listen_mask ^= 1ull << (shifted - "!@#$%^&*");
          apply_listen_mask(state);
        }
      }
    }

    /**
     * Push a chunk of the generated test tone (or silence when muted) into
     * the encoder and transmit all resulting voice packets.
     */
    if (state.encoder) {
      float samples[SAMPLES_PER_TICK];
      for (uint32_t i = 0; i < SAMPLES_PER_TICK; ++i) {
        phase = std::fmod(phase + 440.0f / SAMPLE_RATE, 1.0f);
        samples[i] =
            state.muted ? 0.0f : 0.25f * std::sin(2.0f * 3.14159265f * phase);
      }
      CHECK(odin_encoder_push(state.encoder.get(), samples, SAMPLES_PER_TICK));
      for (;;) {
        uint8_t datagram[2048];
        uint32_t datagram_length = sizeof(datagram);
        OdinError error =
            odin_encoder_pop(state.encoder.get(), datagram, &datagram_length);
        if (error != ODIN_ERROR_SUCCESS) {
          break;
        }
        CHECK(odin_room_send_datagram(room, datagram, datagram_length));
      }
    }

    /**
     * Pump each peer's decoder to measure the received signal level and grab
     * the channels and 3D positions of its most recent voice packet.
     */
    for (auto &[peer_id, peer] : state.peers) {
      float received[SAMPLES_PER_TICK];
      if (odin_decoder_pop(peer.decoder.get(), received, SAMPLES_PER_TICK,
                           nullptr) == ODIN_ERROR_SUCCESS) {
        float squared_sum = 0.0f;
        for (uint32_t i = 0; i < SAMPLES_PER_TICK; ++i) {
          squared_sum += received[i] * received[i];
        }
        peer.level = std::sqrt(squared_sum / SAMPLES_PER_TICK);
      }

      if (std::chrono::steady_clock::now() - peer.last_datagram >
          std::chrono::seconds(1)) {
        // no more traffic from this peer (e.g. culled by the server); drop
        // its stale channel, level and position information
        peer.active_channels = 0;
        peer.level = 0.0f;
        peer.position.reset();
      } else {
        // only channels whose most recent voice packet actually carried a
        // position are reported by the decoder; peers without position data
        // (e.g. other sample clients) are not placed on the grid
        uint64_t positioned =
            odin_decoder_get_active_channels(peer.decoder.get());
        if (positioned != 0) {
          uint64_t lowest_channel = positioned & ~(positioned - 1);
          OdinPosition position;
          uint32_t positions_length = 1;
          if (odin_decoder_get_positions(peer.decoder.get(), lowest_channel,
                                         &position, &positions_length) ==
              ODIN_ERROR_SUCCESS) {
            peer.position = position;
          }
        } else {
          peer.position.reset();
        }
      }
    }

    if (tick++ % 5 == 0) {
      tui::render(state); // redraw at 10 fps to keep the audio ticks light
    }
    lock.unlock();

    std::this_thread::sleep_until(next_tick);
  }

  tui::leave();

  /**
   * Disconnect from the room.
   */
  LOG_INFO("leaving room and closing connection to server");
  odin_room_close(room);
  odin_room_free(room);

  /*
   * Shutdown the ODIN Voice runtime.
   */
  odin_shutdown();

  return EXIT_SUCCESS;
}
