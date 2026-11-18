// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

inline bool rooted_filename(const char *name)
{
#ifdef _WIN32
    return std::filesystem::u8path(name).has_root_path();
#else
    return name[0] == '/';
#endif
}

inline int open_binary_file(const char *name, int flags, int permissions)
{
#ifdef _WIN32
    int mode = (permissions & 0400 ? _S_IREAD : 0) | (permissions & 0200 ? _S_IWRITE : 0);
    return ::_wopen(std::filesystem::u8path(name).c_str(), flags | _O_BINARY, mode);
#else
    return ::open(name, flags, permissions);
#endif
}

inline bool readable_file(const char *name)
{
#ifdef _WIN32
    return ::_waccess(std::filesystem::u8path(name).c_str(), 4) == 0;
#else
    return ::access(name, R_OK) == 0;
#endif
}

inline FILE *open_utf8_file(const char *name, const char *mode)
{
#ifdef _WIN32
    return ::_wfopen(std::filesystem::u8path(name).c_str(), std::filesystem::u8path(mode).c_str());
#else
    return std::fopen(name, mode);
#endif
}
