// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <SDL3/SDL.h>
struct ControllerState
{
    int x = 0, y = 0;
    bool jump = false, use = false, fire = false, special = false;
    bool aim = false;
    float aim_x = 0, aim_y = 0;
};
bool controller_init();
void controller_shutdown();
void controller_event(const SDL_Event &input);
ControllerState controller_state();
const char *controller_name();
void controller_focus(bool focused);
void controller_mouse_used();
bool controller_requires_two_sticks();
bool controller_take_warning();
void controller_suppress_until_neutral();
// Translate a recognized controller event to a menu key or gameplay shortcut.
int controller_key(const SDL_Event &input, bool gameplay);

// Some dialogs (save consoles) run while the simulation is in RUN_STATE.
struct ControllerMenuScope
{
    ControllerMenuScope();
    ~ControllerMenuScope();
    ControllerMenuScope(const ControllerMenuScope &) = delete;
    ControllerMenuScope &operator=(const ControllerMenuScope &) = delete;
};
bool controller_in_menu();
