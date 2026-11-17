// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
struct NetworkOptions
{
    std::string player = "Player", game = "Abuse game";
    int min_players = 2, max_players = 8, kills = 25, level_set = 0, port = 20202;
};
std::string validate_network_options(const NetworkOptions &options);
bool valid_join_address(const std::string &address);
bool save_network_options(const std::string &directory, const NetworkOptions &options, bool host);
