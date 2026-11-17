// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the real engine and dialogs with SDL events; original assets are opt-in.
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include "common.h"
#include "game.h"
#include "view.h"
#include "objects.h"
#include "level.h"
#include "ui/textmenu.h"
#include "ui/save_slots.h"
#include "sdlport/controls.h"
#include "sdlport/preferences.h"
#include "clisp.h"
#include "lisp.h"
#include "configuration.h"
#include "loadgame.h"
#include "sdlport/controller.h"
#include "sdlport/platform.h"
#include "video.h"

int abuse_main(int, char **);
static int phase = 0;
static bool one_stick = false, warned = false;
static SDL_Joystick *virtual_pad = nullptr;
static SDL_JoystickID virtual_id = 0;
static std::atomic<bool> reached{false}, finished{false};
static std::filesystem::path root;
static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "ENGINE FAIL (phase %d): %s\n", phase, message); std::exit(1); }
}
static void key(SDL_Keycode key, SDL_Scancode code)
{
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN; event.key.key = key; event.key.scancode = code;
    require(SDL_PushEvent(&event), "inject key press");
    event.type = SDL_EVENT_KEY_UP; require(SDL_PushEvent(&event), "inject key release");
}
static void enter() { key(SDLK_RETURN, SDL_SCANCODE_RETURN); }
static void escape() { key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE); }
static void choose(TextMenu *menu, const std::string &label)
{
    int index = -1;
    for (size_t i = 0; i < menu->items.size(); ++i) if (menu->items[i].label == label) index = i;
    require(index >= 0 && menu->items[index].enabled, ("find enabled option " + label).c_str());
    // Drive one navigation step per draw, without building up queued key releases.
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
    if (menu->selected != index) {
        key(SDLK_DOWN, SDL_SCANCODE_DOWN);
    } else enter();
}
static bool selected(TextMenu *menu, const std::string &label) { return menu->items.at(menu->selected).label == label; }
static void snapshot(const char *name)
{
    put_image(screen, 0, 0); update_window_done();
    require(platform_screenshot((root / name).c_str()), "capture actual engine menu");
}
extern "C" void __real__ZN8TextMenu4drawER5imageR6JCFontR7palettePKcS7_S7_(TextMenu *, image &, JCFont &, palette &, const char *, const char *, const char *);
extern "C" void __wrap__ZN8TextMenu4drawER5imageR6JCFontR7palettePKcS7_S7_(TextMenu *menu, image &target, JCFont &font, palette &palette, const char *title, const char *section, const char *hint)
{
    __real__ZN8TextMenu4drawER5imageR6JCFontR7palettePKcS7_S7_(menu, target, font, palette, title, section, hint);
    reached = true;
    static std::string last_trace;
    const auto trace = std::to_string(phase) + " " + section + " " + std::to_string(menu->selected);
    if (last_trace != trace) { fprintf(stderr, "Engine integration: %s\n", trace.c_str()); last_trace = trace; }
    const std::string page(section);
    if (one_stick) {
        if (page == "GAMEPAD WARNING") {
            require(controller_requires_two_sticks(), "unsupported controller produces a visible warning");
            warned = true; snapshot("one-stick-warning.bmp");
            SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP); enter();
        } else if (page == "MAIN MENU") {
            require(warned, "warning appears before first main menu");
            require(controller_state().x == 0 && !controller_state().fire, "unsupported controller gameplay stays disabled");
            if (selected(menu, "Quit game")) { enter(); phase = 101; } else choose(menu, "Quit game");
        } else if (page == "QUIT GAME?") {
            if (selected(menu, "Quit to desktop")) { enter(); phase = 102; } else choose(menu, "Quit to desktop");
        }
        return;
    }
    if (phase == 0 && page == "MAIN MENU") {
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        require(l_difficulty->GetValue() == l_hard, "saved difficulty overrides game-data defaults after script load");
        snapshot("main.bmp");
        if (selected(menu, "Settings")) { enter(); phase = 50; } else choose(menu, "Settings");
    } else if (phase == 50 && page == "SETTINGS") {
        if (selected(menu, "Difficulty")) { enter(); phase = 51; } else choose(menu, "Difficulty");
    } else if (phase == 51 && page == "SETTINGS") {
        GameSettings saved;
        if (l_difficulty->GetValue() != l_extreme) return; // queued key release can redraw before the injected press
        require(settings_load((root / "settings.cfg").string(), saved) && saved.difficulty == 3 && l_difficulty->GetValue() == l_extreme,
                "difficulty menu updates live game and saved preference");
        if (selected(menu, "Window scale")) { enter(); phase = 52; } else choose(menu, "Window scale");
    } else if (phase == 52 && page == "SETTINGS") {
        GameSettings saved;
        if (game_settings.scale != 4) return;
        int width, height; SDL_GetWindowSize(platform_window(), &width, &height);
        require(settings_load((root / "settings.cfg").string(), saved) && saved.scale == 4 && width == 1280 && height == 800,
                "window scale menu resizes live window and saves preference");
        phase = 1;
        choose(menu, "Key bindings");
    } else if (phase == 1 && page == "SETTINGS") {
        if (selected(menu, "Key bindings")) { enter(); phase = 2; } else choose(menu, "Key bindings");
    } else if (phase == 2 && page == "KEY BINDINGS") {
        snapshot("bindings.bmp"); choose(menu, "Move left"); phase = 3;
    } else if (phase == 3 && page == "REBIND KEY") {
        key(SDLK_J, SDL_SCANCODE_J); phase = 4;
    } else if (phase == 4 && page == "KEY BINDINGS") {
        require(keys.left == 'j' && std::filesystem::exists(root / "controls.cfg"), "UI rebind is applied and persisted");
        escape(); phase = 5;
    } else if (phase == 5 && page == "SETTINGS") { escape(); phase = 6;
    } else if (phase == 6 && page == "MAIN MENU") {
        if (selected(menu, "Multiplayer")) { enter(); phase = 7; } else choose(menu, "Multiplayer");
    } else if (phase == 7 && page == "MULTIPLAYER") {
        if (selected(menu, "Join address")) { enter(); phase = 8; } else choose(menu, "Join address");
    } else if (phase == 8 && page == "JOIN ADDRESS") {
        for (int i = 0; i < 64; ++i) key(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE);
        key(SDLK_H, SDL_SCANCODE_H); key(SDLK_COLON, SDL_SCANCODE_SEMICOLON); key(SDLK_0, SDL_SCANCODE_0);
        enter(); phase = 9;
    } else if (phase == 9 && page == "INPUT ERROR") { snapshot("network-error.bmp"); enter(); phase = 10;
    } else if (phase == 10 && page == "MULTIPLAYER") {
        if (selected(menu, "Host game")) { enter(); phase = 11; } else choose(menu, "Host game");
    } else if (phase == 11 && page == "HOST GAME") {
        snapshot("host.bmp"); escape(); phase = 12;
    } else if (phase == 12 && page == "MULTIPLAYER") {
        require(!std::filesystem::exists(root / "config"), "cancelled host setup writes nothing");
        escape(); phase = 13;
    } else if (phase == 13 && page == "MAIN MENU") {
        if (selected(menu, "New game")) { enter(); phase = 14; } else choose(menu, "New game");
    } else if (phase == 15 && page == "PAUSED") {
        if (selected(menu, "Load game")) { enter(); phase = 16; } else choose(menu, "Load game");
    } else if (phase == 16 && page == "LOAD GAME") {
        snapshot("load.bmp");
        require(menu->items.size() == 16, "load dialog includes all fifteen slots and Back");
        if (selected(menu, "Slot 15")) { enter(); phase = 17; } else choose(menu, "Slot 15");
    } else if ((phase == 21 || phase == 23) && page == "SAVE GAME") {
        if (selected(menu, "Slot 15")) { enter(); phase = phase == 21 ? 22 : 24; }
        else choose(menu, "Slot 15");
    } else if (phase == 22 && page == "OVERWRITE?") {
        require(selected(menu, "Cancel"), "overwrite confirmation defaults to cancel");
        snapshot("overwrite.bmp"); enter(); phase = 23;
    } else if (phase == 24 && page == "OVERWRITE?") {
        if (selected(menu, "Overwrite slot 15")) { enter(); phase = 27; }
        else choose(menu, "Overwrite slot 15");
    } else if (phase == 25 && page == "SAVE GAME") { escape(); phase = 26;
    } else if (phase == 18 && page == "PAUSED") {
        if (selected(menu, "Quit game")) { enter(); phase = 19; } else choose(menu, "Quit game");
    } else if (phase == 19 && page == "QUIT GAME?") {
        if (selected(menu, "Quit to desktop")) { enter(); phase = 20; } else choose(menu, "Quit to desktop");
    }
}
extern "C" void __real__ZN4view9get_inputEv(view *);
extern "C" void __wrap__ZN4view9get_inputEv(view *v)
{
    __real__ZN4view9get_inputEv(v);
    if (!the_game || the_game->state != RUN_STATE || !v->focus) return;
    if (phase == 14) {
        int x, y, b1, b2, b3, b4;
        the_game->set_key_down('j', 1);
        get_movement(0, x, y, b1, b2, b3, b4);
        require(x == -1, "rebound key drives actual gameplay movement");
        the_game->set_key_down('j', 0);
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_LEFTX, 24000);
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        SDL_UpdateJoysticks();
        get_movement(0, x, y, b1, b2, b3, b4);
        require(x == 1 && b2 == 1, "virtual controller drives movement and firing in the actual engine");
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_LEFTX, 0);
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        SDL_UpdateJoysticks();
        v->focus->set_hp(73);
        require(current_level->save("save0015.spe", 1) == 1, "serialize live game state");
        require(scan_save_slots(root.string()).at(14).state == SaveSlotState::Saved, "real save recognized by text slot picker");
        phase = 21;
        require(load_game(1, "SAVE") == 15 && phase == 27, "checkpoint slot picker cancels then confirms overwrite of slot 15");
        phase = 25;
        require(load_game(1, "SAVE") == 0 && phase == 26, "escape cancels checkpoint slot picker");
        v->focus->set_hp(17);
        phase = 15;
        SDL_Event start = {}; start.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        start.gbutton.which = virtual_id; start.gbutton.button = SDL_GAMEPAD_BUTTON_START;
        require(SDL_PushEvent(&start), "inject short controller pause press");
        start.type = SDL_EVENT_GAMEPAD_BUTTON_UP; require(SDL_PushEvent(&start), "inject short controller pause release");
    } else if (phase == 17) {
        require(v->focus->hp() == 73, "load dialog restores saved player health");
        phase = 18; escape();
    }
}
int main(int argc, char **argv)
{
    require(argc >= 2, "supply original game data path");
    one_stick = argc > 2 && std::string(argv[2]) == "--one-stick";
    root = std::filesystem::current_path() / (one_stick ? "engine-integration-one-stick" : "engine-integration-fixture");
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);
    GameSettings initial;
    initial.difficulty = 2;
    require(settings_write((root / "settings.cfg").string(), initial), "prepare persistent engine difficulty");
    std::ofstream(root / "gamma.lsp") << "(setq darkest_gray 16)\n";
    const auto saves = root.string() + "/";
    SDL_setenv_unsafe("ABUSE_SAVE_PATH", saves.c_str(), 1);
    SDL_unsetenv_unsafe("ABUSE_PATH");
    require(SDL_Init(SDL_INIT_GAMEPAD), "initialize virtual gamepad fixture");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_VirtualJoystickDesc device;
    SDL_INIT_INTERFACE(&device);
    device.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    device.naxes = one_stick ? 2 : SDL_GAMEPAD_AXIS_COUNT;
    device.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    device.axis_mask = (1u << device.naxes) - 1;
    device.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    device.name = one_stick ? "One-stick integration device" : "Dual-stick integration device";
    virtual_id = SDL_AttachVirtualJoystick(&device);
    require(virtual_id != 0, "attach integration gamepad");
    virtual_pad = SDL_OpenJoystick(virtual_id);
    require(virtual_pad != nullptr, "open integration gamepad");
    if (!one_stick) {
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
        SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    }
    SDL_UpdateJoysticks();
    std::thread intro([] {
        while (!finished && !reached) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!reached && SDL_WasInit(SDL_INIT_VIDEO)) enter();
        }
    });
    char datadir[] = "-datadir", nosound[] = "-nosound";
    char *arguments[] = {argv[0], datadir, argv[1], nosound, nullptr};
    const int result = abuse_main(4, arguments);
    finished = true; intro.join();
    require(result == 0 && phase == (one_stick ? 102 : 20), "complete actual-engine UI flow");
    SDL_CloseJoystick(virtual_pad); SDL_DetachVirtualJoystick(virtual_id);
    if (one_stick) { puts("Actual-engine one-stick warning and disabled gameplay checks passed."); return 0; }
    control_reset(); require(controls_load((root / "controls.cfg").string()) && keys.left == 'j', "persisted UI binding reloads");
    puts("Actual-engine integration passed: key rebind, network error/cancel, slot 15 save/load, short pause tap and quit.");
}
