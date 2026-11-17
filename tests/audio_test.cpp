// SPDX-License-Identifier: GPL-2.0-or-later
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "common.h"
#include "sdlport/sound.h"
#include "sdlport/setup.h"
#include "specs.h"
flags_struct flags = {};
static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "Audio test: %s (%s)\n", message, SDL_GetError()); exit(1); }
}
int main(int argc, char **argv)
{
    const bool intro = argc == 3;
    SDL_setenv_unsafe("TIMIDITY_CFG", intro ? argv[2] : "/nonexistent/abuse-timidity.cfg", 1);
    const int available = sound_init(0, nullptr);
    require((available & SFX_INITIALIZED) != 0, "effects initialize with dummy device");
    if (intro) {
        require((available & MUSIC_INITIALIZED) != 0, "valid instrument configuration enables MIDI decoder");
        set_filename_prefix(argv[1]);
        {
            song music("music/intro.hmi");
            music.play(64);
            require(music.playing(), "original HMI intro converts, decodes and starts playing");
            music.set_volume(32); music.stop();
            require(!music.playing(), "music stops cleanly");
        }
    } else {
        require((available & MUSIC_INITIALIZED) == 0, "missing patches disable music without claiming damaged assets");
        const auto root = std::filesystem::current_path() / "audio-fixture";
        std::filesystem::create_directories(root);
        const auto wav = root / "test.wav";
        std::ofstream file(wav, std::ios::binary);
        const auto number = [&](unsigned value, int bytes) { for (int i = 0; i < bytes; ++i) file.put((value >> (i * 8)) & 255); };
        file.write("RIFF", 4); number(36 + 400, 4); file.write("WAVEfmt ", 8);
        number(16, 4); number(1, 2); number(1, 2); number(22050, 4); number(44100, 4);
        number(2, 2); number(16, 2); file.write("data", 4); number(400, 4);
        for (int i = 0; i < 200; ++i) number(0, 2);
        file.close();
        set_filename_prefix(root.c_str());
        { sound_effect effect("test.wav"); effect.play(); SDL_Delay(10); }
        std::filesystem::remove_all(root);
    }
    sound_uninit(); set_filename_prefix(nullptr); SDL_Quit();
    puts(intro ? "Original HMI intro playback test passed." : "Missing MIDI patches and independent WAV effects test passed.");
}
