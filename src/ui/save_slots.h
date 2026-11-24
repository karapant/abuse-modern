// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
enum class SaveSlotState { Empty, Saved, Unreadable };
inline constexpr char save_details_entry[] = "save details.v1";
inline constexpr int save_details_size = 7;
inline constexpr uint16_t no_save_station = 0xffff;
struct SaveSlot {
    int number;
    SaveSlotState state;
    std::string description;
    std::string level, saved_at;
    int width = 0, height = 0;
    std::vector<uint8_t> preview;
    int difficulty = -1;
    uint32_t play_ticks = 0;
    uint16_t station = no_save_station;
};
std::string save_slot_filename(int number);
std::vector<SaveSlot> scan_save_slots(const std::string &directory, bool details = true);
