#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace nlohmann {

template <typename T> struct adl_serializer<std::optional<T>> {
  static void to_json(json &j, const std::optional<T> &opt) {
    if (!opt.has_value()) {
      j = nullptr;
    } else {
      j = *opt;
    }
  }
  static void from_json(const json &j, std::optional<T> &opt) {
    if (j.is_null()) {
      opt = std::nullopt;
    } else {
      opt = j.template get<T>();
    }
  }
};

template <typename... Ts> struct adl_serializer<std::variant<Ts...>> {
  static void to_json(json &j, const std::variant<Ts...> &v) {
    std::visit([&j](auto const &x) { j = x; }, v);
  }
  static void from_json(const json &j, std::variant<Ts...> &v) {
    auto kind = j.at("kind").get<std::string>();
    bool matched = (... || ([&] {
                      auto o = Ts{};
                      if (kind == o.kind) {
                        v = j.get<Ts>();
                        return true;
                      }
                      return false;
                    }()));
    if (!matched) {
      throw std::runtime_error("unexpected rpc notification kind: " + kind);
    }
  }
};

} // namespace nlohmann

namespace api {

// ─── BASIC STRUCTS ───────────────────────────────────────────────────────────

struct Media {
  uint16_t id;
  nlohmann::json properties;
  bool paused;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Media, id, properties, paused);

struct Peer {
  uint64_t id;
  std::string user_id;
  nlohmann::json::binary_t user_data;
  std::vector<Media> medias;
  std::vector<std::string> tags;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Peer, id, user_id, user_data, medias, tags);

struct Room {
  std::string id;
  std::string customer;
  nlohmann::json::binary_t user_data;
  std::vector<Peer> peers;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Room, id, customer, user_data, peers);

namespace server {

// ─── ROOM UPDATE EVENT VARIANTS ──────────────────────────────────────────────

/**
 * Emitted after joining once initial room information was processed.
 */
struct RoomUpdateJoined {
  std::string kind = "Joined";
  Room room;
  std::vector<uint16_t> media_ids;
  uint64_t own_peer_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoomUpdateJoined, kind, room, media_ids,
                                   own_peer_id);

/**
 * Emitted when the global user data of the room was changed.
 */
struct RoomUpdateUserDataChanged {
  std::string kind = "UserDataChanged";
  nlohmann::json::binary_t user_data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoomUpdateUserDataChanged, kind, user_data);

/**
 * Emitted after being removed from a room by the server.
 */
struct RoomUpdateLeft {
  std::string kind = "Left";
  std::string reason;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoomUpdateLeft, kind, reason);

/**
 * Emitted when other peers joined the room.
 */
struct RoomUpdatePeerJoined {
  std::string kind = "PeerJoined";
  Peer peer;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoomUpdatePeerJoined, kind, peer);

/**
 * Emitted when other peers left the room.
 */
struct RoomUpdatePeerLeft {
  std::string kind = "PeerLeft";
  uint64_t peer_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoomUpdatePeerLeft, kind, peer_id);

using RoomUpdateKinds =
    std::variant<RoomUpdateJoined, RoomUpdateUserDataChanged, RoomUpdateLeft,
                 RoomUpdatePeerJoined, RoomUpdatePeerLeft>;

struct RoomUpdated {
  std::vector<RoomUpdateKinds> updates;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RoomUpdated, updates);

// ─── PEER UPDATE EVENT VARIANTS ──────────────────────────────────────────────

/**
 * Emitted when other peers updated their user data.
 */
struct PeerUpdateUserDataChanged {
  std::string kind = "UserDataChanged";
  uint64_t peer_id;
  nlohmann::json::binary_t user_data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeerUpdateUserDataChanged, kind, peer_id,
                                   user_data);

/**
 * Emitted when other peers started a media stream.
 */
struct PeerUpdateMediaStarted {
  std::string kind = "MediaStarted";
  uint64_t peer_id;
  Media media;
  nlohmann::json properties; // deprecated
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeerUpdateMediaStarted, kind, peer_id, media,
                                   properties);

/**
 * Emitted when other peers stopped a media stream.
 */
struct PeerUpdateMediaStopped {
  std::string kind = "MediaStopped";
  uint64_t peer_id;
  uint16_t media_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeerUpdateMediaStopped, kind, peer_id,
                                   media_id);

/**
 * Emitted when the tags of another peer were changed.
 */
struct PeerUpdateTagsChanged {
  std::string kind = "TagsChanged";
  uint64_t peer_id;
  std::vector<std::string> tags;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeerUpdateTagsChanged, kind, peer_id, tags);

using PeerUpdated =
    std::variant<PeerUpdateUserDataChanged, PeerUpdateMediaStarted,
                 PeerUpdateMediaStopped, PeerUpdateTagsChanged>;

// ─── INCOMING ARBITRARY DATA EVENT ───────────────────────────────────────────

/**
 * Emitted when others peers sent a message with arbitrary data.
 */
struct MessageReceived {
  uint64_t sender_peer_id;
  nlohmann::json::binary_t message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MessageReceived, sender_peer_id, message);

// ─── ROOM STATUS CHANGED EVENT ───────────────────────────────────────────────

/**
 * Emitted when the status of the underlying connection for a room changed.
 */
struct RoomStatusChanged {
  std::optional<std::string> message;
  std::string status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RoomStatusChanged, message,
                                                status);

// ─── SUPPORTED EVENTS AND RESPONSE TYPES ─────────────────────────────────────

/**
 * Emitted when we received the response for a command RPC.
 */
struct CommandFinished {
  std::optional<std::string> error;
  nlohmann::json result;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CommandFinished, error, result);

using Event = std::variant<CommandFinished, RoomUpdated, PeerUpdated,
                           MessageReceived, RoomStatusChanged>;

}; // namespace server

namespace client {

struct UpdatePeer {
  std::vector<uint8_t> user_data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UpdatePeer, user_data);

struct UpdatePosition {
  std::vector<float> coordinates;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(UpdatePosition, coordinates);

struct StartMedia {
  uint16_t media_id;
  nlohmann::json properties;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StartMedia, media_id, properties);

struct StopMedia {
  uint16_t media_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StopMedia, media_id);

struct PauseMedia {
  uint16_t media_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PauseMedia, media_id);

struct ResumeMedia {
  uint16_t media_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ResumeMedia, media_id);

struct SendMessage {
  nlohmann::json::binary_t message;
  std::optional<std::vector<uint64_t>> target_peer_ids;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SendMessage, message,
                                                target_peer_ids);

// ─── SUPPORTED COMMANDS ──────────────────────────────────────────────────────

using Command = std::variant<UpdatePeer, UpdatePosition, StartMedia, StopMedia,
                             PauseMedia, ResumeMedia, SendMessage>;

} // namespace client

/**
 * Helper to combine multiple lambda functions for variant visitation.
 */

template <class... Ts> struct visitor : Ts... {
  using Ts::operator()...;
};
template <class... Ts> visitor(Ts...) -> visitor<Ts...>;

} // namespace api

namespace nlohmann {

template <> struct adl_serializer<api::server::Event> {
  static void from_json(const json &j, api::server::Event &e) {
    if (!j.is_array() || j.empty() || !j[0].is_number_integer()) {
      throw std::runtime_error("invalid rpc payload format");
    }
    int type = j.at(0).get<int>();
    switch (type) {
    case 1:
      // MessagePack-RPC Response Message [type, msgid, error, result]
      {
        if (j.size() < 4) {
          throw std::runtime_error("invalid rpc format");
        }
        e = api::server::CommandFinished{
            j[2].is_null()
                ? std::nullopt
                : std::optional<std::string>{j[2].get<std::string>()},
            j[3]};
      }
      break;
    case 2:
      // MessagePack-RPC Notification Message [type, method, params]
      {
        if (j.size() < 3) {
          throw std::runtime_error("invalid rpc format");
        }
        auto event_name = j.at(1).get<std::string>();
        json event_data = j.at(2);
        if (event_name == "RoomUpdated") {
          e = event_data.get<api::server::RoomUpdated>();
        } else if (event_name == "PeerUpdated") {
          e = event_data.get<api::server::PeerUpdated>();
        } else if (event_name == "MessageReceived") {
          e = event_data.get<api::server::MessageReceived>();
        } else if (event_name == "RoomStatusChanged") {
          e = event_data.get<api::server::RoomStatusChanged>();
        } else {
          throw std::runtime_error("unknown event name: " + event_name);
        }
      }
      break;
    default:
      throw std::runtime_error("unexpected rpc message type: " +
                               std::to_string(type));
    }
  }
};

template <> struct adl_serializer<api::client::Command> {
  static void to_json(json &j, const api::client::Command &c) {
    j = std::visit(
        api::visitor{
            [](api::client::UpdatePeer const &args) {
              return nlohmann::json::array({0, 0, "UpdatePeer", args});
            },
            [](api::client::UpdatePosition const &args) {
              return nlohmann::json::array({0, 0, "UpdatePosition", args});
            },
            [](api::client::StartMedia const &args) {
              return nlohmann::json::array({0, 0, "StartMedia", args});
            },
            [](api::client::StopMedia const &args) {
              return nlohmann::json::array({0, 0, "StopMedia", args});
            },
            [](api::client::PauseMedia const &args) {
              return nlohmann::json::array({0, 0, "PauseMedia", args});
            },
            [](api::client::ResumeMedia const &args) {
              return nlohmann::json::array({0, 0, "ResumeMedia", args});
            },
            [](api::client::SendMessage const &args) {
              return nlohmann::json::array({0, 0, "SendMessage", args});
            }},
        c);
  }
};

} // namespace nlohmann
