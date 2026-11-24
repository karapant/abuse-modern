// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#ifndef GRUE_MENU_HPP
#define GRUE_MENU_HPP
#include "fonts.h"

int menu(void *args, JCFont *font);             // reurns -1 on esc
class TextMenu;
struct TextMenuAction;
#include <string>
void confirm_fullscreen_change();
TextMenuAction run_text_menu(TextMenu &menu, const char *section, const char *hint);
void text_notice(const char *title, const std::string &message);
bool text_entry(const char *title, const char *label, std::string &value, size_t maximum = 24);
void main_menu();
int text_confirm_quit();

#endif
