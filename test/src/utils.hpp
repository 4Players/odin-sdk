/*
 * 4Players ODIN Sample Utilities
 *
 * Shared helpers used by all sample clients: logging and error-check macros,
 * command-line argument handling, access key persistence and local room token
 * generation.
 *
 * Copyright (c) 4Players GmbH. Licensed under the MIT License; see LICENSE-MIT.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <cxxopts.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <odin.h>

#define ODIN_ACCESS_KEY_FILE "odin_access_key.txt"
#define ODIN_DEFAULT_GW_ADDR "gateway.odin.4players.io"
#define ODIN_DEFAULT_ROOM_ID "default"
#define ODIN_DEFAULT_USER_ID "My User ID"

template <class T> using OpaquePtr = std::unique_ptr<T, void (*)(T *)>;

/**
 * Custom macros that support formatting with variadic arguments.
 */
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARNING(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...)                                                      \
  do {                                                                         \
    spdlog::critical(__VA_ARGS__);                                             \
    std::exit(EXIT_FAILURE);                                                   \
  } while (0)

/**
 * Custom macro to execute the given expression (which should be a valid API
 * call from the ODIN Voice SDK and checks its result. It is intended for
 * scenarios where a failure is considered critical.
 */
#define CHECK(expr)                                                            \
  do {                                                                         \
    const OdinError odin_check_error = (expr);                                 \
    if (odin_check_error != ODIN_ERROR_SUCCESS) {                              \
      LOG_CRITICAL(#expr " failed: {}", odin_error_get_last_error());          \
    }                                                                          \
  } while (false)

/**
 * Global namespace for application-wide variables.
 */
namespace global {
inline std::optional<cxxopts::ParseResult> arguments;
} // namespace global

/**
 * Parses the specified command-line options and stores the result in a
 * global variable. This also handles the common `--version` and `--help`
 * options, which print the requested information and exit.
 */
inline void parse_arguments(cxxopts::Options &options, int argc, char *argv[]) {
  try {
    global::arguments.emplace(options.parse(argc, argv));
  } catch (const cxxopts::exceptions::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (global::arguments->count("version")) {
    std::cout << options.program() << " (SDK " << ODIN_VERSION ")" << std::endl;
    exit(EXIT_SUCCESS);
  }

  if (global::arguments->count("help")) {
    std::cout << options.help() << std::endl;
    exit(EXIT_SUCCESS);
  }
}

/**
 * Queries the globally stored command-line arguments to determine if the
 * specified option is present.
 */
inline bool has_argument(std::string name) {
  return !!global::arguments->count(name.data());
}

/**
 * Template function to fetch the value associated with the given argument
 * name from the globally parsed command-line options and convert it to the
 * specified type `T`.
 */
template <typename T> T get_argument(std::string name) {
  return (*global::arguments)[name].as<T>();
}

/**
 * Reads an ODIN access key from the specified file if it exists.
 */
inline std::error_code read_access_key_file(const std::filesystem::path &path,
                                            std::string &data) {
  std::ifstream file(path, std::ios::binary);
  if (file) {
    std::ostringstream ss;
    ss << file.rdbuf();
    if (!file.good() && !file.eof()) {
      return std::make_error_code(std::errc::io_error);
    }
    data = ss.str();
  }
  return std::error_code{};
}

/**
 * Writes an ODIN access key to the specified file.
 */
inline std::error_code write_access_key_file(const std::filesystem::path &path,
                                             const std::string &data) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return std::make_error_code(std::errc::no_such_file_or_directory);
  }
  file.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!file) {
    return std::make_error_code(std::errc::io_error);
  }
  return std::error_code{};
}

/**
 * Creates an `OdinTokenGenerator` instance. If the provided access key is
 * non-empty, it is used to create the token generator. Otherwise, a new
 * access key is generated (and stored in the provided string) during
 * creation.
 */
inline OpaquePtr<OdinTokenGenerator>
get_token_generator(std::string &access_key) {
  OdinTokenGenerator *token_generator;
  if (!access_key.empty()) {
    CHECK(odin_token_generator_create(access_key.data(), &token_generator));
  } else {
    CHECK(odin_token_generator_create(nullptr, &token_generator));
    char out_access_key[128];
    uint32_t out_access_key_length = sizeof(out_access_key) - 1;
    CHECK(odin_token_generator_get_access_key(token_generator, out_access_key,
                                              &out_access_key_length));
    access_key = std::string(out_access_key, out_access_key_length);
  }

  return {token_generator, &odin_token_generator_free};
}

/**
 * Constructs a JSON payload with the audience, room ID, user ID and
 * validity timestamps, then signs it using the provided token generator to
 * produce a JWT for authentication in the ODIN network.
 */
inline std::string
generate_token(OpaquePtr<OdinTokenGenerator> &token_generator,
               const std::string &room_id, const std::string &user_id) {
  auto nbf = time(nullptr);
  auto exp = nbf + 300; /* 5 minutes */

  nlohmann::json claims = {
      {"rid", room_id},
      {"uid", user_id},
      {"nbf", nbf},
      {"exp", exp},
  };

  if (has_argument("bypass-gateway")) {
    claims.update({
        {"adr", get_argument<std::string>("server-url")},
        {"aud", "sfu"},
        {"cid", "<no_customer>"},
    });
  }

  std::string token(1024, '\0');
  uint32_t token_length = static_cast<uint32_t>(token.size());
  CHECK(odin_token_generator_sign(token_generator.get(), claims.dump().c_str(),
                                  &token[0], &token_length));
  token.resize(token_length);

  return token;
}
