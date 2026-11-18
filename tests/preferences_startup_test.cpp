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
#include "video.h"
#include "sdlport/platform.h"
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
    const bool startup = std::string(argv[1]) == "defaults" || std::string(argv[1]) == "fullscreen";
    const auto root = std::filesystem::current_path() / (startup ? std::string("preferences-") + argv[1] + "-fixture" : "preferences-startup-fixture");
    const auto path = (root / "settings.cfg").string();
    if (startup) {
        std::filesystem::remove_all(root); std::filesystem::create_directories(root / "assets");
        std::ofstream(root / "assets/abuse.lsp");
        const bool restore_smoothing = std::string(argv[1]) == "fullscreen";
        if (restore_smoothing) {
            std::ofstream(root / "abuserc") << "fullscreen=0\n";
            GameSettings old; old.fullscreen = 0;
            old.smooth_pixels = 1;
            require(settings_write(path, old), "prepare previous windowed preference");
        }
        SDL_setenv_unsafe("ABUSE_SAVE_PATH", root.string().c_str(), 1); SDL_unsetenv_unsafe("ABUSE_PATH");
        std::string assets = (root / "assets").string();
        const char *args[] = {"preferences-startup-test", "-datadir", assets.c_str()};
        setup(3, const_cast<char **>(args));
        require(flags.fullscreen == 1 && xres == 480 && yres == 300, "new configurations start zoomed out in fullscreen");
        set_mode(0);
        int width, height;
        require(SDL_GetRenderOutputSize(platform_renderer(), &width, &height), "read native output resolution");
        const auto *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(platform_window()));
        require(mode && width == mode->w && height == mode->h && SDL_GetWindowFullscreenMode(platform_window()) == nullptr,
                "fullscreen uses native desktop pixels without an exclusive display mode");
        require(game_settings.smooth_pixels == restore_smoothing, "restore saved pixel filter at startup");
        const auto screenshot = (root / "startup.bmp").string();
        require(platform_screenshot(screenshot.c_str()), "capture the startup frame");
        SDL_Surface *frame = SDL_LoadBMP(screenshot.c_str());
        const int scale = restore_smoothing ? 2 : 1;
        require(frame && frame->w == xres * scale && frame->h == yres * scale, "startup uses the configured pixel filter");
        SDL_DestroySurface(frame);
        close_graphics(); std::filesystem::remove_all(root);
        return 0;
    }
    if (std::string(argv[1]) == "write") {
        std::filesystem::remove_all(root); std::filesystem::create_directories(root / "assets");
        std::ofstream(root / "assets/abuse.lsp");
        // Bad legacy settings must not prevent startup or override the new settings.
        std::ofstream(root / "abuserc") << "fullscreen=invalid\nscale=0\nmono=bad\ngrabmouse=-3\n";
        GameSettings saved;
        saved.sfx_volume = 44; saved.music_volume = 55; saved.difficulty = 2; saved.fullscreen = 1; saved.scale = 4; saved.view_resolution = 2;
        require(settings_write(path, saved), "prepare saved preferences");
        return 0;
    }
    const auto saves = root.string() + "/";
    SDL_setenv_unsafe("ABUSE_SAVE_PATH", saves.c_str(), 1); SDL_unsetenv_unsafe("ABUSE_PATH");
    std::string assets = (root / "assets").string();
    const char *args[] = {"preferences-startup-test", "-datadir", assets.c_str(), "-windowed", "-view", "320x200", "-scale", "2",
                          "-sfx_volume", "127", "-music_volume", "0", "-difficulty", "extreme", "-nosound"};
    const int argument_count = int(sizeof(args) / sizeof(args[0]));
    setup(argument_count, const_cast<char **>(args));
    start_sound(argument_count, const_cast<char **>(args));
    require(flags.fullscreen == 0 && flags.xres == 640 && flags.yres == 400, "saved display settings load, then CLI overrides apply");
    require(sfx_volume == 127 && music_volume == 0 && game_settings.difficulty == 3, "audio startup and difficulty honor overrides");
    require(save_setting(Setting::Music, 66), "change one setting after startup");
    GameSettings next;
    require(settings_load(path, next), "reload settings");
    require(next.scale == 4 && next.fullscreen == 1 && next.sfx_volume == 44 && next.music_volume == 66 && next.difficulty == 2 && next.view_resolution == 2,
            "real startup does not persist launch-only overrides");
    std::filesystem::remove_all(root);
    puts("Cross-process startup preferences checks passed.");
}
