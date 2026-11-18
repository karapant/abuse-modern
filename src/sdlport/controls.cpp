// SPDX-License-Identifier: GPL-2.0-or-later
#include "controls.h"
#include "keys.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

ControllerOptions controller_options;
static const std::array<const char *, control_count> labels = {
    "Move left", "Move right", "Jump", "Use / crouch", "Fire", "Special", "Previous weapon", "Next weapon"};
int &control_binding(ControlAction action)
{
    switch (action) {
    case ControlAction::Left: return keys.left;
    case ControlAction::Right: return keys.right;
    case ControlAction::Jump: return keys.up;
    case ControlAction::Use: return keys.down;
    case ControlAction::Fire: return keys.b2;
    case ControlAction::Special: return keys.b1;
    case ControlAction::Previous: return keys.b3;
    case ControlAction::Next: return keys.b4;
    default: return keys.left;
    }
}
const char *control_label(ControlAction action) { return labels.at(int(action)); }
std::string control_key_name(int key)
{
    char name[64] = {};
    if (key >= 0 && key <= JK_MAX_KEY) key_name(key, name);
    return *name ? name : "Unbound";
}
bool control_key_allowed(int key)
{
    // These remain available for menu navigation and the game's fixed help/weapon shortcuts.
    return ((key >= 32 && key <= 126) || (key >= 256 && key <= JK_MAX_KEY)) &&
        key != JK_F1 && !(key >= '1' && key <= '7') && key != ',' && key != '.';
}
bool control_rebind(ControlAction action, int key)
{
    if (int(action) < 0 || int(action) >= control_count || !control_key_allowed(key)) return false;
    int &target = control_binding(action);
    const int old = target;
    for (int i = 0; i < control_count; ++i)
        if (i != int(action) && control_binding(ControlAction(i)) == key)
            control_binding(ControlAction(i)) = old;
    target = key;
    return true;
}
void control_reset()
{
    keys = {'a', 'd', 'w', 's', JK_SHIFT_L, JK_SPACE, JK_CTRL_R, JK_INSERT};
    controller_options = {};
}
bool controls_load(const std::string &path)
{
    std::ifstream in(std::filesystem::u8path(path));
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos || line.empty() || line[0] == '#') continue;
        const auto name = line.substr(0, equal);
        std::istringstream value(line.substr(equal + 1));
        int n; char trailing;
        if (!(value >> n) || (value >> trailing)) continue;
        if (name == "controller_enabled" && (n == 0 || n == 1)) controller_options.enabled = n;
        else if (name == "controller_deadzone" && n >= 2000 && n <= 16000) controller_options.deadzone = n;
        else for (int i = 0; i < control_count; ++i)
            if (name == "key" + std::to_string(i)) control_rebind(ControlAction(i), n);
    }
    return true;
}
bool controls_save(const std::string &path)
{
    const auto temporary = std::filesystem::u8path(path + ".tmp");
    std::ofstream out(temporary, std::ios::trunc);
    if (!out) return false;
    for (int i = 0; i < control_count; ++i) out << "key" << i << '=' << control_binding(ControlAction(i)) << '\n';
    out << "controller_enabled=" << controller_options.enabled << '\n'
        << "controller_deadzone=" << controller_options.deadzone << '\n';
    out.close();
    if (!out) { std::filesystem::remove(temporary); return false; }
    std::error_code error;
    std::filesystem::rename(temporary, std::filesystem::u8path(path), error);
    if (error) std::filesystem::remove(temporary);
    return !error;
}
