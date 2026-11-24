// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
#include "common.h"
#include "textmenu.h"
#include "image.h"
#include "fonts.h"
#include "palette.h"
#include "event.h"
#include "menu_font_data.h"
#include "sdlport/platform.h"
#include "video.h"
#include <algorithm>

static const menu_font::Glyph &next_glyph(std::string_view &text, bool title)
{
    const char *next = text.data();
    size_t remaining = text.size();
    Uint32 character = SDL_StepUTF8(&next, &remaining);
    if (character == SDL_INVALID_UNICODE_CODEPOINT && next == text.data() + 1)
        character = static_cast<unsigned char>(text.front());
    text.remove_prefix(std::max<size_t>(1, text.size() - remaining));
    if (!(character >= 32 && character <= 126) && !(character >= 160 && character <= 255))
        character = '?';
    const auto index = character < 127 ? character - 32 : character - 160 + 95;
    return (title ? menu_font::title_glyphs : menu_font::body_glyphs)[index];
}

int MenuText::width(std::string_view text, bool title) const
{
    if (!smooth) return int(text.size()) * fallback.width();
    int advance = 0;
    while (!text.empty()) advance += next_glyph(text, title).advance;
    return (advance + 1) / 2;
}

int MenuText::height() const { return fallback.height(); }

std::string MenuText::fit(std::string_view text, int available, bool title) const
{
    if (width(text, title) <= available) return std::string(text);
    constexpr std::string_view ellipsis = "...";
    const int pixels = available - width(ellipsis, title);
    if (pixels < 0) return {};
    auto remaining = text;
    int used = 0;
    while (!remaining.empty()) {
        const size_t before = remaining.size();
        if (smooth) used += next_glyph(remaining, title).advance;
        else {
            used += fallback.width() * 2;
            remaining.remove_prefix(1);
        }
        if (used > pixels * 2) return std::string(text.substr(0, text.size() - before)) + std::string(ellipsis);
    }
    return std::string(text);
}

void MenuText::put(image &target, int x, int y, std::string_view text, int color, bool title) const
{
    if (!smooth) {
        fallback.put_string(&target, x, y, std::string(text).c_str(), color);
        return;
    }
    int pen = x * 2;
    const auto *masks = title ? menu_font::title_masks : menu_font::body_masks;
    while (!text.empty()) {
        const auto &glyph = next_glyph(text, title);
        platform_menu_glyph(pen + glyph.x, y * 2 + glyph.y, glyph.width, glyph.height,
                            masks + glyph.offset, color);
        pen += glyph.advance;
    }
}

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
    int clip_left, clip_top, clip_right, clip_bottom;
    target.GetClip(clip_left, clip_top, clip_right, clip_bottom);
    target.SetClip(0, 0, target.Size().x, target.Size().y);
    MenuText type(font, &target == screen && platform_begin_menu_text());
    selected = std::clamp(selected, 0, int(items.size()) - 1);
    if (!items[selected].enabled) move(1);
    if (selected < first_visible) first_visible = selected;
    if (selected >= first_visible + visible_rows) first_visible = selected - visible_rows + 1;
    first_visible = std::clamp(first_visible, 0, std::max(0, int(items.size()) - visible_rows));
    const int panel_width = std::min(288, target.Size().x - 16);
    const int x = (target.Size().x - panel_width) / 2;
    const int y = (target.Size().y - 192) / 2;
    left = x + 10;
    width = panel_width - 20;
    if (draw_preview) width = panel_width / 2 - 18;
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
    type.put(target, left + 4, y + 9, title, text, true);
    const auto section = type.fit(subtitle, panel_width - 44 - type.width(title, true), true);
    type.put(target, x + panel_width - 14 - type.width(section, true), y + 9, section, accent, true);
    target.line(left, y + 33, x + panel_width - 11, y + 33, line);
    for (int i = first_visible; i < std::min(int(items.size()), first_visible + visible_rows); ++i) {
        const auto &item = items[i];
        const int row_y = top + (i - first_visible) * row_height;
        const bool active = int(i) == selected;
        if (active) {
            target.bar(left, row_y, left + width - 1, row_y + row_height - 1, selected_fill);
            target.bar(left, row_y, left + 2, row_y + row_height - 1, accent);
            type.put(target, left + 7, row_y + 1, ">", accent);
        }
        const auto value = type.fit(item.value, width / 2);
        const int value_width = type.width(value);
        const auto label = type.fit(item.label, width - 30 - value_width - (value.empty() ? 0 : 8));
        type.put(target, left + 21, row_y + 1, label, !item.enabled ? disabled : active ? accent : text);
        if (!value.empty())
            type.put(target, left + width - 9 - value_width, row_y + 1, value, item.enabled ? muted : disabled);
    }
    if (int(items.size()) > visible_rows) {
        const int track = visible_rows * row_height;
        const int thumb_top = top + first_visible * track / int(items.size());
        const int thumb_size = std::max(4, track * visible_rows / int(items.size()));
        const int scrollbar = left + width + 4;
        target.bar(scrollbar, top, scrollbar + 1, top + track - 1, line);
        target.bar(scrollbar, thumb_top, scrollbar + 1, thumb_top + thumb_size - 1, accent);
    }
    if (draw_preview) {
        const int preview_x = left + width + 14;
        draw_preview(target, type, colors, items[selected].id, preview_x, top, x + panel_width - 10 - preview_x);
    }
    target.line(left, y + 170, x + panel_width - 11, y + 170, line);
    type.put(target, left + 3, y + 174, type.fit(hint, panel_width - 26), muted);
    target.SetClip(clip_left, clip_top, clip_right, clip_bottom);
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
        case JK_PAGEUP: for (int i = 0; i < visible_rows; ++i) move(-1); break;
        case JK_PAGEDOWN: for (int i = 0; i < visible_rows; ++i) move(1); break;
        case JK_HOME: selected = int(items.size()) - 1; move(1); break;
        case JK_END: selected = 0; move(-1); break;
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
               && input.mouse_move.y >= top && input.mouse_move.y < top + std::min(int(items.size()), visible_rows) * row_height) {
        const int row = first_visible + (input.mouse_move.y - top) / row_height;
        if (items[row].enabled) {
            selected = row;
            if (input.type == EV_MOUSE_BUTTON && (input.mouse_button & LEFT_BUTTON))
                return {items[row].id};
        }
    }
    return {};
}
