#pragma once

#include "core/game-types.hpp"
#include "net/session.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/// Narrow interface for UIManager — exposes only the subset of App
/// that the UI layer actually needs.
struct UIControl {
    std::function<SessionMode()> get_session_mode;
    std::function<double()> get_fps;

    // Dialogue
    std::function<void(std::string const &)> do_dialogue_action;
    std::function<void()> end_dialogue;
    std::function<std::string(std::string const &, int,
                               std::unordered_map<std::string, std::string> const &)>
        resolve_dialogue_text;

    // Session management
    std::function<void()> start_local_session;
    std::function<void(int)> start_host_session;
    std::function<void(std::string const &, int)> start_client_session;
    std::function<void()> stop_listen;
    std::function<void(SessionMode)> set_session_mode;
    std::function<std::vector<std::shared_ptr<Session>> const &()> server_sessions;
    std::function<void(std::shared_ptr<Session>, std::string const &)> kick_session;

    // Save / Load
    std::function<std::vector<int>()> available_save_slots;
    std::function<void(int)> load_from_slot;
};
