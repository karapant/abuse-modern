// SPDX-License-Identifier: GPL-2.0-or-later
#include "save_slots.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>

std::string save_slot_filename(int number)
{
    if (number < 1 || number > 15) return {};
    char name[32];
    std::snprintf(name, sizeof(name), "save%04d.spe", number);
    return name;
}

// Older builds could capture the picker. Match its side margins and accent stripe
// at the historical render sizes, following the original fixed-point resampler.
static bool legacy_menu_preview(const SaveSlot &slot)
{
    if (slot.width != 160 || slot.height != 100) return false;
    constexpr int sizes[][2] = {{320, 200}, {480, 300}, {640, 400}, {640, 360}};
    for (const auto &size : sizes) {
        const int stripe = ((size[0] - 288) / 2) * 160 / size[0];
        const int y_step = (size[1] << 16) / 100;
        const int panel_top = (size[1] - 192) / 2;
        const auto *middle = slot.preview.data() + 50 * 160;
        const auto background = middle[0], accent = middle[stripe], panel = middle[stripe + 2];
        if (accent == background || accent == panel || panel == background) continue;

        bool match = true;
        for (int y = 0; y < 100 && match; ++y) {
            const auto *row = slot.preview.data() + y * 160;
            const int original_y = (y * y_step) >> 16;
            const bool inside = original_y >= panel_top && original_y < panel_top + 192;
            match = row[0] == background && row[159] == background &&
                    row[stripe] == (inside ? accent : background);
            if (original_y > panel_top && original_y < panel_top + 191)
                match = match && row[stripe + 2] == panel;
        }
        if (match) return true;
    }
    return false;
}

static bool readable_save(const std::filesystem::path &path, SaveSlot &slot, bool details)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const auto length = file.tellg();
    if (length < 10) return false;
    file.seekg(0);
    const auto number = [&](int bytes) -> uint32_t {
        uint32_t n = 0;
        for (int i = 0; i < bytes; ++i) {
            int b = file.get();
            if (b < 0) return 0;
            n |= uint32_t(b) << (8 * i);
        }
        return n;
    };
    std::array<char, 8> magic{};
    file.read(magic.data(), magic.size());
    if (std::string(magic.data(), magic.size()) != std::string("SPEC1.0\0", 8)) return false;
    const auto count = number(2);
    if (!count || count > 4096) return false;
    std::set<std::string> required = {"fgmap", "bgmap", "object_list", "player_info", "thumb nail"};
    uint32_t first_data = uint32_t(length);
    for (uint32_t i = 0; i < count; ++i) {
        const int type = file.get(), name_size = file.get();
        if (type < 0 || name_size <= 0) return false;
        std::string name(name_size, '\0');
        file.read(name.data(), name.size());
        if (!file || name.back() != '\0' || file.get() != 0) return false;
        name.pop_back();
        uint32_t size = number(4), offset = number(4);
        if (!file || offset > uint64_t(length) || size > uint64_t(length) - offset) return false;
        if (size) first_data = std::min(first_data, offset);
        if (name == "thumb nail") {
            if (type != 4 || size < 4) return false;
            auto at = file.tellg();
            file.seekg(offset);
            uint32_t w = number(2), h = number(2);
            if (!file || !w || !h || w > 2048 || h > 2048 || uint64_t(w) * h + 4 > size) return false;
            if (details && w <= 320 && h <= 240) {
                slot.width = w;
                slot.height = w == 160 ? std::min(h, 100u) : h;
                slot.preview.resize(size_t(slot.width) * slot.height);
                file.read(reinterpret_cast<char *>(slot.preview.data()), slot.preview.size());
                if (!file) return false;
            }
            file.seekg(at);
        } else if (details && name == save_details_entry && size == save_details_size) {
            const auto at = file.tellg();
            file.seekg(offset);
            const auto difficulty = number(1);
            slot.play_ticks = number(4);
            slot.station = number(2);
            if (!file) return false;
            if (difficulty < 4) slot.difficulty = difficulty;
            file.seekg(at);
        } else if (details && name == "first name" && size >= 2 && size <= 256) {
            const auto at = file.tellg();
            file.seekg(offset);
            const auto len = number(1);
            if (len > 1 && len < size) {
                std::string name(len, '\0');
                file.read(name.data(), len);
                if (!file) return false;
                if (name.back() == '\0') {
                    name.pop_back();
                    const auto slash = name.find_last_of("/\\");
                    if (slash != std::string::npos) name.erase(0, slash + 1);
                    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".spe") == 0)
                        name.resize(name.size() - 4);
                    if (name.size() == 7 && name.compare(0, 5, "level") == 0 &&
                        name[5] >= '0' && name[5] <= '9' && name[6] >= '0' && name[6] <= '9')
                        name = "Level " + name.substr(5);
                    for (char &c : name)
                        if (c < 32 || c > 126) c = '?';
                    slot.level = std::move(name);
                }
            }
            file.seekg(at);
        }
        required.erase(name);
    }
    if (legacy_menu_preview(slot) ||
        std::all_of(slot.preview.begin(), slot.preview.end(), [](uint8_t pixel) { return pixel == 0; }))
        slot.preview.clear();
    return file && required.empty() && file.tellg() <= std::streamoff(first_data);
}
std::vector<SaveSlot> scan_save_slots(const std::string &directory, bool details)
{
    std::vector<SaveSlot> slots;
    for (int n = 1; n <= 15; ++n) {
        const auto path = std::filesystem::u8path(directory) / save_slot_filename(n);
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        SaveSlot slot{n, SaveSlotState::Empty, "Empty"};
        if (error || (exists && !readable_save(path, slot, details))) {
            slot = {n, SaveSlotState::Unreadable, "Unreadable"};
        } else if (exists) {
            slot.state = SaveSlotState::Saved;
            slot.description = "Saved";
            if (details) {
                const auto time = std::filesystem::last_write_time(path, error);
                if (!error) {
                    using namespace std::chrono;
                    const auto local = system_clock::to_time_t(
                        time_point_cast<system_clock::duration>(time - decltype(time)::clock::now() + system_clock::now()));
                    char formatted[24];
                    if (const auto *clock = std::localtime(&local))
                        if (std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M", clock))
                            slot.saved_at = formatted;
                }
            }
        }
        slots.push_back(std::move(slot));
    }
    return slots;
}
