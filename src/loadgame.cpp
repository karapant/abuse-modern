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
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

int last_save_game_number = 0;
static int checkpoint_slot = 0;
int last_checkpoint_slot() { return checkpoint_slot; }
void reset_checkpoint_slot() { checkpoint_slot = 0; }

void record_checkpoint_save(const char *filename)
{
    for (int n = 1; n <= 15; ++n)
        if (save_slot_filename(n) == filename) {
            checkpoint_slot = n;
            return;
        }
}

bool load_checkpoint_slot(int slot)
{
    if (slot < 1 || slot > 15) return false;
    const std::string directory = get_save_filename_prefix();
    if (scan_save_slots(directory, false)[slot - 1].state != SaveSlotState::Saved)
        return false;
    const auto path = directory + save_slot_filename(slot);
    the_game->load_level(path.c_str());
    checkpoint_slot = slot;
    the_game->reset_keymap();
    the_game->set_state(RUN_STATE);
    return true;
}
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
    for (const auto &slot : scan_save_slots(get_save_filename_prefix(), false))
        if (slot.state == SaveSlotState::Saved) return 1;
    return 0;
}
int get_save_spot()
{
    // Restore the scene underneath the modal picker.
    std::unique_ptr<image> gameplay(screen->copy());
    const int slot = load_game(1, "SAVE GAME");
    int left, top, right, bottom;
    screen->GetClip(left, top, right, bottom);
    screen->SetClip(0, 0, screen->Size().x, screen->Size().y);
    gameplay->put_image(screen, 0, 0);
    screen->SetClip(left, top, right, bottom);
    return slot;
}

static void draw_slot_preview(image &target, const MenuText &font, palette &colors, const SaveSlot &slot,
                              int x, int y, int width)
{
    const int text = colors.find_closest(236, 239, 226);
    const int muted = colors.find_closest(158, 171, 176);
    if (slot.state != SaveSlotState::Saved) {
        font.put(target, x, y + 5, slot.description, muted);
        return;
    }
    const auto title = slot.level.empty() ? std::string("Saved game") : slot.level;
    font.put(target, x, y, font.fit(title, width), text);
    font.put(target, x, y + font.height() + 4, font.fit(slot.saved_at, width), muted);
    y += font.height() * 2 + 10;
    int preview_space = 82;
    if (slot.difficulty >= 0) {
        static const char *difficulty[] = {"Easy", "Normal", "Hard", "Extreme"};
        const unsigned seconds = slot.play_ticks / gameplay_ticks_per_second;
        char duration[24];
        snprintf(duration, sizeof(duration), "%u:%02u:%02u", seconds / 3600, seconds / 60 % 60, seconds % 60);
        font.put(target, x, y, font.fit(std::string(difficulty[slot.difficulty]) + "   " + duration, width), muted);
        y += font.height() + 2;
        preview_space -= font.height() + 2;
    }
    if (slot.station != no_save_station) {
        font.put(target, x, y, font.fit("Checkpoint " + std::to_string(slot.station), width), muted);
        y += font.height() + 2;
        preview_space -= font.height() + 2;
    }
    if (slot.preview.empty()) {
        font.put(target, x, y, font.fit("Preview unavailable", width), muted);
        return;
    }
    const int preview_width = std::max(1, std::min(width, preview_space * slot.width / slot.height));
    const int preview_height = std::max(1, preview_width * slot.height / slot.width);
    for (int row = 0; row < preview_height; ++row) {
        const auto *source = slot.preview.data() + row * slot.height / preview_height * slot.width;
        auto *dest = target.scan_line(y + row) + x;
        for (int col = 0; col < preview_width; ++col)
            dest[col] = source[col * slot.width / preview_width];
    }
}

int load_game(int show_all, const char *)
{
    TextMenu menu;
    for (;;) {
        const auto slots = scan_save_slots(get_save_filename_prefix());
        menu.items.clear();
        menu.draw_preview = [&](image &target, const MenuText &font, palette &colors, int id, int x, int y, int width) {
            if (id >= 1 && id <= 15) draw_slot_preview(target, font, colors, slots[id - 1], x, y, width);
        };
        for (const auto &slot : slots) {
            char label[32]; snprintf(label, sizeof(label), "Slot %02d", slot.number);
            const char *badge = !show_all || slot.state == SaveSlotState::Empty ? "" :
                                slot.state == SaveSlotState::Saved ? "Saved" : "Error";
            menu.items.push_back({slot.number, label, badge,
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
