// Modified for Abuse Modern on 2026-11-17; modifications licensed GPL-2.0-or-later.
/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 2001 Anthony Kruize <trandor@labyrinth.net.au>
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include "common.h"
#include "image.h"
#include "palette.h"
#include "video.h"
#include "mouse.h"
#include "event.h"
#include "game.h"
#include "platform.h"
#include "preferences.h"
#include "controller.h"

extern int get_key_binding(const char *, int);
static std::array<int, SDL_SCANCODE_COUNT> pressed = {};
static SDL_MouseButtonFlags held_buttons = 0;

static int game_key(const SDL_KeyboardEvent &key)
{
    switch (key.key) {
    case SDLK_DOWN: return JK_DOWN;
    case SDLK_UP: return JK_UP;
    case SDLK_LEFT: return JK_LEFT;
    case SDLK_RIGHT: return JK_RIGHT;
    case SDLK_LCTRL: return JK_CTRL_L;
    case SDLK_RCTRL: return JK_CTRL_R;
    case SDLK_LALT: return JK_ALT_L;
    case SDLK_RALT: return JK_ALT_R;
    case SDLK_LSHIFT: return JK_SHIFT_L;
    case SDLK_RSHIFT: return JK_SHIFT_R;
    case SDLK_NUMLOCKCLEAR: return JK_NUM_LOCK;
    case SDLK_HOME: return JK_HOME;
    case SDLK_END: return JK_END;
    case SDLK_BACKSPACE: return JK_BACKSPACE;
    case SDLK_TAB: return JK_TAB;
    case SDLK_RETURN: return JK_ENTER;
    case SDLK_SPACE: return JK_SPACE;
    case SDLK_CAPSLOCK: return JK_CAPS;
    case SDLK_ESCAPE: return JK_ESC;
    case SDLK_INSERT: return JK_INSERT;
    case SDLK_DELETE: return JK_DEL;
    case SDLK_LGUI:
    case SDLK_RGUI: return JK_COMMAND;
    case SDLK_PAGEUP: return JK_PAGEUP;
    case SDLK_PAGEDOWN: return JK_PAGEDOWN;
    case SDLK_KP_0: return JK_INSERT;
    case SDLK_KP_8: return JK_UP;
    case SDLK_KP_2: return JK_DOWN;
    case SDLK_KP_4: return JK_LEFT;
    case SDLK_KP_6: return JK_RIGHT;
    case SDLK_F1: return JK_F1;
    case SDLK_F2: return JK_F2;
    case SDLK_F3: return JK_F3;
    case SDLK_F4: return JK_F4;
    case SDLK_F5: return JK_F5;
    case SDLK_F6: return JK_F6;
    case SDLK_F7: return JK_F7;
    case SDLK_F8: return JK_F8;
    case SDLK_F9: return JK_F9;
    case SDLK_F10: return JK_F10;
    default: break;
    }
    int value = int(key.key);
    if (value < 32 || value > 126) return 0; // legacy key-state storage has a bounded range
    if (key.mod & SDL_KMOD_SHIFT) {
        if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
        else {
            const char *plain = "1234567890-=[];'/.,\\`";
            const char *shift = "!@#$%^&*()_+{}:\"?><|~";
            const char *found = strchr(plain, value);
            if (found) value = shift[found - plain];
        }
    }
    return value;
}

static void read_mouse(event &ev, float x, float y, SDL_MouseButtonFlags buttons)
{
    platform_mouse_position(x, y, ev.mouse_move.x, ev.mouse_move.y);
    ev.mouse_button = 0;
    if (buttons & SDL_BUTTON_LMASK) ev.mouse_button |= LEFT_BUTTON;
    if (buttons & SDL_BUTTON_RMASK) ev.mouse_button |= RIGHT_BUTTON;
    if (buttons & SDL_BUTTON_MMASK) ev.mouse_button |= LEFT_BUTTON | RIGHT_BUTTON;
}

event_handler::event_handler(image *screen, palette *pal)
{
    mouse = new JCMouse(screen, pal);
    mhere = mouse->exsist();
    last_keystat = get_key_flags();
    ewaiting = 0;
    pressed.fill(0);
    held_buttons = 0;
}
event_handler::~event_handler() { delete mouse; }
void event_handler::flush_screen() { update_dirty(screen); }
int event_handler::get_key_flags()
{
    SDL_Keymod mod = SDL_GetModState();
    return ((mod & SDL_KMOD_SHIFT) != 0) << 3 |
           ((mod & SDL_KMOD_CTRL) != 0) << 2 |
           ((mod & SDL_KMOD_ALT) != 0) << 1;
}
int event_handler::event_waiting()
{
    SDL_PumpEvents();
    return events.first() || SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}
void event_handler::add_redraw(int x1, int y1, int x2, int y2, void *start)
{
    event *ev = new event;
    ev->type = EV_REDRAW;
    ev->redraw = {x1, y1, x2, y2, start};
    events.add_end(ev);
}
void event_handler::get_event(event &ev)
{
    while (!event_waiting()) SDL_Delay(1);
    if (event *pending = static_cast<event *>(events.first())) {
        ev = *pending;
        events.unlink(pending);
        delete pending;
        return;
    }
    ev.type = EV_SPURIOUS;
    float x, y;
    SDL_GetMouseState(&x, &y);
    read_mouse(ev, x, y, held_buttons);
    SDL_Event input;
    if (!SDL_PollEvent(&input)) return;
    controller_event(input);
    if (input.type == SDL_EVENT_GAMEPAD_ADDED && the_game && the_game->state == RUN_STATE && controller_take_warning())
        the_game->show_help("Gamepad needs two sticks. Use keyboard/mouse. Controller gameplay disabled.");
    if (int key = controller_key(input, the_game && the_game->state == RUN_STATE)) {
        ev.type = EV_KEY; ev.key = key;
        event *release = new event;
        release->type = EV_KEYRELEASE; release->key = key;
        release->mouse_move = ev.mouse_move; release->mouse_button = ev.mouse_button;
        events.add_end(release);
        return;
    }
    switch (input.type) {
    case SDL_EVENT_QUIT: exit(0);
    case SDL_EVENT_MOUSE_MOTION:
        controller_mouse_used();
        held_buttons = input.motion.state;
        read_mouse(ev, input.motion.x, input.motion.y, held_buttons);
        ev.type = EV_MOUSE_MOVE;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        // Reconstruct transitions from event order; polling state may already include later events.
        if (input.type == SDL_EVENT_MOUSE_BUTTON_DOWN) held_buttons |= SDL_BUTTON_MASK(input.button.button);
        else held_buttons &= ~SDL_BUTTON_MASK(input.button.button);
        read_mouse(ev, input.button.x, input.button.y, held_buttons);
        ev.type = EV_MOUSE_BUTTON;
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float delta = input.wheel.y;
        if (input.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
        if (delta != 0) {
            const bool gameplay = the_game && the_game->state == RUN_STATE && !controller_in_menu();
            if (gameplay && !game_settings.wheel_weapons) break;
            ev.key = gameplay ?
                get_key_binding(delta > 0 ? "b4" : "b3", 0) : delta > 0 ? JK_UP : JK_DOWN;
            ev.type = EV_KEY;
            event *release = new event;
            release->type = EV_KEYRELEASE;
            release->key = ev.key;
            release->mouse_move = ev.mouse_move;
            release->mouse_button = ev.mouse_button;
            events.add_end(release);
        }
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = input.type == SDL_EVENT_KEY_DOWN;
        if (input.key.repeat) break;
        if (input.key.key == SDLK_F11) {
            if (down && !platform_set_fullscreen(
                    !(SDL_GetWindowFlags(platform_window()) & SDL_WINDOW_FULLSCREEN)) && the_game)
                the_game->show_help("Could not save fullscreen preference.");
            break;
        }
        if (input.key.key == SDLK_F12) {
            if (down) SDL_SetWindowMouseGrab(platform_window(), !SDL_GetWindowMouseGrab(platform_window()));
            break;
        }
        if (input.key.key == SDLK_PRINTSCREEN) {
            if (down && platform_screenshot("screenshot.bmp") && the_game)
                the_game->show_help("Screenshot saved to: screenshot.bmp.\n");
            break;
        }
        const auto scancode = size_t(input.key.scancode);
        if (scancode >= pressed.size()) break;
        ev.key = down ? game_key(input.key) : pressed[scancode];
        pressed[scancode] = down ? ev.key : 0;
        if (ev.key) ev.type = down ? EV_KEY : EV_KEYRELEASE;
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED: controller_focus(true); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        controller_focus(false);
        // Release held actions so alt-tab cannot leave movement or firing stuck.
        for (int &key : pressed) if (key) {
            event *release = new event;
            release->type = EV_KEYRELEASE;
            release->key = key;
            release->mouse_move = ev.mouse_move;
            release->mouse_button = 0;
            events.add_end(release);
            key = 0;
        }
        held_buttons = 0;
        ev.mouse_button = 0;
        ev.type = EV_MOUSE_BUTTON;
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        update_window_done();
        break;
    default: break;
    }
    mouse->update(ev.mouse_move.x, ev.mouse_move.y, ev.mouse_button);
}
