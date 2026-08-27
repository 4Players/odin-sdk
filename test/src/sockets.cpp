/*
 * 4Players ODIN Socket Example
 *
 * Usage: odin_sockets -r <room_id> -s <server_url> -k <access_key>
 *
 * Copyright (c) 4Players GmbH. Licensed under the MIT License; see LICENSE-MIT.
 * SPDX-License-Identifier: MIT
 */

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <odin.h>

#include "api.hpp"
#include "utils.hpp"

/**
 * Application state shared with the event callbacks, keeping track of our own
 * peer ID, the other peers in the room and the currently open socket.
 */
struct State {
  // guards the peer list shared between the room event callbacks and the
  // interactive command loop, which run on different threads
  std::mutex mutex;

  api::PeerId own_peer_id = 0;
  std::map<api::PeerId, std::string> peers;
  OdinSocket *socket = nullptr;
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
      // --debug
      ("d,debug", "show verbosity output")
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
 * Callback invoked when an RPC message is received from the room. This
 * function is registered with the ODIN room to keep track of the peers in
 * the room, so sockets can be opened to them by their actual peer ID.
 */
void on_rpc(OdinRoom *, const char *text, void *user_data) {
  const auto state = reinterpret_cast<State *>(user_data);
  try {
    nlohmann::json rpc = nlohmann::json::parse(text);
    LOG_DEBUG("received rpc: {}", rpc.dump());

    auto event = rpc.get<api::server::Event>();
    std::scoped_lock lock(state->mutex);
    std::visit(api::visitor{
                   [state](const api::server::Joined &u) {
                     state->own_peer_id = u.own_peer_id;
                     LOG_INFO("joined room '{}' as peer {}", u.room_id,
                              u.own_peer_id);
                   },
                   [](const api::server::Left &u) {
                     LOG_INFO("left room; {}", u.reason);
                   },
                   [state](const api::server::PeerJoined &u) {
                     state->peers[u.peer_id] = u.user_id;
                     LOG_INFO("peer {} ('{}') joined", u.peer_id, u.user_id);
                   },
                   [state](const api::server::PeerLeft &u) {
                     state->peers.erase(u.peer_id);
                     LOG_INFO("peer {} left", u.peer_id);
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
                   [](const api::server::RoomStatusChanged &u) {
                     LOG_INFO("room status changed to '{}'", u.status);
                   },
                   [](const api::server::Error &u) {
                     LOG_ERROR("server error: {}", u.message);
                   },
               },
               event);

  } catch (const std::exception &e) {
    LOG_WARNING("failed to decode incoming rpc; {}", e.what());
  }
}

/**
 * Callback invoked when a message is received on an ODIN socket. This
 * function is registered with the ODIN room to handle incoming socket
 * messages. It queries and prints the socket details and, for sockets opened
 * by a remote peer, sends back a simple reply to demonstrate bidirectional
 * communication.
 */
void on_socket(OdinSocket *socket, const uint8_t *message,
               uint32_t message_length, void *) {
  OdinSocketInfo info;
  CHECK(odin_socket_info(socket, &info));

  std::string text(reinterpret_cast<const char *>(message), message_length);
  LOG_INFO("received message from peer {} on {} {} socket (label {}): {}",
           info.remote_peer_id, info.is_inbound ? "inbound" : "outbound",
           info.kind == ODIN_SOCKET_KIND_RELIABLE ? "reliable" : "unreliable",
           info.label, text);

  if (info.is_inbound) {
    std::string reply = "replied to: " + text;
    if (reply.size() > std::numeric_limits<uint32_t>::max()) {
      LOG_WARNING("unable to reply; message is too large");
      return;
    }
    CHECK(odin_socket_send(socket,
                           reinterpret_cast<const uint8_t *>(reply.c_str()),
                           static_cast<uint32_t>(reply.size())));
  }
}

/**
 * Prints the list of commands available in the interactive prompt.
 */
void print_usage() {
  std::cout << "--- Available commands ---" << std::endl;
  std::cout << "  peers                      list peers in the room"
            << std::endl;
  std::cout << "  open <peer> [u] [label]    open a reliable (or [u]nreliable)"
            << " socket to the" << std::endl;
  std::cout << "                             specified peer ID; use 0 to"
            << " address all peers" << std::endl;
  std::cout << "  send <text>                send a message on the open socket"
            << std::endl;
  std::cout << "  close                      close the open socket"
            << std::endl;
  std::cout << "  help                       show this list" << std::endl;
  std::cout << "  exit                       leave room and quit" << std::endl;
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
      // optional room id in case the token contains multiple room ids
      {"room_id", room_id},
  });

  /*
   * Create a new ODIN room pointer and establish a connection to the ODIN
   * network to join the specified room.
   */
  OdinRoom *room;
  OdinRoomEvents events{
      .on_datagram = nullptr,
      .on_rpc = &on_rpc,
      .on_socket = &on_socket,
      .user_data = reinterpret_cast<void *>(&state),
  };
  CHECK(odin_room_create(gateway.data(), authentication.dump().data(), &events,
                         nullptr, &room));

  /**
   * Process user commands.
   */
  print_usage();

  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string command;
    input >> command;

    if (command == "exit" || command == "quit") {
      break;
    } else if (command == "help") {
      print_usage();
    } else if (command == "peers") {
      std::scoped_lock lock(state.mutex);
      std::cout << "Peers in room:" << std::endl;
      std::cout << "    " << state.own_peer_id << ": me" << std::endl;
      for (const auto &[peer_id, peer_user_id] : state.peers) {
        std::cout << "    " << peer_id << ": " << peer_user_id << std::endl;
      }
    } else if (command == "open") {
      uint32_t peer_id;
      if (!(input >> peer_id)) {
        LOG_WARNING("usage: open <peer> [u] [label]");
        continue;
      }
      {
        std::scoped_lock lock(state.mutex);
        if (peer_id != 0 && !state.peers.count(peer_id)) {
          LOG_WARNING("unknown peer {}; enter 'peers' for a list", peer_id);
          continue;
        }
      }
      auto kind = ODIN_SOCKET_KIND_RELIABLE;
      int label = 0;
      std::string option;
      if (input >> option) {
        if (option == "u" || option == "unreliable") {
          kind = ODIN_SOCKET_KIND_UNRELIABLE;
          input >> option;
        }
        if (!option.empty() && option != "u" && option != "unreliable") {
          auto [end, ec] = std::from_chars(
              option.data(), option.data() + option.size(), label);
          if (ec != std::errc() || end != option.data() + option.size()) {
            LOG_WARNING("invalid label '{}'; usage: open <peer> [u] [label]",
                        option);
            continue;
          }
        }
      }

      if (state.socket) {
        odin_socket_reset(state.socket);
        state.socket = nullptr;
      }
      CHECK(odin_socket_create(room, kind, peer_id, label, 0, &state.socket));
      LOG_INFO("opened {} socket to {} (label {})",
               kind == ODIN_SOCKET_KIND_RELIABLE ? "reliable" : "unreliable",
               peer_id == 0 ? "all peers" : "peer " + std::to_string(peer_id),
               label);
    } else if (command == "send") {
      std::string text;
      std::getline(input >> std::ws, text);
      if (text.empty()) {
        LOG_WARNING("usage: send <text>");
        continue;
      }
      if (!state.socket) {
        LOG_WARNING("no open socket; enter 'open <peer>' first");
        continue;
      }
      if (text.size() > std::numeric_limits<uint32_t>::max()) {
        LOG_WARNING("unable to send; message is too large");
        continue;
      }
      CHECK(odin_socket_send(state.socket,
                             reinterpret_cast<const uint8_t *>(text.c_str()),
                             static_cast<uint32_t>(text.size())));
    } else if (command == "close") {
      if (state.socket) {
        odin_socket_reset(state.socket);
        state.socket = nullptr;
        LOG_INFO("socket closed");
      }
    } else if (!command.empty()) {
      LOG_WARNING("unknown command '{}'; enter 'help' for a list", command);
    }
  }

  /**
   * Disconnect from the room.
   */
  LOG_INFO("leaving room and closing connection to server");
  if (state.socket) {
    odin_socket_reset(state.socket);
  }
  odin_room_close(room);
  odin_room_free(room);

  /*
   * Shutdown the ODIN Voice runtime.
   */
  odin_shutdown();

  return EXIT_SUCCESS;
}
