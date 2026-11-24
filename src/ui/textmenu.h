// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class image;
class JCFont;
class palette;
class event;

class MenuText
{
public:
    MenuText(JCFont &fallback, bool smooth) : fallback(fallback), smooth(smooth) {}
    int width(std::string_view text, bool title = false) const;
    int height() const;
    std::string fit(std::string_view text, int available, bool title = false) const;
    void put(image &target, int x, int y, std::string_view text, int color, bool title = false) const;
private:
    JCFont &fallback;
    bool smooth;
};

struct TextMenuItem
{
    int id;
    std::string label;
    std::string value;
    bool enabled = true;
    bool adjustable = false;
};

struct TextMenuAction
{
    int id = 0; // zero: no action, -1: Back/Escape
    int direction = 1;
};

// Shared layout keeps the visible row and its mouse target identical.
class TextMenu
{
public:
    std::vector<TextMenuItem> items;
    std::function<void(image &, const MenuText &, palette &, int, int, int, int)> draw_preview;
    int selected = 0;
    int first_visible = 0;
    static constexpr int visible_rows = 7;
    void draw(image &target, JCFont &font, palette &colors,
              const char *title, const char *subtitle, const char *hint);
    TextMenuAction handle(const event &input);
private:
    int left = 0, top = 0, width = 0, row_height = 0;
    void move(int direction);
};
