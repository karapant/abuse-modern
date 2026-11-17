// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
class event;
class TextEntry
{
public:
    enum class Result { Editing, Accepted, Cancelled };
    std::string value;
    explicit TextEntry(std::string initial, size_t maximum = 24) : value(initial.substr(0, maximum)), limit(maximum) {}
    Result handle(const event &input);
private:
    size_t limit;
};
