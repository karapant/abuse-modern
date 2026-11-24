// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
struct GameSettings
{
    int sfx_volume = 127, music_volume = 127;
    int difficulty = -1; // inherit the game data's default until explicitly selected
    int fullscreen = 1, scale = 3;
    int wheel_weapons = 1;
    int modern_hud = 1, enhanced_lighting = 1, checkpoint_feedback = 1;
    int smooth_pixels = 0;
    int enhanced_projectiles = 1;
    int auto_equip_pickups = -1; // inherit the campaign's default
    int directional_sound = 1;
    int combat_feedback = 1;
    int smart_crosshair = 1;
    int view_resolution = 1; // 480x300: more surroundings than the original view
};
enum class Setting { Effects, Music, Difficulty, Fullscreen, Scale, ViewResolution, WheelWeapons, ModernHud, EnhancedLighting, CheckpointFeedback, SmoothPixels, EnhancedProjectiles, AutoEquipPickups, DirectionalSound, CombatFeedback, SmartCrosshair };
extern GameSettings game_settings;
extern GameSettings saved_settings;
bool settings_load(const std::string &path, GameSettings &settings);
bool settings_write(const std::string &path, const GameSettings &settings);
bool settings_override(GameSettings &settings, const std::string &option, const std::string &value = "");
bool remember_setting(const std::string &path, GameSettings &saved, GameSettings &effective, Setting field, int value);
bool save_setting(Setting field, int value);

int view_width(int preset);
int view_height(int preset);
std::string view_label(int preset);
