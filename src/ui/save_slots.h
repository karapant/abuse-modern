// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
enum class SaveSlotState { Empty, Saved, Unreadable };
struct SaveSlot { int number; SaveSlotState state; std::string description; };
std::string save_slot_filename(int number);
std::vector<SaveSlot> scan_save_slots(const std::string &directory);
