// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 2001 Anthony Kruize <trandor@labyrinth.net.au>
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#include "config.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "specs.h"
#include "keys.h"
#include "setup.h"
#include "controls.h"
#include "controller.h"

flags_struct flags = {};
keys_struct keys = {};
extern int xres, yres;
static int scale = 3;

void showHelp()
{
    printf("Abuse Modern %s (SDL3)\n"
           "Usage: abuse [options]\n"
           "  -datadir PATH    External Abuse game data\n"
           "  -scale N         Initial window scale (1-8)\n"
           "  -size W H        Editor framebuffer size (320-4096 x 200-2160)\n"
           "  -fullscreen      Desktop fullscreen (F11 toggles)\n"
           "  -nosound         Disable audio\n"
           "  -mono            Disable stereo panning\n"
           "  -edit            Open the original level editor\n"
           "  -f FILE          Load a map\n"
           "  -a NAME          Load an addon\n"
           "  -h, --help       Show this help\n"
           "F12 toggles mouse grab; Print Screen saves screenshot.bmp.\n",
           PACKAGE_VERSION);
}

static int number(const char *text, int low, int high, const char *option)
{
    char *end = nullptr;
    errno = 0;
    long n = strtol(text, &end, 10);
    if (errno || end == text || *end || n < low || n > high)
    {
        fprintf(stderr, "%s must be between %d and %d\n", option, low, high);
        exit(EXIT_FAILURE);
    }
    return int(n);
}

void createRCFile(const char *path)
{
    std::ofstream out(path);
    out << "; Abuse Modern configuration\n"
           "fullscreen=0\nmono=0\ngrabmouse=0\nscale=3\n"
           "left=a\nright=d\nup=w\ndown=s\n"
           "fire=SPACE\nspecial=SHIFT_L\nweapprev=CTRL_R\nweapnext=INSERT\n";
}

void readRCFile()
{
    std::string path = std::string(get_save_filename_prefix()) + "abuserc";
    std::ifstream in(path);
    if (!in) { createRCFile(path.c_str()); return; }
    std::string line;
    while (std::getline(in, line))
    {
        auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == ';' || line[0] == '#') continue;
        std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        const auto trim = [](std::string &s) {
            auto first = s.find_first_not_of(" \t\r");
            if (first == std::string::npos) { s.clear(); return; }
            s = s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
        };
        trim(key); trim(value);
        if (key.empty() || value.empty()) continue;
        if (key == "fullscreen") flags.fullscreen = number(value.c_str(), 0, 1, "fullscreen");
        else if (key == "mono") flags.mono = number(value.c_str(), 0, 1, "mono");
        else if (key == "grabmouse") flags.grabmouse = number(value.c_str(), 0, 1, "grabmouse");
        else if (key == "scale") scale = number(value.c_str(), 1, 8, "scale");
        else if (key == "datadir") set_filename_prefix(value.c_str());
        else if (key == "left") keys.left = key_value(value.c_str());
        else if (key == "right") keys.right = key_value(value.c_str());
        else if (key == "up") keys.up = key_value(value.c_str());
        else if (key == "down") keys.down = key_value(value.c_str());
        else if (key == "fire") keys.b2 = key_value(value.c_str());
        else if (key == "special") keys.b1 = key_value(value.c_str());
        else if (key == "weapprev") keys.b3 = key_value(value.c_str());
        else if (key == "weapnext") keys.b4 = key_value(value.c_str());
    }
}

void parseCommandLine(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const auto arg = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s requires an argument\n", name);
                exit(EXIT_FAILURE);
            }
            return argv[++i];
        };
        if (!strcmp(argv[i], "-fullscreen")) flags.fullscreen = 1;
        else if (!strcmp(argv[i], "-nosound")) flags.nosound = 1;
        else if (!strcmp(argv[i], "-mono")) flags.mono = 1;
        else if (!strcmp(argv[i], "-scale")) scale = number(arg("-scale"), 1, 8, "-scale");
        else if (!strcmp(argv[i], "-size")) {
            xres = number(arg("-size"), 320, 4096, "width");
            yres = number(arg("-size"), 200, 2160, "height");
        }
        else if (!strcmp(argv[i], "-datadir")) {
            const char *path = arg("-datadir");
            if (!*path) { fprintf(stderr, "-datadir cannot be empty\n"); exit(EXIT_FAILURE); }
            set_filename_prefix(path);
        }
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { showHelp(); exit(0); }
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "-a")) {
            const char *option = argv[i];
            arg(option); // validate, then leave interpretation to the game/editor
        }
        // Other arguments belong to the inherited game/editor parser.
    }
}

void setup(int argc, char **argv)
{
    // Help and version work without an installed display or audio device.
    for (int i = 1; i < argc; ++i)
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { showHelp(); exit(0); }
    xres = 320; yres = 200;
    keys.left = 'a'; keys.right = 'd'; keys.up = 'w'; keys.down = 's';
    keys.b2 = key_value("SPACE"); keys.b1 = key_value("SHIFT_L");
    keys.b3 = key_value("CTRL_R"); keys.b4 = key_value("INSERT");
    set_filename_prefix(ASSETDIR);
    // A separate SDL preference directory avoids overwriting the original game's settings.
    const char *save = getenv("ABUSE_SAVE_PATH");
    char *allocated = save ? nullptr : SDL_GetPrefPath("abuse-modern", "abuse-modern");
    const char *pref = save ? save : allocated;
    if (!pref || !*pref) {
        fprintf(stderr, "Cannot determine save directory: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    if (!SDL_CreateDirectory(pref)) {
        fprintf(stderr, "Cannot create save directory: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    std::string save_path(pref);
    SDL_free(allocated);
    if (save_path.back() != '/') save_path += '/';
    set_save_filename_prefix(save_path.c_str());
    readRCFile();
    controls_load(std::string(get_save_filename_prefix()) + "controls.cfg");
    parseCommandLine(argc, argv);
    if (const char *assets = getenv("ABUSE_PATH"); assets && *assets) set_filename_prefix(assets);
    const std::string startup = std::string(get_filename_prefix()) + "abuse.lsp";
    if (!std::ifstream(startup)) {
        fprintf(stderr, "Cannot find Abuse game data at %s. Use -datadir PATH.\n", get_filename_prefix());
        exit(EXIT_FAILURE);
    }
    flags.xres = xres * scale;
    flags.yres = yres * scale;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Cannot initialize SDL3: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    atexit(SDL_Quit);
    if (controller_init()) atexit(controller_shutdown);
    printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

int get_key_binding(char const *dir, int i)
{
    if( strcasecmp( dir, "left" ) == 0 )
        return keys.left;
    else if( strcasecmp( dir, "right" ) == 0 )
        return keys.right;
    else if( strcasecmp( dir, "up" ) == 0 )
        return keys.up;
    else if( strcasecmp( dir, "down" ) == 0 )
        return keys.down;
    else if( strcasecmp( dir, "b1" ) == 0 )
        return keys.b1;
    else if( strcasecmp( dir, "b2" ) == 0 )
        return keys.b2;
    else if( strcasecmp( dir, "b3" ) == 0 )
        return keys.b3;
    else if( strcasecmp( dir, "b4" ) == 0 )
        return keys.b4;

    return 0;
}
