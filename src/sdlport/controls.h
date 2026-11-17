// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include "setup.h"

enum class ControlAction { Left, Right, Jump, Use, Fire, Special, Previous, Next, Count };
constexpr int control_count = int(ControlAction::Count);
extern keys_struct keys;
int &control_binding(ControlAction action);
const char *control_label(ControlAction action);
std::string control_key_name(int key);
bool control_key_allowed(int key);
// Conflicting keys swap actions, keeping all actions reachable.
bool control_rebind(ControlAction action, int key);
void control_reset();
bool controls_load(const std::string &path);
bool controls_save(const std::string &path);
struct ControllerOptions { bool enabled = true; int deadzone = 8000; };
extern ControllerOptions controller_options;
