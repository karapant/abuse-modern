// SPDX-License-Identifier: GPL-2.0-or-later
#include "controller.h"
#include "controls.h"
#include "keys.h"
#include <algorithm>
#include <cmath>

static SDL_Gamepad *pad = nullptr;
static bool focused = true;
static bool aiming = false;
static bool warning_pending = false;
static bool suppress_buttons = false;
static bool mouse_priority = false;
static bool last_input_was_gamepad = false;
static int menu_depth = 0;
ControllerMenuScope::ControllerMenuScope() { ++menu_depth; }
ControllerMenuScope::~ControllerMenuScope() { if (--menu_depth == 0) suppress_buttons = true; }
bool controller_in_menu() { return menu_depth > 0; }
static float aim_x = 1, aim_y = 0;
static void open_first()
{
    if (pad) return;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && !pad; ++i) pad = SDL_OpenGamepad(ids[i]);
    SDL_free(ids);
    if (pad) warning_pending = controller_requires_two_sticks();
}
bool controller_init()
{
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) return false;
    open_first();
    return true;
}
bool controller_requires_two_sticks()
{
    return pad && (!SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) ||
                   !SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) ||
                   !SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) ||
                   !SDL_GamepadHasAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
}
bool controller_take_warning() { const bool pending = warning_pending; warning_pending = false; return pending; }
void controller_suppress_until_neutral() { suppress_buttons = true; }
void controller_shutdown()
{
    SDL_CloseGamepad(pad); pad = nullptr; aiming = false; warning_pending = false;
    last_input_was_gamepad = false;
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}
void controller_focus(bool active) { focused = active; if (!active) aiming = false; }
void controller_mouse_used() { aiming = false; mouse_priority = true; last_input_was_gamepad = false; }
void controller_keyboard_used() { last_input_was_gamepad = false; }
void controller_event(const SDL_Event &input)
{
    if (pad && controller_options.enabled && !controller_in_menu() && !controller_requires_two_sticks()) {
        const auto id = SDL_GetGamepadID(pad);
        if ((input.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && input.gbutton.which == id) ||
            (input.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && input.gaxis.which == id &&
             (input.gaxis.axis <= SDL_GAMEPAD_AXIS_RIGHTY ? std::abs(int(input.gaxis.value)) : input.gaxis.value)
                 > controller_options.deadzone))
            last_input_was_gamepad = true;
    }
    if (input.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && pad && input.gaxis.which == SDL_GetGamepadID(pad) &&
        (input.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTX || input.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY)) mouse_priority = false;
    if (input.type == SDL_EVENT_GAMEPAD_ADDED) open_first();
    else if (input.type == SDL_EVENT_GAMEPAD_REMOVED && pad && input.gdevice.which == SDL_GetGamepadID(pad)) {
        SDL_CloseGamepad(pad); pad = nullptr; aiming = false; last_input_was_gamepad = false; open_first();
    }
}
const char *controller_use_hint()
{
    if (!last_input_was_gamepad || !pad || !controller_options.enabled ||
        !SDL_GamepadHasButton(pad, SDL_GAMEPAD_BUTTON_EAST)) return nullptr;
    switch (SDL_GetGamepadButtonLabel(pad, SDL_GAMEPAD_BUTTON_EAST)) {
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
        default: return "East button";
    }
}
const char *controller_name() { return pad ? SDL_GetGamepadName(pad) : "Not connected"; }
ControllerState controller_state()
{
    ControllerState state;
    if (controller_in_menu() || !focused || !controller_options.enabled || !pad || !SDL_GamepadConnected(pad) || controller_requires_two_sticks()) {
        aiming = false; return state;
    }
    const auto button = [](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    const int dead = controller_options.deadzone;
    const int x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    const int y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    state.x = (x > dead || button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) - (x < -dead || button(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
    state.y = (y > dead || button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) - (y < -dead || button(SDL_GAMEPAD_BUTTON_DPAD_UP));
    state.jump = button(SDL_GAMEPAD_BUTTON_SOUTH);
    state.use = button(SDL_GAMEPAD_BUTTON_EAST);
    state.fire = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > dead;
    state.special = button(SDL_GAMEPAD_BUTTON_WEST) || SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > dead;
    if (suppress_buttons) {
        if (state.jump || state.use || state.fire || state.special) return {};
        suppress_buttons = false;
    }
    float rx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    float ry = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    const float magnitude = std::hypot(rx, ry);
    if (magnitude > dead && !mouse_priority) { aiming = true; aim_x = rx / magnitude; aim_y = ry / magnitude; }
    state.aim = aiming; state.aim_x = aim_x; state.aim_y = aim_y;
    return state;
}
int controller_key(const SDL_Event &input, bool gameplay)
{
    gameplay = gameplay && !controller_in_menu();
    if ((!controller_options.enabled && gameplay) || !focused || !pad || input.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        input.gbutton.which != SDL_GetGamepadID(pad)) return 0;
    if (input.gbutton.button == SDL_GAMEPAD_BUTTON_START) return JK_ESC;
    if (gameplay && controller_requires_two_sticks()) return 0;
    switch (input.gbutton.button) {
    case SDL_GAMEPAD_BUTTON_START: return JK_ESC;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return gameplay ? control_binding(ControlAction::Previous) : JK_PAGEUP;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return gameplay ? control_binding(ControlAction::Next) : JK_PAGEDOWN;
    default: break;
    }
    if (gameplay) return 0;
    switch (input.gbutton.button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return JK_ENTER;
    case SDL_GAMEPAD_BUTTON_EAST: return JK_ESC;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return JK_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return JK_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return JK_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return JK_RIGHT;
    default: return 0;
    }
}
