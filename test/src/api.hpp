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

} // namespace nlohmann

namespace api {

// ─── BASIC TYPES ───────────────────────────────────────────────────────────

using PeerId = uint32_t;

using PeerUserData = nlohmann::json;

using Parameters = nlohmann::json::object_t;

using Message = nlohmann::json::binary_t;

using ChannelMask = uint64_t;

// ─── SUPPORTED EVENTS --──────────────────────────────────────────────────────

namespace server {

struct Joined {
  PeerId own_peer_id;
  std::string room_id;
  std::string customer;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Joined, own_peer_id, room_id, customer);

struct Left {
  std::string reason;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Left, reason);

struct PeerJoined {
  PeerId peer_id;
  std::string user_id;
  std::optional<PeerUserData> user_data;
  std::optional<std::vector<std::string>> tags;
  std::optional<Parameters> parameters;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PeerJoined, peer_id, user_id,
                                                user_data, tags, parameters);

struct PeerLeft {
  PeerId peer_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PeerLeft, peer_id);

struct PeerChanged {
  PeerId peer_id;
  std::optional<PeerUserData> user_data;
  std::optional<std::vector<std::string>> tags;
  std::optional<Parameters> parameters;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PeerChanged, peer_id, user_data,
                                                tags, parameters);

struct NewReconnectToken {
  std::string token;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(NewReconnectToken, token);

struct MessageReceived {
  PeerId sender_peer_id;
  Message message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MessageReceived, sender_peer_id, message);

struct RoomStatusChanged {
  std::string status;
  std::optional<std::string> message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RoomStatusChanged, status,
                                                message);

struct Error {
  std::string message;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Error, message);

using Event =
    std::variant<Joined, Left, PeerJoined, PeerLeft, PeerChanged,
                 NewReconnectToken, MessageReceived, RoomStatusChanged, Error>;

}; // namespace server

// ─── SUPPORTED COMMANDS ──────────────────────────────────────────────────────

namespace client {

struct ChangeSelf {
  PeerUserData user_data;
  std::optional<Parameters> parameters;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChangeSelf, user_data);

struct SetChannelMasks {
  std::vector<std::pair<PeerId, ChannelMask>> masks;
  bool reset;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SetChannelMasks, masks, reset);

struct SendMessage {

  nlohmann::json::binary_t message;
  std::optional<std::vector<PeerId>> peer_ids;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SendMessage, message, peer_ids);

using Command = std::variant<ChangeSelf, SetChannelMasks, SendMessage>;

} // namespace client

template <class... Ts> struct visitor : Ts... {
  using Ts::operator()...;
};
template <class... Ts> visitor(Ts...) -> visitor<Ts...>;

} // namespace api

namespace nlohmann {

template <> struct adl_serializer<api::server::Event> {
  static void from_json(const json &j, api::server::Event &e) {
    if (!j.is_object() || j.size() != 1) {
      throw std::runtime_error("invalid rpc payload format");
    }
    const auto &event_name = j.cbegin().key();
    const auto &event_data = j.cbegin().value();
    if (event_name == "Joined") {
      e = event_data.get<api::server::Joined>();
    } else if (event_name == "Left") {
      e = event_data.get<api::server::Left>();
    } else if (event_name == "PeerJoined") {
      e = event_data.get<api::server::PeerJoined>();
    } else if (event_name == "PeerLeft") {
      e = event_data.get<api::server::PeerLeft>();
    } else if (event_name == "PeerChanged") {
      e = event_data.get<api::server::PeerChanged>();
    } else if (event_name == "NewReconnectToken") {
      e = event_data.get<api::server::NewReconnectToken>();
    } else if (event_name == "MessageReceived") {
      e = event_data.get<api::server::MessageReceived>();
    } else if (event_name == "RoomStatusChanged") {
      e = event_data.get<api::server::RoomStatusChanged>();
    } else if (event_name == "Error") {
      e = event_data.get<api::server::Error>();
    } else {
      throw std::runtime_error("unknown event name: " + event_name);
    }
  }
};

template <> struct adl_serializer<api::client::Command> {
  static void to_json(json &j, const api::client::Command &c) {
    j = std::visit(api::visitor{[](api::client::ChangeSelf const &args) {
                                  return json{{"ChangeSelf", args}};
                                },
                                [](api::client::SetChannelMasks const &args) {
                                  return json{{"SetChannelMasks", args}};
                                },
                                [](api::client::SendMessage const &args) {
                                  return json{{"SendMessage", args}};
                                }},
                   c);
  }
};

} // namespace nlohmann
