// SPDX-License-Identifier: GPL-2.0-or-later
#include "common.h"
#include "textentry.h"
#include "event.h"
TextEntry::Result TextEntry::handle(const event &input)
{
    if (input.type != EV_KEY) return Result::Editing;
    if (input.key == JK_ESC) return Result::Cancelled;
    if (input.key == JK_ENTER) return Result::Accepted;
    if (input.key == JK_BACKSPACE && !value.empty()) value.pop_back();
    else if (input.key >= 32 && input.key <= 126 && value.size() < limit) value += char(input.key);
    return Result::Editing;
}
