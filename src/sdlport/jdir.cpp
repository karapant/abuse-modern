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

#if defined HAVE_CONFIG_H
#   include "config.h"
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>

void get_directory(char *path, char **&files, int &tfiles, char **&dirs, int &tdirs)
{
    files = nullptr;
    dirs = nullptr;
    tfiles = tdirs = 0;

    std::error_code error;
    std::filesystem::directory_iterator entries(std::filesystem::u8path(path), error);
    if (error) return;

    const auto append = [](char **&list, int &count, const char *name) {
        list = static_cast<char **>(realloc(list, sizeof(char *) * (count + 1)));
        list[count++] = strdup(name);
    };
    append(dirs, tdirs, ".");
    append(dirs, tdirs, "..");
    const std::filesystem::directory_iterator end;
    for (; entries != end && !error; entries.increment(error)) {
        const auto name = entries->path().filename().u8string();
        std::error_code status_error;
        if (entries->is_directory(status_error))
            append(dirs, tdirs, name.c_str());
        else
            append(files, tfiles, name.c_str());
    }
}
