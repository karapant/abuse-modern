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
#include "loadgame.h"
#include "menu.h"
#include "ui/textmenu.h"
#include "ui/save_slots.h"
#include "game.h"
#include "id.h"
#include <cstdio>
#include <string>

int last_save_game_number = 0;
void load_number_icons() {} // save slots now use text rather than numbered icon assets
void last_savegame_name(char *buf)
{
    sprintf(buf, "%s%s", get_save_filename_prefix(),
            save_slot_filename((last_save_game_number + 14) % 15 + 1).c_str());
}
void get_savegame_name(char *buf)
{
    sprintf(buf, "%s", save_slot_filename((last_save_game_number++) % 15 + 1).c_str());
}
int show_load_icon()
{
    for (const auto &slot : scan_save_slots(get_save_filename_prefix()))
        if (slot.state == SaveSlotState::Saved) return 1;
    return 0;
}
int get_save_spot() { return load_game(1, "SAVE GAME"); }
int load_game(int show_all, const char *)
{
    TextMenu menu;
    for (;;) {
        const auto slots = scan_save_slots(get_save_filename_prefix());
        menu.items.clear();
        for (const auto &slot : slots) {
            char label[32]; snprintf(label, sizeof(label), "Slot %02d", slot.number);
            menu.items.push_back({slot.number, label, slot.description,
                                  show_all || slot.state == SaveSlotState::Saved});
        }
        menu.items.push_back({100, "Back", ""});
        auto action = run_text_menu(menu, show_all ? "SAVE GAME" : "LOAD GAME", "ARROWS / SCROLL   ENTER SELECT   ESC BACK");
        if (action.id == -1 || action.id == 100) { the_game->reset_keymap(); return 0; }
        const auto &slot = slots.at(action.id - 1);
        if (show_all && slot.state != SaveSlotState::Empty) {
            TextMenu confirm;
            confirm.items = {{1, "Cancel", ""}, {2, "Overwrite slot " + std::to_string(action.id), ""}};
            if (run_text_menu(confirm, "OVERWRITE?", "ENTER CONFIRMS   ESC CANCELS").id != 2) continue;
        }
        the_game->reset_keymap();
        return action.id;
    }
}
