#include "common.h"
#include "textmenu.h"
#include "image.h"
#include "fonts.h"
#include "palette.h"
#include "event.h"
#include <algorithm>
#include <cstring>

void TextMenu::move(int direction)
{
    if (items.empty()) return;
    for (size_t i = 0; i < items.size(); ++i) {
        selected = (selected + direction + int(items.size())) % int(items.size());
        if (items[selected].enabled) return;
    }
}

void TextMenu::draw(image &target, JCFont &font, palette &colors,
                    const char *title, const char *subtitle, const char *hint)
{
    if (items.empty()) return;
    selected = std::clamp(selected, 0, int(items.size()) - 1);
    if (!items[selected].enabled) move(1);
    const int panel_width = std::min(288, target.Size().x - 16);
    const int x = (target.Size().x - panel_width) / 2;
    const int y = (target.Size().y - 192) / 2;
    left = x + 10;
    width = panel_width - 20;
    top = y + 43;
    row_height = font.height() + 4;
    const int background = colors.find_closest(10, 14, 20);
    const int panel = colors.find_closest(18, 24, 30);
    const int line = colors.find_closest(56, 66, 74);
    const int text = colors.find_closest(236, 239, 226);
    const int muted = colors.find_closest(158, 171, 176);
    const int disabled = colors.find_closest(91, 105, 111);
    const int accent = colors.find_closest(240, 169, 66);
    const int selected_fill = colors.find_closest(49, 61, 68);
    target.clear(background);
    target.bar(x, y, x + panel_width - 1, y + 191, panel);
    target.rectangle(x, y, x + panel_width - 1, y + 191, line);
    target.bar(x, y, x + 2, y + 191, accent);
    font.put_string(&target, left + 4, y + 10, title, text);
    font.put_string(&target, x + panel_width - 14 - int(strlen(subtitle)) * font.width(),
                    y + 10, subtitle, accent);
    target.line(left, y + 33, left + width - 1, y + 33, line);
    for (size_t i = 0; i < items.size(); ++i) {
        const auto &item = items[i];
        const int row_y = top + int(i) * row_height;
        const bool active = int(i) == selected;
        if (active) {
            target.bar(left, row_y, left + width - 1, row_y + row_height - 1, selected_fill);
            target.bar(left, row_y, left + 2, row_y + row_height - 1, accent);
            font.put_string(&target, left + 7, row_y + 2, ">", accent);
        }
        font.put_string(&target, left + 21, row_y + 2, item.label.c_str(),
                        !item.enabled ? disabled : active ? accent : text);
        if (!item.value.empty())
            font.put_string(&target, left + width - 9 - int(item.value.size()) * font.width(),
                            row_y + 2, item.value.c_str(), item.enabled ? muted : disabled);
    }
    target.line(left, y + 170, left + width - 1, y + 170, line);
    font.put_string(&target, left + 3, y + 175, hint, muted);
}

TextMenuAction TextMenu::handle(const event &input)
{
    if (items.empty()) return {};
    if (input.type == EV_MOUSE_BUTTON && !(input.mouse_button & LEFT_BUTTON)) return {};
    if (input.type == EV_KEY) {
        switch (input.key) {
        case JK_ESC: return {-1};
        case JK_UP: move(-1); break;
        case JK_DOWN: move(1); break;
        case JK_ENTER:
        case ' ':
            if (items[selected].enabled) return {items[selected].id};
            break;
        case JK_LEFT:
        case JK_RIGHT:
            if (items[selected].enabled && items[selected].adjustable)
                return {items[selected].id, input.key == JK_LEFT ? -1 : 1};
            break;
        }
    } else if ((input.type == EV_MOUSE_MOVE || input.type == EV_MOUSE_BUTTON)
               && input.mouse_move.x >= left && input.mouse_move.x < left + width
               && input.mouse_move.y >= top && input.mouse_move.y < top + int(items.size()) * row_height) {
        const int row = (input.mouse_move.y - top) / row_height;
        if (items[row].enabled) {
            selected = row;
            if (input.type == EV_MOUSE_BUTTON && (input.mouse_button & LEFT_BUTTON))
                return {items[row].id};
        }
    }
    return {};
}
