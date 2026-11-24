// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdlport/preferences.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "Preferences: %s\n", message); exit(1); }
}
int main()
{
    const auto root = std::filesystem::current_path() / "preferences-fixture";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);
    const auto path = (root / "settings.cfg").string();
    GameSettings saved;
    require(saved.modern_hud && saved.enhanced_lighting && saved.checkpoint_feedback &&
            saved.combat_feedback && saved.smart_crosshair, "enhancements default to on");
    saved.modern_hud = 0; saved.enhanced_lighting = 0; saved.checkpoint_feedback = 0;
    saved.combat_feedback = 0; saved.smart_crosshair = 0;
    saved.sfx_volume = 17; saved.music_volume = 0; saved.difficulty = 3; saved.fullscreen = 1; saved.scale = 8; saved.view_resolution = 3; saved.wheel_weapons = 0;
    require(settings_write(path, saved), "write settings");
    GameSettings restarted;
    require(settings_load(path, restarted), "load settings after restart");
    require(!restarted.modern_hud && !restarted.enhanced_lighting && !restarted.checkpoint_feedback &&
            !restarted.combat_feedback && !restarted.smart_crosshair, "disabled enhancements survive restart");
    require(restarted.sfx_volume == 17 && restarted.music_volume == 0 && restarted.difficulty == 3 && restarted.fullscreen == 1 && restarted.scale == 8 && restarted.view_resolution == 3 && restarted.wheel_weapons == 0,
            "all fields survive restart, including mute and maximum scale");
    std::ofstream(root / "invalid.cfg") << "# comment\nscale=0\nscale=999999999999999999999\nsfx_volume=-1\nmusic_volume=128\ndifficulty=4\nfullscreen=2\nview_resolution=4\nwheel_weapons=2\nscale=3junk\nunknown=5\n";
    require(settings_load((root / "invalid.cfg").string(), restarted), "read invalid configuration without terminating");
    require(restarted.sfx_volume == 17 && restarted.music_volume == 0 && restarted.difficulty == 3 && restarted.fullscreen == 1 && restarted.scale == 8 && restarted.view_resolution == 3 && restarted.wheel_weapons == 0,
            "invalid values retain valid defaults");
    std::ofstream(root / "partial.cfg") << "  scale = 2 \r\n; comment\n music_volume = 63\n";
    require(settings_load((root / "partial.cfg").string(), restarted) && restarted.scale == 2 && restarted.music_volume == 63 && restarted.sfx_volume == 17,
            "partial files accept whitespace and preserve unspecified settings");
    GameSettings effective = saved;
    require(settings_override(effective, "-windowed") && settings_override(effective, "-scale", "2") &&
            settings_override(effective, "-sfx_volume", "127") && settings_override(effective, "-music_volume", "0") &&
            settings_override(effective, "-difficulty", "easy"), "valid command-line overrides");
    require(effective.fullscreen == 0 && effective.scale == 2 && effective.sfx_volume == 127 && effective.music_volume == 0 && effective.difficulty == 0,
            "command-line overrides win for this launch");
    require(!settings_override(effective, "-scale", "0") && !settings_override(effective, "-music_volume", "128") &&
            !settings_override(effective, "-difficulty", "impossible") && effective.scale == 2, "invalid overrides have no partial effects");
    require(remember_setting(path, saved, effective, Setting::Music, 44), "save only changed menu value");
    GameSettings next;
    settings_load(path, next);
    require(next.music_volume == 44 && next.scale == 8 && next.fullscreen == 1 && next.difficulty == 3 && next.sfx_volume == 17 && next.view_resolution == 3 && next.wheel_weapons == 0,
            "saving music does not make unrelated command-line overrides permanent");
    require(effective.scale == 2 && effective.difficulty == 0 && effective.music_volume == 44, "menu edits preserve other effective overrides");
    require(settings_override(effective, "-view", "640x360") && effective.view_resolution == 3 &&
            !settings_override(effective, "-view", "999x999"), "view presets validate command-line resolution");
    const auto before = saved;
    require(!remember_setting((root / "missing/settings.cfg").string(), saved, effective, Setting::Scale, 5), "write failure reported");
    require(saved.scale == before.scale && effective.scale == 2, "failed save retains previous runtime and stored values");
    require(!std::filesystem::exists(path + ".tmp"), "atomic save leaves no temporary file");
    std::filesystem::remove_all(root);
    puts("Preferences persistence, fallback, overrides and write-failure checks passed.");
}
