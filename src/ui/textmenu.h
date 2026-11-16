#pragma once
#include <string>
#include <vector>

class image;
class JCFont;
class palette;
class event;

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
    int selected = 0;
    void draw(image &target, JCFont &font, palette &colors,
              const char *title, const char *subtitle, const char *hint);
    TextMenuAction handle(const event &input);
private:
    int left = 0, top = 0, width = 0, row_height = 0;
    void move(int direction);
};
