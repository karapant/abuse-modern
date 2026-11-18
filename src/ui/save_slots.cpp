// SPDX-License-Identifier: GPL-2.0-or-later
#include "save_slots.h"
#include <array>
#include <cstdint>
#include <cstdio>
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
static bool readable_save(const std::filesystem::path &path)
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
            file.seekg(at);
        }
        required.erase(name);
    }
    return file && required.empty() && file.tellg() <= std::streamoff(first_data);
}
std::vector<SaveSlot> scan_save_slots(const std::string &directory)
{
    std::vector<SaveSlot> slots;
    for (int n = 1; n <= 15; ++n) {
        const auto path = std::filesystem::u8path(directory) / save_slot_filename(n);
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        const auto state = error ? SaveSlotState::Unreadable : !exists ? SaveSlotState::Empty :
            readable_save(path) ? SaveSlotState::Saved : SaveSlotState::Unreadable;
        slots.push_back({n, state, state == SaveSlotState::Empty ? "Empty" : state == SaveSlotState::Saved ? "Saved" : "Unreadable"});
    }
    return slots;
}
