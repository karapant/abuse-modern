// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the real engine and dialogs with SDL events; original assets are opt-in.
#include <SDL3/SDL.h>
#include <array>
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
#include "ui/checkpoint_marker.h"
#include "sdlport/controls.h"
#include "sdlport/preferences.h"
#include "clisp.h"
#include "lisp.h"
#include "configuration.h"
#include "loadgame.h"
#include "sdlport/controller.h"
#include "sdlport/platform.h"
#include "video.h"
#include "sbar.h"
#include "jwindow.h"
#include "light.h"
#include "chars.h"
#include "transimage.h"
#include "loader2.h"
#include "jrand.h"
#include "nfserver.h"
#include <cstring>

int abuse_main(int, char **);
static int phase = 0;
static bool one_stick = false, warned = false, wide_view = false, opening_levels = false;
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
static void check_hud()
{
    int left, top, right, bottom;
    sbar.bounds(left, top, right, bottom);
    require(right > left && top >= 0 && bottom == yres && abs(left + right - xres) <= 1,
            "weapon bar, health and ammo share bottom-center screen anchor");
}
static void check_explosion_effect()
{
    int type = 0;
    while (type < total_objects && strcmp(object_names[type], "EXP_LIGHT")) ++type;
    require(type < total_objects, "stock explosion flash type exists");
    auto *previous = current_object;
    auto *flash = create(type, 80, 64, 0, 90);
    require(flash->decide(), "stock explosion flash runs its script");
    auto *light = flash->get_light(0);
    require(light != nullptr, "stock flash creates a light");
    flash->remove_light(light);
    delete_light(light);
    delete flash;
    current_object = previous;

    image test(vec2i(160, 128), nullptr, 2);
    const int gray = pal->find_closest(96, 96, 96);
    const auto tick = current_level->tick_counter();
    const auto sample = [&](unsigned age, int x, int y) {
        test.clear(gray);
        enhanced_light_screen(&test, 0, 0, white_light, 63, *pal, tick + age);
        return test.Pixel(vec2i(x, y));
    };
    require(sample(0, 80, 64) != gray, "scripted blast casts a colored flash");
    require(sample(0, 76, 62) == pal->find_closest(255, 222, 128), "blast draws bright sparks");
    require(sample(12, 80, 52) != gray, "smoke remains after the flash");
    require(sample(21, 80, 52) == gray, "blast overlay expires");

    // Expose the light levels directly to verify pixel falloff and the viewport clip.
    std::array<uint8_t, 64 * 256> levels{};
    for (size_t i = 0; i < levels.size(); ++i) levels[i] = i / 256;
    light = add_light_source(0, 20032, 20032, 0, 32, 0, 0);
    light->environment_tint = false;
    test.clear(80);
    test.SetClip(3, 4, 70, 71);
    enhanced_light_screen(&test, 20000, 20000, levels.data(), 0, *pal, tick);
    require(test.Pixel(vec2i(35, 36)) == 63 && test.Pixel(vec2i(3, 4)) == 0 &&
            test.Pixel(vec2i(55, 36)) > test.Pixel(vec2i(56, 36)) &&
            test.Pixel(vec2i(2, 36)) == 80, "lighting has per-pixel falloff and respects the viewport");
    delete_light(light);
}
static void check_environment_lights()
{
    image test(vec2i(160, 128), nullptr, 2);
    const int gray = pal->find_closest(108, 108, 108);
    // The identity lookup separates fixture tint from ambient shading in these assertions.
    std::array<uint8_t, 64 * 256> unchanged{};
    for (size_t i = 0; i < unchanged.size(); ++i) unchanged[i] = i % 256;

    auto *round = add_light_source(0, 20035, 20050, 6, 30, 0, 0);
    auto *directional = add_light_source(2, 20105, 20050, 6, 30, 0, 0);
    auto *brightness = add_light_source(9, 20036, 20051, 32, 33, 8, 8);
    test.clear(gray);
    enhanced_light_screen(&test, 20000, 20000, unchanged.data(), 10, *pal, current_level->tick_counter());
    const int amber = test.Pixel(vec2i(35, 50));
    const int blue = test.Pixel(vec2i(105, 55));
    require(amber != gray && pal->red(amber) > pal->blue(amber), "round lights cast a warm tint");
    require(blue != gray && pal->blue(blue) > pal->red(blue), "directional fixtures cast a cool tint");
    require(test.Pixel(vec2i(105, 45)) == gray && test.Pixel(vec2i(35, 83)) == gray,
            "fixture color respects direction and radius");
    require(test.Pixel(vec2i(40, 55)) == gray, "solid room lighting preserves color");
    delete_light(brightness);
    delete_light(directional);
    delete_light(round);
}
static void check_projectile_lights()
{
    const auto create_projectile = [](const char *name) {
        int type = 0;
        while (type < total_objects && strcmp(object_names[type], name)) ++type;
        require(type < total_objects, "stock projectile type exists");
        return create(type, 20090, 20064, 0);
    };
    image test(vec2i(160, 128), nullptr, 2);
    image surfaces(test.Size(), nullptr, 2);
    const int gray = pal->find_closest(124, 124, 124);
    const int black = pal->find_closest(0, 0, 0);
    const auto render = [&] {
        test.clear(gray);
        test.scan_line(70)[62] = black;
        reflect_projectile_lights(test, surfaces, 20000, 20000, *pal);
    };
    auto *laser = create_projectile("SHOTGUN_BULLET");
    const auto *type = figures[laser->otype];
    for (int i = 0; i < type->tiv; ++i) {
        if (!type->vars[i]) continue;
        const char *name = lstring_value(static_cast<LSymbol *>(type->vars[i])->GetName());
        if (!strcmp(name, "sgb_lastx")) laser->lvars[type->var_index[i]] = 20030;
        if (!strcmp(name, "sgb_lasty")) laser->lvars[type->var_index[i]] = 20064;
    }
    surfaces.clear(NoReflection);
    surfaces.bar(55, 70, 64, 96, WallReflection);
    surfaces.bar(65, 70, 75, 96, BackgroundReflection);
    clear_projectile_lights();
    laser->collect_projectile_light(laser->x, laser->y);
    render();
    const int red = test.Pixel(vec2i(60, 70));
    const int background = test.Pixel(vec2i(70, 70));
    require(pal->red(red) > pal->green(red) && pal->red(red) > pal->red(gray),
            "starter laser casts red light onto a nearby wall");
    require(pal->red(background) < pal->red(red), "background reflection is weaker than wall reflection");
    require(test.Pixel(vec2i(60, 64)) == gray && test.Pixel(vec2i(62, 70)) == black &&
            test.Pixel(vec2i(60, 96)) == gray,
            "open space and black pixels stay dark and the light fades away from the beam");
    clear_projectile_lights();
    delete laser;

    auto *rocket = create_projectile("ROCKET");
    rocket->set_xvel(12);
    surfaces.clear(WallReflection);
    rocket->collect_projectile_light(rocket->x, rocket->y);
    render();
    const int rocket_y = 64 - rocket->picture()->Size().y / 2;
    const int amber = test.Pixel(vec2i(78, rocket_y));
    require(pal->red(amber) > pal->red(gray) && pal->green(amber) > pal->blue(amber),
            "rocket exhaust casts amber light on walls");
    clear_projectile_lights();
    rocket->collect_projectile_light(rocket->x + 65, rocket->y);
    render();
    require(test.Pixel(vec2i(78, rocket_y)) == gray && test.Pixel(vec2i(145, rocket_y)) != gray,
            "rocket reflection follows the rendered position without a stale glow");
    delete rocket;
    clear_projectile_lights();
}
static void check_wheel(view *v)
{
    require(total_weapons >= 2, "weapon fixture has at least two weapons");
    require(control_rebind(ControlAction::Next, 'n') && control_rebind(ControlAction::Previous, 'p') &&
            controls_save((root / "controls.cfg").string()), "persist secondary weapon controls");
    control_reset();
    require(controls_load((root / "controls.cfg").string()) && keys.b4 == 'n' && keys.b3 == 'p', "weapon bindings survive reload");
    const int old_weapon = v->current_weapon, old_ammo = v->weapons[1];
    v->weapons[1] = 10;
    const auto scroll = [](float delta) {
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        SDL_Event wheel = {}; wheel.type = SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y = delta;
        require(SDL_PushEvent(&wheel), "inject physical wheel event");
        event result; wm->eh->get_event(result); return result;
    };
    v->current_weapon = 0;
    event ev = scroll(1);
    require(ev.type == EV_KEY && ev.key == 'n' && v->handle_event(ev) && v->suggest.new_weapon == 1,
            "wheel up follows saved Next binding and selects an available weapon");
    wm->eh->get_event(ev);
    require(ev.type == EV_KEYRELEASE && ev.key == 'n', "wheel pulse releases the weapon key");
    v->current_weapon = 1;
    ev = scroll(-1);
    require(ev.type == EV_KEY && ev.key == 'p' && v->handle_event(ev) && v->suggest.new_weapon == 0,
            "wheel down follows saved Previous binding and selects the preceding weapon");
    wm->eh->get_event(ev);
    require(save_setting(Setting::WheelWeapons, 0), "persist disabled weapon wheel");
    GameSettings loaded;
    require(settings_load((root / "settings.cfg").string(), loaded) && loaded.wheel_weapons == 0,
            "weapon wheel preference reloads");
    ev = scroll(1);
    require(ev.type == EV_SPURIOUS, "disabled gameplay wheel emits no weapon action");
    {
        ControllerMenuScope menu;
        ev = scroll(1);
        require(ev.type == EV_KEY && ev.key == JK_UP, "menu wheel scrolling remains available when weapon wheel is disabled");
        wm->eh->get_event(ev);
    }
    require(save_setting(Setting::WheelWeapons, 1), "restore weapon wheel");
    v->current_weapon = old_weapon; v->weapons[1] = old_ammo; v->suggest.send_weapon_change = 0;
}
static void snapshot(const char *name)
{
    put_image(screen, 0, 0); update_window_done();
    require(platform_screenshot((root / name).c_str()), "capture actual engine menu");
}
static int save_notices = 0;
extern "C" void __real__ZN4Game9show_helpEPKc(Game *, const char *);
extern "C" void __wrap__ZN4Game9show_helpEPKc(Game *game, const char *text)
{
    if (!strcmp(text, "Game saved")) ++save_notices;
    __real__ZN4Game9show_helpEPKc(game, text);
}

static void invoke_save(const char *expression)
{
    LObject::Compile(expression)->Eval();
}

static void check_checkpoint_marker(view *v)
{
    int type = 0;
    while (type < total_objects && strcmp(object_names[type], "RESTART_POSITION")) ++type;
    require(type < total_objects, "stock save console type exists");
    auto *console = create(type, v->focus->x + 56, v->focus->y + 36, 0);
    current_level->add_object(console);
    current_level->unactivate_all();
    current_level->add_drawables(v->xoff(), v->yoff(), v->xoff() + xres, v->yoff() + yres);
    const int left = v->cx1 + console->x - v->xoff() - checkpoint_marker_size / 2;
    const int top = std::max(v->cy1, v->cy1 + console->y - v->yoff() -
                                   console->picture()->Size().y - checkpoint_marker_size - 2);
    require(left >= v->cx1 && left + checkpoint_marker_size <= v->cx2 &&
            top + checkpoint_marker_size <= v->cy2, "checkpoint fixture is inside the viewport");
    the_game->draw(0);
    const int amber = pal->find_closest(165, 108, 36);
    const auto count_amber = [&] {
        int pixels = 0;
        for (int y = top; y < top + checkpoint_marker_size; ++y)
            for (int x = left; x < left + checkpoint_marker_size; ++x)
                if (screen->Pixel(vec2i(x, y)) == amber) ++pixels;
        return pixels;
    };
    const int marked = count_amber();
    snapshot("checkpoint-marker.bmp");
    game_settings.checkpoint_feedback = 0;
    the_game->draw(0);
    require(marked > 15 && count_amber() < marked, "disabling checkpoint feedback hides the in-world badge");
    game_settings.checkpoint_feedback = 1;
    current_level->delete_object(console);
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
    if (opening_levels && phase == 0 && page == "MAIN MENU") {
        if (selected(menu, "New game")) { enter(); phase = 200; }
        else choose(menu, "New game");
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
        require(settings_load((root / "settings.cfg").string(), saved) && saved.scale == 4 && width == xres * 4 && height == yres * 4,
                "window scale menu resizes live window and saves preference");
        if (selected(menu, "View resolution")) { enter(); phase = 53; } else choose(menu, "View resolution");
    } else if (phase == 53 && page == "SETTINGS") {
        const int expected = wide_view ? 0 : 1;
        if (game_settings.view_resolution != expected) return;
        GameSettings saved;
        require(settings_load((root / "settings.cfg").string(), saved) && saved.view_resolution == expected &&
                screen->Size().x == view_width(expected) && xres == view_width(expected) && yres == view_height(expected),
                "resolution menu immediately resizes framebuffer and persists selection");
        // Resizing warps the cursor; discard that synthetic motion before keyboard adjustment.
        SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
        if (!selected(menu, "View resolution")) { choose(menu, "View resolution"); return; }
        key(SDLK_LEFT, SDL_SCANCODE_LEFT); phase = 54;
    } else if (phase == 54 && page == "SETTINGS") {
        if (game_settings.view_resolution != (wide_view ? 3 : 0)) return;
        require(xres == (wide_view ? 640 : 320) && yres == (wide_view ? 360 : 200), "live resolution can return to previous view");
        phase = 1; choose(menu, "Key bindings");
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
    } else if (phase == 60 && page == "PAUSED") {
        if (selected(menu, "Settings")) { enter(); phase = 61; } else choose(menu, "Settings");
    } else if (phase == 61 && page == "SETTINGS") {
        if (selected(menu, "View resolution")) { enter(); phase = 62; } else choose(menu, "View resolution");
    } else if (phase == 62 && page == "SETTINGS") {
        if (game_settings.view_resolution != (wide_view ? 0 : 1)) return;
        check_hud();
        require(player_list->focus->hp() == 17 && player_list->cx2 < xres && player_list->cy2 < yres,
                "paused resolution change preserves player state and keeps camera in bounds");
        if (selected(menu, "Enhancements")) { enter(); phase = 64; }
        else choose(menu, "Enhancements");
    } else if (phase == 64 && page == "ENHANCEMENTS") {
        require(menu->items.at(0).value == "[x]" && menu->items.at(1).value == "[x]" &&
                menu->items.at(2).value == "[x]" && menu->items.at(3).value == "[ ]" && menu->items.at(4).value == "[x]",
                "original enhancements are enabled and smoothing is off by default");
        if (selected(menu, "Modern HUD")) { enter(); phase = 65; }
        else choose(menu, "Modern HUD");
    } else if (phase == 65 && page == "ENHANCEMENTS") {
        if (game_settings.modern_hud) return;
        require(menu->items.at(0).value == "[ ]", "HUD checkbox reflects toggle");
        check_hud();
        if (selected(menu, "Enhanced Lighting")) { enter(); phase = 66; }
        else choose(menu, "Enhanced Lighting");
    } else if (phase == 66 && page == "ENHANCEMENTS") {
        if (game_settings.enhanced_lighting) return;
        if (selected(menu, "Checkpoint Feedback")) { enter(); phase = 68; }
        else choose(menu, "Checkpoint Feedback");
    } else if (phase == 68 && page == "ENHANCEMENTS") {
        if (game_settings.checkpoint_feedback) return;
        if (selected(menu, "Smooth pixels")) { enter(); phase = 69; }
        else choose(menu, "Smooth pixels");
    } else if (phase == 69 && page == "ENHANCEMENTS") {
        if (!game_settings.smooth_pixels) return;
        if (selected(menu, "Enhanced projectiles")) { enter(); phase = 70; }
        else choose(menu, "Enhanced projectiles");
    } else if (phase == 70 && page == "ENHANCEMENTS") {
        if (game_settings.enhanced_projectiles) return;
        GameSettings reloaded;
        require(menu->items.at(1).value == "[ ]" && menu->items.at(2).value == "[ ]" && menu->items.at(3).value == "[x]" &&
                menu->items.at(4).value == "[ ]" &&
                settings_load((root / "settings.cfg").string(), reloaded) &&
                !reloaded.modern_hud && !reloaded.enhanced_lighting && !reloaded.checkpoint_feedback &&
                reloaded.smooth_pixels && !reloaded.enhanced_projectiles,
                "changed enhancements are saved from the pause menu");
        escape(); phase = 67;
    } else if (phase == 67 && page == "SETTINGS") {
        escape(); phase = 63;
    } else if (phase == 63 && page == "PAUSED") { phase = 15; choose(menu, "Load game");
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

static void check_projectile_styles(view *v)
{
    image *frame = screen;
    auto *saved_view = current_view;
    auto *saved_object = current_object;
    const int32_t saved_random = rand_on;
    const int32_t saved_x = current_vxadd, saved_y = current_vyadd;
    const int enhanced = game_settings.enhanced_projectiles;
    current_view = v;
    current_vxadd = v->xoff() - v->cx1;
    current_vyadd = v->yoff() - v->cy1;
    image original(frame->Size()), modern(frame->Size());
    for (const char *name : {"SHOTGUN_BULLET", "PLASMAGUN_BULLET", "LSABER_BULLET", "ROCKET",
                             "GRENADE", "FIREBOMB", "DFRIS_BULLET"}) {
        int type = 0;
        while (type < total_objects && strcmp(object_names[type], name)) ++type;
        require(type < total_objects, name);
        auto *object = create(type, current_vxadd + 200, current_vyadd + 95);
        object->set_xvel(12);
        const bool laser = !strcmp(name, "SHOTGUN_BULLET");
        if (laser || !strcmp(name, "PLASMAGUN_BULLET") || !strcmp(name, "LSABER_BULLET")) {
            char origin_x[] = "sgb_lastx", origin_y[] = "sgb_lasty";
            require(object->set_var_by_name(origin_x, object->x - 30) &&
                    object->set_var_by_name(origin_y, object->y + 8), "initialize beam origin");
        }
        if (laser) {
            char bright[] = "sgb_bright_color", medium[] = "sgb_medium_color";
            object->set_var_by_name(bright, pal->find_closest(255, 45, 12));
            object->set_var_by_name(medium, pal->find_closest(175, 18, 6));
        }
        const auto draw = [&](image &target, bool enabled) {
            screen = &target;
            target.clear();
            rand_on = saved_random;
            game_settings.enhanced_projectiles = enabled;
            object->draw();
        };
        draw(original, false);
        draw(modern, true);
        bool different = false;
        for (int y = 70; y < 110 && !different; ++y)
            different = memcmp(original.scan_line(y) + 160, modern.scan_line(y) + 160, 60) != 0;
        require(different, name);
        draw(modern, false);
        for (int y = 70; y < 110; ++y)
            require(!memcmp(original.scan_line(y) + 160, modern.scan_line(y) + 160, 60),
                    "disabling projectile styling restores the original draw");
        delete object;
    }
    game_settings.enhanced_projectiles = enhanced;
    rand_on = saved_random;
    current_vxadd = saved_x; current_vyadd = saved_y;
    current_object = saved_object;
    current_view = saved_view;
    screen = frame;
}

static void check_motion_frames(view *camera, game_object *shot)
{
    require(interpolate_motion({10, 20}, {22, 12}, 1) == vec2i(13, 18) &&
            interpolate_motion({10, 20}, {22, 12}, 3) == vec2i(19, 14),
            "subframes space motion evenly in both axes");
    require(interpolate_motion({10, 20}, {310, 12}, 1) == vec2i(310, 12),
            "an object teleports directly to its destination");
    const auto tick = current_level->tick_counter();
    const auto sync = make_sync();
    const vec2i player_position(camera->focus->x, camera->focus->y);
    const vec2i camera_position(camera->last_x, camera->last_y);
    const vec2i shot_position(shot->x, shot->y);
    const vec2i shot_history(shot->last_x, shot->last_y);
    const auto shot_tick = shot->movement_tick;
    shot->movement_tick = tick;
    shot->last_x = shot->x - 20;
    shot->last_y = shot->y;
    for (int frame = 1; frame < frames_per_tick; ++frame) {
        require(shot->render_position(frame, tick) == shot_position - vec2i(20 - 5 * frame, 0),
                "a live projectile has distinct intermediate positions");
        the_game->update_screen(frame);
        require(vec2i(shot->x, shot->y) == shot_position && shot->last_x == shot->x - 20 &&
                shot->render_shift == vec2i(0, 0) &&
                vec2i(camera->focus->x, camera->focus->y) == player_position &&
                vec2i(camera->last_x, camera->last_y) == camera_position,
                "rendering restores object and camera positions without consuming motion history");
        require(current_level->tick_counter() == tick && make_sync() == sync,
                "rendering does not advance the simulation");
    }
    shot->movement_tick = shot_tick;
    shot->last_x = shot_history.x;
    shot->last_y = shot_history.y;
    the_game->update_screen();
}

static void check_opening_level(view *v)
{
    require(xres == 640 && yres == 360, "opening-level check runs at 640x360");
    const bool tutorial = std::string(current_level->name()) == "levels/level00.spe";
    const bool first_level = std::string(current_level->name()) == "levels/level01.spe";
    require(tutorial || first_level, "load the original first two opening maps");

    int32_t left, top, right, bottom, narrow_left, narrow_top, narrow_right, narrow_bottom;
    v->activation_area(left, top, right, bottom);
    const int saved_right = v->cx2, saved_bottom = v->cy2;
    v->cx2 = v->cx1 + 316;
    v->cy2 = v->cy1 + 162;
    v->activation_area(narrow_left, narrow_top, narrow_right, narrow_bottom);
    v->cx2 = saved_right;
    v->cy2 = saved_bottom;
    require(left == narrow_left && top == narrow_top && right == narrow_right && bottom == narrow_bottom &&
            right - left < xres, "enemy activation area stays fixed when the view zooms out");

    const auto mouse = [&](Uint32 type, int gx, int gy) {
        SDL_Event click{};
        click.type = type;
        click.button.button = SDL_BUTTON_LEFT;
        require(SDL_RenderCoordinatesToWindow(platform_renderer(), float(gx), float(gy),
                                              &click.button.x, &click.button.y), "map mouse to window");
        require(SDL_PushEvent(&click), "inject edge aim mouse click");
    };
    const auto read_input = [&] {
        the_game->get_input();
        __real__ZN4view9get_inputEv(v);
        require(base->packet.packet_size() >= 7, "game produced player input");
        const auto *data = base->packet.packet_data() + base->packet.packet_size() - 7;
        require(data[0] == SCMD_SET_INPUT, "player input ends with aim and action bits");
        return std::array<int, 3>{data[2], int16_t(data[3] | (data[4] << 8)),
                               int16_t(data[5] | (data[6] << 8))};
    };
    const auto steer = [&](int axis, int value) {
        require(SDL_SetJoystickVirtualAxis(virtual_pad, SDL_GamepadAxis(axis), value), "send virtual stick input");
        SDL_UpdateJoysticks();
    };
    static int step = 0;
    static int start_x = 0;
    static bool saw_player_shot = false;
    if (step == 0) {
        saw_player_shot = false;
        start_x = v->focus->x;
        controller_focus(true);
        SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
        controller_state(); // confirm the controls have returned to neutral after the menu
        steer(SDL_GAMEPAD_AXIS_RIGHTX, 26000);
        steer(SDL_GAMEPAD_AXIS_RIGHTY, -26000);
        steer(SDL_GAMEPAD_AXIS_LEFTX, 25000);
        steer(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        const auto input = read_input();
        require((input[0] & 33) == 33 && input[1] > v->focus->x && input[2] < v->focus->y,
                "dual sticks aim diagonally while moving and firing on the original map");
        ++step;
        return;
    }
    if (step < 9) {
        if (!saw_player_shot) {
            for (auto *object = current_level->first_object(); object; object = object->next) {
                if (strcmp(object_names[object->otype], "SHOTGUN_BULLET") ||
                    !object->total_objects() || object->get_object(0) != v->focus) continue;
                saw_player_shot = true;
                snapshot(tutorial ? "tutorial-shot.bmp" : "first-level-shot.bmp");
                if (tutorial) {
                    require(platform_set_smooth_pixels(true), "enable the filter on the tutorial firefight");
                    snapshot("tutorial-shot-smooth.bmp");
                    require(platform_set_smooth_pixels(false), "restore crisp pixels after capture");
                    check_projectile_styles(v);
                    check_motion_frames(v, object);
                }
                break;
            }
        }
        ++step;
        read_input();
        return;
    }
    const int gx = tutorial ? v->cx1 + 2 : v->cx2 - 2;
    const int gy = v->cy1 + 2;
    if (step == 9) {
        require(v->focus->x > start_x, "controller moves the player through the opening room");
        require(saw_player_shot, "firing the starter weapon spawns a projectile in the opening room");
        steer(SDL_GAMEPAD_AXIS_LEFTX, 0);
        steer(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        SDL_Event key_down{};
        key_down.type = SDL_EVENT_KEY_DOWN;
        key_down.key.key = SDLK_A;
        key_down.key.scancode = SDL_SCANCODE_A;
        require(SDL_PushEvent(&key_down), "inject keyboard movement");
        int32_t expected_x, expected_y;
        the_game->mouse_to_game(gx, gy, expected_x, expected_y, v);
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, gx, gy);
        const auto input = read_input();
        if ((input[0] & 34) != 34 || abs(input[1] - expected_x) > 1 || abs(input[2] - expected_y) > 1)
            fprintf(stderr, "Opening input flags %d aim %d,%d expected %d,%d\n",
                    input[0], input[1], input[2], expected_x, expected_y);
        require((input[0] & 34) == 34 && abs(input[1] - expected_x) <= 1 && abs(input[2] - expected_y) <= 1,
                "keyboard moves while a click takes aim back from the held stick at the viewport edge");
        start_x = v->focus->x;
        ++step;
        return;
    }
    if (step < 18) {
        ++step;
        read_input();
        return;
    }
    require(v->focus->x < start_x, "keyboard moves the player through the opening room");
    SDL_Event key_up{};
    key_up.type = SDL_EVENT_KEY_UP;
    key_up.key.key = SDLK_A;
    key_up.key.scancode = SDL_SCANCODE_A;
    require(SDL_PushEvent(&key_up), "release keyboard movement");
    mouse(SDL_EVENT_MOUSE_BUTTON_UP, gx, gy);
    steer(SDL_GAMEPAD_AXIS_RIGHTX, 0);
    steer(SDL_GAMEPAD_AXIS_RIGHTY, 0);
    read_input();
    step = 0;

    if (tutorial) {
        char next_map[] = "levels/level01.spe";
        the_game->request_level_load(next_map);
        phase = 201;
    } else {
        phase = 18;
        escape();
    }
}
extern "C" void __wrap__ZN4view9get_inputEv(view *v)
{
    if (opening_levels && (phase == 200 || phase == 201) && the_game &&
        the_game->state == RUN_STATE && v->focus) {
        check_opening_level(v);
        return;
    }
    __real__ZN4view9get_inputEv(v);
    if (!the_game || the_game->state != RUN_STATE || !v->focus) return;
    if (phase == 14) {
        require(v->cx2 - v->cx1 >= xres - 8, "camera uses full framebuffer width");
        if (wide_view) {
            require(xres == 640 && yres == 360 && v->cy2 - v->cy1 >= 300, "zoomed-out widescreen exposes a larger world area");
            float wx, wy; SDL_RenderCoordinatesToWindow(platform_renderer(), xres / 2.0f, yres / 2.0f, &wx, &wy);
            int mx, my; platform_mouse_position(wx, wy, mx, my);
            require(abs(mx - xres / 2) <= 1 && abs(my - yres / 2) <= 1, "widescreen aiming maps to view center");
        }
        check_hud();
        if (wide_view) {
            std::vector<int> ammo;
            for (int i = 0; i < total_weapons; ++i) { ammo.push_back(v->weapons[i]); v->weapons[i] = 100 + i; }
            the_game->draw(0); snapshot("gameplay-wide.bmp");
            for (int i = 0; i < total_weapons; ++i) v->weapons[i] = ammo[i];
        }
        check_explosion_effect();
        check_projectile_lights();
        check_environment_lights();
        check_wheel(v);
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
        check_checkpoint_marker(v);
        invoke_save("(save_game \"save0015.spe\")");
        require(save_notices == 1, "the checkpoint save command confirms a successful write");
        invoke_save("(save_game \"missing/checkpoint.spe\")");
        require(save_notices == 1, "a save that cannot open its destination gives no success message");
        require(scan_save_slots(root.string()).at(14).state == SaveSlotState::Saved, "real save recognized by text slot picker");
        phase = 21;
        require(load_game(1, "SAVE") == 15 && phase == 27, "checkpoint slot picker cancels then confirms overwrite of slot 15");
        phase = 25;
        require(load_game(1, "SAVE") == 0 && phase == 26, "escape cancels checkpoint slot picker");
        v->focus->set_hp(17);
        phase = 60;
        SDL_Event start = {}; start.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        start.gbutton.which = virtual_id; start.gbutton.button = SDL_GAMEPAD_BUTTON_START;
        require(SDL_PushEvent(&start), "inject short controller pause press");
        start.type = SDL_EVENT_GAMEPAD_BUTTON_UP; require(SDL_PushEvent(&start), "inject short controller pause release");
    } else if (phase == 17) {
        require(!game_settings.modern_hud && !game_settings.enhanced_lighting, "classic modes stay active after loading");
        the_game->draw(0);
        require(v->focus->hp() == 73, "load dialog restores saved player health");
        require(v->cx2 < xres && v->cy2 < yres, "save loading adapts camera to current live resolution");
        check_hud();
        invoke_save("(save_game \"silent-checkpoint.spe\")");
        require(std::filesystem::exists(root / "silent-checkpoint.spe") && save_notices == 1,
                "switching checkpoint feedback off suppresses the notice without preventing saving");
        phase = 18; escape();
    }
}
int main(int argc, char **argv)
{
    require(argc >= 2, "supply original game data path");
    one_stick = argc > 2 && std::string(argv[2]) == "--one-stick";
    wide_view = argc > 2 && std::string(argv[2]) == "--wide-view";
    opening_levels = argc > 2 && std::string(argv[2]) == "--opening-levels";
    root = std::filesystem::current_path() / (opening_levels ? "engine-opening-levels" : wide_view ? "engine-integration-wide" : one_stick ? "engine-integration-one-stick" : "engine-integration-fixture");
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);
    GameSettings initial;
    initial.fullscreen = 0;
    initial.view_resolution = (wide_view || opening_levels) ? 3 : 0;
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
    char datadir[] = "-datadir", nosound[] = "-nosound", windowed[] = "-windowed";
    char *arguments[] = {argv[0], datadir, argv[1], nosound, windowed, nullptr};
    const int result = abuse_main(5, arguments);
    finished = true; intro.join();
    require(result == 0 && phase == (one_stick ? 102 : 20), "complete actual-engine UI flow");
    SDL_CloseJoystick(virtual_pad); SDL_DetachVirtualJoystick(virtual_id);
    if (one_stick) { puts("Actual-engine one-stick warning and disabled gameplay checks passed."); return 0; }
    if (opening_levels) {
        puts("Original opening maps passed at 640x360 with mouse, keyboard and virtual controller.");
        return 0;
    }
    control_reset(); require(controls_load((root / "controls.cfg").string()) && keys.left == 'j', "persisted UI binding reloads");
    puts("Actual-engine integration passed: key rebind, network error/cancel, slot 15 save/load, short pause tap and quit.");
}
