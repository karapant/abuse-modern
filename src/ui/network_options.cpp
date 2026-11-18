// SPDX-License-Identifier: GPL-2.0-or-later
#include "network_options.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>
static bool valid_name(const std::string &name)
{
    return !name.empty() && name.size() <= 24 && name.find_first_not_of(' ') != std::string::npos &&
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= 32 && c <= 126 && c != '"' && c != '\\'; });
}
std::string validate_network_options(const NetworkOptions &o)
{
    if (!valid_name(o.player)) return "Player name must use 1-24 characters, without quotes or backslashes.";
    if (!valid_name(o.game)) return "Game name must use 1-24 characters, without quotes or backslashes.";
    if (o.min_players < 1 || o.min_players > 8 || o.max_players < 2 || o.max_players > 8 || o.min_players > o.max_players)
        return "Players must be between 1 and 8, with maximum at least minimum.";
    if (o.kills < 1 || o.kills > 99) return "Kills to win must be between 1 and 99.";
    if (o.level_set < 0 || o.level_set > 2) return "Select a valid map set.";
    if (o.port < 1 || o.port > 65533) return "Port must be between 1 and 65533; the engine also uses the next two ports.";
    return {};
}
bool valid_join_address(const std::string &address)
{
    if (address.empty() || address.size() > 96) return false;
    const auto colon = address.find(':');
    const std::string host = address.substr(0, colon);
    if (host.empty() || !std::all_of(host.begin(), host.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '.'; })) return false;
    if (colon != std::string::npos) {
        const auto port = address.substr(colon + 1);
        if (port.empty() || port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); })) return false;
        int value = std::stoi(port);
        if (value < 1 || value > 65533) return false;
    }
    return true;
}
bool save_network_options(const std::string &directory, const NetworkOptions &o, bool host)
{
    if (!validate_network_options(o).empty()) return false;
    std::error_code error;
    const auto path = std::filesystem::u8path(directory) / "config";
    std::filesystem::create_directories(path, error);
    if (error) return false;
    std::vector<std::pair<std::string, std::string>> files = {
        {"username.lsp", "(setq username \"" + o.player + "\")\n"}};
    if (host) {
        const char *sets[] = {"small.lsp", "medium.lsp", "large.lsp"};
        files.push_back({"gamename.lsp", "(setq gamename \"" + o.game + "\")\n"});
        files.push_back({"levelset.lsp", std::string("(load \"addon/deathmat/") + sets[o.level_set] + "\")\n"});
    }
    for (const auto &file : files) {
        std::ofstream out(path / (file.first + ".tmp"), std::ios::trunc);
        out << file.second; out.close();
        if (!out) return false;
    }
    for (const auto &file : files) {
        std::filesystem::rename(path / (file.first + ".tmp"), path / file.first, error);
        if (error) return false;
    }
    return true;
}
