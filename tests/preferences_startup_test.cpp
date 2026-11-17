// SPDX-License-Identifier: GPL-2.0-or-later
// Two processes verify the real startup path using an isolated configuration.
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "common.h"
#include "sdlport/preferences.h"
#include "sdlport/setup.h"
#include "loader2.h"
void setup(int, char **);
void start_sound(int, char **);
extern flags_struct flags;
static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "Startup preferences: %s\n", message); exit(1); }
}
int main(int argc, char **argv)
{
    require(argc == 2, "write/read test mode");
    const auto root = std::filesystem::current_path() / "preferences-startup-fixture";
    const auto path = (root / "settings.cfg").string();
    if (std::string(argv[1]) == "write") {
        std::filesystem::remove_all(root); std::filesystem::create_directories(root / "assets");
        std::ofstream(root / "assets/abuse.lsp");
        // Bad legacy settings must not prevent startup or override the new settings.
        std::ofstream(root / "abuserc") << "fullscreen=invalid\nscale=0\nmono=bad\ngrabmouse=-3\n";
        GameSettings saved;
        saved.sfx_volume = 44; saved.music_volume = 55; saved.difficulty = 2; saved.fullscreen = 1; saved.scale = 4;
        require(settings_write(path, saved), "prepare saved preferences");
        return 0;
    }
    const auto saves = root.string() + "/";
    SDL_setenv_unsafe("ABUSE_SAVE_PATH", saves.c_str(), 1); SDL_unsetenv_unsafe("ABUSE_PATH");
    std::string assets = (root / "assets").string();
    const char *args[] = {"preferences-startup-test", "-datadir", assets.c_str(), "-windowed", "-scale", "2",
                          "-sfx_volume", "127", "-music_volume", "0", "-difficulty", "extreme", "-nosound"};
    const int argument_count = int(sizeof(args) / sizeof(args[0]));
    setup(argument_count, const_cast<char **>(args));
    start_sound(argument_count, const_cast<char **>(args));
    require(flags.fullscreen == 0 && flags.xres == 640 && flags.yres == 400, "saved display settings load, then CLI overrides apply");
    require(sfx_volume == 127 && music_volume == 0 && game_settings.difficulty == 3, "audio startup and difficulty honor overrides");
    require(save_setting(Setting::Music, 66), "change one setting after startup");
    GameSettings next;
    require(settings_load(path, next), "reload settings");
    require(next.scale == 4 && next.fullscreen == 1 && next.sfx_volume == 44 && next.music_volume == 66 && next.difficulty == 2,
            "real startup does not persist launch-only overrides");
    std::filesystem::remove_all(root);
    puts("Cross-process startup preferences checks passed.");
}
