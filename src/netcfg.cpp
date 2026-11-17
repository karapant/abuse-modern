// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#include "common.h"
#include "netcfg.h"
#include "menu.h"
#include "ui/textmenu.h"
#include "ui/network_options.h"
#include "game.h"
#include "dev.h"
#include "net/sock.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

extern net_protocol *prot;
extern char *get_login();
extern char game_name[50];
extern char lsf[256];
extern int start_running;
net_configuration *main_net_cfg = nullptr;

net_configuration::net_configuration()
{
    snprintf(name, sizeof(name), "%s", get_login() ? get_login() : "Player");
    strcpy(server_name, "127.0.0.1:20202");
    min_players = 2; max_players = 8; kills = 25;
    port = server_port = 20202; state = SINGLE_PLAYER;
}
void net_configuration::cfg_error(const char *message) { text_notice("INPUT ERROR", message); }
void net_configuration::error(const char *message) { text_notice("NETWORK ERROR", message); }
int net_configuration::restart_state()
{
    return state == RESTART_SERVER || state == RESTART_CLIENT || state == RESTART_SINGLE;
}
int net_configuration::notify_reset()
{
    if (state == RESTART_SERVER) state = SERVER;
    else if (state == RESTART_CLIENT) state = CLIENT;
    else if (state == RESTART_SINGLE) state = SINGLE_PLAYER;
    return 1;
}

int net_configuration::get_options(int server)
{
    NetworkOptions draft;
    draft.player = name; draft.game = *game_name ? game_name : "Abuse game";
    draft.min_players = min_players; draft.max_players = max_players;
    draft.kills = kills; draft.port = server ? port : server_port;
    TextMenu menu;
    for (;;) {
        const char *sets[] = {"Small", "Medium", "Large"};
        menu.items = {{1, "Player name", draft.player}};
        if (server) {
            menu.items.push_back({2, "Game name", draft.game});
            menu.items.push_back({3, "Minimum players", std::to_string(draft.min_players) + " < >", true, true});
            menu.items.push_back({4, "Maximum players", std::to_string(draft.max_players) + " < >", true, true});
            menu.items.push_back({5, "Kills to win", std::to_string(draft.kills) + " < >", true, true});
            menu.items.push_back({6, "Map set", std::string(sets[draft.level_set]) + " < >", true, true});
        }
        menu.items.push_back({7, "Port", std::to_string(draft.port)});
        menu.items.push_back({8, server ? "Start hosting" : "Join game", ""});
        menu.items.push_back({9, "Cancel", ""});
        const auto action = run_text_menu(menu, server ? "HOST GAME" : "JOIN GAME", "ENTER EDITS   LEFT / RIGHT ADJUST");
        if (action.id == -1 || action.id == 9) return 0;
        if (action.id == 1) text_entry("PLAYER NAME", "Name", draft.player);
        else if (action.id == 2) text_entry("GAME NAME", "Name", draft.game);
        else if (action.id == 3) draft.min_players = std::clamp(draft.min_players + action.direction, 1, 8);
        else if (action.id == 4) draft.max_players = std::clamp(draft.max_players + action.direction, 2, 8);
        else if (action.id == 5) draft.kills = std::clamp(draft.kills + action.direction, 1, 99);
        else if (action.id == 6) draft.level_set = (draft.level_set + action.direction + 3) % 3;
        else if (action.id == 7) {
            std::string value = std::to_string(draft.port);
            if (text_entry("NETWORK PORT", "Port", value, 5)) {
                if (value.empty() || !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    cfg_error("Use a port number between 1 and 65533.");
                else draft.port = std::stoi(value);
            }
        } else if (action.id == 8) {
            const auto problem = validate_network_options(draft);
            if (!problem.empty()) { cfg_error(problem.c_str()); continue; }
            if (server && !std::filesystem::exists(std::filesystem::path(get_filename_prefix()) / "addon/deathmat/deathmat.lsp")) {
                error("The game data does not include the deathmatch addon."); continue;
            }
            if (!save_network_options(get_save_filename_prefix(), draft, server != 0)) {
                error("Could not write network configuration to your save directory."); continue;
            }
            snprintf(name, sizeof(name), "%s", draft.player.c_str());
            if (server) {
                snprintf(game_name, 50, "%s", draft.game.c_str());
                min_players = draft.min_players; max_players = draft.max_players; kills = draft.kills;
                port = draft.port;
                strcpy(lsf, "addon/deathmat/deathmat.lsp");
            } else server_port = draft.port;
            return 1;
        }
    }
}

int net_configuration::input()
{
    if (!prot) { error("No network protocol is available."); return 0; }
    TextMenu menu;
    for (;;) {
        menu.items = {{1, "Host game", ""}, {2, "Find LAN games", ""}, {3, "Join address", ""}};
        if (main_net_cfg && (main_net_cfg->state == SERVER || main_net_cfg->state == CLIENT))
            menu.items.push_back({4, "Return to single player", ""});
        menu.items.push_back({5, "Back", ""});
        auto action = run_text_menu(menu, "MULTIPLAYER", "ARROWS / MOUSE   ENTER TO SELECT");
        if (action.id == -1 || action.id == 5) return 0;
        if (action.id == 1) {
            if (get_options(1)) { state = RESTART_SERVER; return 1; }
        } else if (action.id == 4) {
            state = RESTART_SINGLE; start_running = 0; strcpy(lsf, "abuse.lsp"); return 1;
        } else if (action.id == 3) {
            std::string address = server_name;
            if (!text_entry("JOIN ADDRESS", "Host:port", address, 64)) continue;
            if (!valid_join_address(address)) { cfg_error("Enter an IPv4 address or hostname, optionally followed by :port."); continue; }
            const auto colon = address.find(':');
            if (colon != std::string::npos) server_port = std::stoi(address.substr(colon + 1));
            if (get_options(0)) {
                address = address.substr(0, colon) + ":" + std::to_string(server_port);
                snprintf(server_name, sizeof(server_name), "%s", address.c_str());
                state = RESTART_CLIENT; return 1;
            }
        } else if (action.id == 2) {
            struct Discovery { std::string name; std::unique_ptr<net_address> address; };
            std::vector<Discovery> found;
            // Scan for a bounded interval, with a visible Cancel action.
            TextMenu discovery;
            discovery.items = {{0, "Searching on local network...", "", false}, {100, "Cancel", ""}};
            const auto start = SDL_GetTicks();
            bool cancelled = false;
            while (SDL_GetTicks() - start < 2500 && found.size() < 9) {
                discovery.draw(*screen, *big_font, *pal, "ABUSE MODERN", "FIND GAMES", "ESC CANCELS   SCAN TAKES 2.5 SECONDS");
                wm->flush_screen();
                if (wm->event_waiting()) {
                    event input; wm->get_event(input);
                    const auto choice = discovery.handle(input);
                    if (choice.id == -1 || choice.id == 100) { cancelled = true; break; }
                }
                char label[256] = {};
                std::unique_ptr<net_address> address(prot->find_address(0x9090, label));
                if (address) {
                    address->set_port(server_port);
                    found.push_back({label, std::move(address)});
                    discovery.items[0].label = "Games found: " + std::to_string(found.size());
                }
                SDL_Delay(10);
            }
            prot->reset_find_list();
            if (cancelled) continue;
            if (found.empty()) { text_notice("NO GAMES", "No LAN games found. You can also join a host directly using Join address."); continue; }
            TextMenu results;
            for (size_t i = 0; i < found.size(); ++i) results.items.push_back({int(i) + 1, found[i].name, "Join"});
            results.items.push_back({100, "Back", ""});
            const auto selected = run_text_menu(results, "LAN GAMES", "ENTER JOINS   ESC GOES BACK");
            if (selected.id == -1 || selected.id == 100) continue;
            if (get_options(0)) {
                found.at(selected.id - 1).address->set_port(server_port);
                found.at(selected.id - 1).address->store_string(server_name, sizeof(server_name));
                state = RESTART_CLIENT; return 1;
            }
        }
    }
}
