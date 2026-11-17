// SPDX-License-Identifier: GPL-2.0-or-later
#include "preferences.h"
#include "specs.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

GameSettings game_settings, saved_settings;
namespace {
struct Field { const char *name; int GameSettings::*member; int low, high; };
const std::array<Field, 7> fields = {{
    {"sfx_volume", &GameSettings::sfx_volume, 0, 127},
    {"music_volume", &GameSettings::music_volume, 0, 127},
    {"difficulty", &GameSettings::difficulty, -1, 3},
    {"fullscreen", &GameSettings::fullscreen, 0, 1},
    {"scale", &GameSettings::scale, 1, 8},
    {"view_resolution", &GameSettings::view_resolution, 0, 3},
    {"wheel_weapons", &GameSettings::wheel_weapons, 0, 1}}};
bool integer(const std::string &text, int &value)
{
    std::istringstream input(text);
    char extra;
    return bool(input >> value) && !(input >> extra);
}
std::string trim(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
bool valid(const Field &field, int value) { return value >= field.low && value <= field.high; }
}
bool settings_load(const std::string &path, GameSettings &settings)
{
    std::ifstream input(path);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const auto name = trim(line.substr(0, equal));
        int value;
        if (!integer(line.substr(equal + 1), value)) continue;
        for (const auto &field : fields)
            if (name == field.name && valid(field, value)) settings.*(field.member) = value;
    }
    return true;
}
bool settings_write(const std::string &path, const GameSettings &settings)
{
    for (const auto &field : fields) if (!valid(field, settings.*(field.member))) return false;
    const auto temporary = path + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) return false;
    output << "# Abuse Modern settings\n";
    for (const auto &field : fields) output << field.name << '=' << settings.*(field.member) << '\n';
    output.close();
    std::error_code error;
    if (!output) { std::filesystem::remove(temporary, error); return false; }
    std::filesystem::rename(temporary, path, error);
    if (error) { std::filesystem::remove(temporary, error); return false; }
    return true;
}
bool settings_override(GameSettings &settings, const std::string &option, const std::string &text)
{
    if (option == "-fullscreen" || option == "-windowed") {
        settings.fullscreen = option == "-fullscreen"; return true;
    }
    if (option == "-view") {
        for (int i = 0; i < 4; ++i) if (text == view_label(i)) { settings.view_resolution = i; return true; }
        return false;
    }
    if (option == "-difficulty") {
        const std::array<std::string, 4> names = {"easy", "normal", "hard", "extreme"};
        for (size_t i = 0; i < names.size(); ++i) if (text == names[i]) { settings.difficulty = int(i); return true; }
        return false;
    }
    for (const auto &field : fields) if (option == std::string("-") + field.name && option != "-difficulty") {
        int value;
        if (!integer(text, value) || !valid(field, value)) return false;
        settings.*(field.member) = value; return true;
    }
    return false;
}
bool remember_setting(const std::string &path, GameSettings &saved, GameSettings &effective, Setting setting, int value)
{
    const auto index = size_t(setting);
    if (index >= fields.size() || !valid(fields[index], value)) return false;
    GameSettings next = saved;
    next.*(fields[index].member) = value;
    if (!settings_write(path, next)) return false;
    saved = next;
    effective.*(fields[index].member) = value;
    return true;
}
bool save_setting(Setting setting, int value)
{
    const auto directory = get_save_filename_prefix();
    return directory && *directory && remember_setting(std::string(directory) + "settings.cfg", saved_settings, game_settings, setting, value);
}

int view_width(int preset) { return preset == 0 ? 320 : preset == 1 ? 480 : 640; }
int view_height(int preset) { return preset == 0 ? 200 : preset == 1 ? 300 : preset == 2 ? 400 : 360; }
std::string view_label(int preset) { return std::to_string(view_width(preset)) + "x" + std::to_string(view_height(preset)); }
