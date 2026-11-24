// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the real engine and dialogs with SDL events; original assets are opt-in.
#include <SDL3/SDL.h>
#include <algorithm>
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
#include "automap.h"
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

static void check_title_frame()
{
    const auto size = screen->Size();
    int left, top, right, bottom;
    screen->GetClip(left, top, right, bottom);
    screen->SetClip(0, 0, size.x, size.y);
    draw_title_screen();
    std::vector<uint8_t> full_frame;
    for (int y = 0; y < size.y; ++y)
        full_frame.insert(full_frame.end(), screen->scan_line(y), screen->scan_line(y) + size.x);
    screen->clear(211);
    screen->SetClip(5, 5, size.x - 5, size.y - 40);
    draw_title_screen();
    for (int y = 0; y < size.y; ++y)
        require(std::equal(screen->scan_line(y), screen->scan_line(y) + size.x, full_frame.data() + y * size.x),
                "title art draws across the whole framebuffer even after gameplay clipped it");
    screen->SetClip(left, top, right, bottom);
}

static void check_gameplay_feedback(view *v)
{
    const int center = (v->cx1 + v->cx2) / 2;
    const int amber = pal->find_closest(241, 177, 73);
    const auto count_accent = [&](int left, int top, int right, int bottom, int color) {
        int count = 0;
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                count += screen->Pixel(vec2i(x, y)) == color;
        return count;
    };
    const auto pickup_accent = [&] { return count_accent(center - 112, v->cy2 - 24, center + 112, v->cy2, amber); };
    the_game->update_screen();
    const int before_pickup = pickup_accent();
    const int old_ammo = v->weapons[1];
    v->weapons[1] = -1;
    v->give_weapon(1);
    v->add_ammo(1, 5);
    the_game->update_screen();
    require(v->weapons[1] == 5 && pickup_accent() > before_pickup,
            "the real inventory grant draws a weapon and ammo notice above the HUD");
    v->weapons[1] = old_ammo;

    const int volume = sfx_volume;
    sfx_volume = 0;
    const int sound = lnumber_value(LSymbol::Find("SWISH")->GetValue());
    const int sound_x = v->xoff() + center - v->cx1, sound_y = v->yoff() - 8;
    const int blue = pal->find_closest(105, 157, 194);
    const auto count_cue = [&] { return count_accent(center - 75, v->cy1, center + 75, v->cy1 + 24, blue); };
    game_settings.directional_sound = 0;
    the_game->play_sound(sound, 127, sound_x, sound_y);
    the_game->update_screen();
    const int before_cue = count_cue();
    game_settings.directional_sound = 1;
    the_game->play_sound(sound, 127, sound_x, sound_y);
    the_game->update_screen();
    require(count_cue() > before_cue, "an off-screen door sound draws a directional cue with audio muted");
    sfx_volume = volume;
}

static const vec2i map_test_tile(8, 8);
static vec2i map_test_checkpoint;

static void verify_map_hover(view *v, vec2i mouse, int marker)
{
    wm->set_mouse_position(mouse.x, mouse.y);
    auto *close = new event;
    close->type = EV_KEY;
    close->key = JK_ESC;
    close->mouse_move = {mouse.x, mouse.y};
    close->mouse_button = 0;
    wm->push_event(close);
    show_pause_map(*current_level, *v->focus);
    bool tooltip = false;
    for (int y = mouse.y - 9; y <= mouse.y - 4; ++y) {
        int border_pixels = 0;
        for (int x = mouse.x + 12; x < mouse.x + 32; ++x)
            border_pixels += screen->Pixel(vec2i(x, y)) == marker;
        tooltip |= border_pixels == 20;
    }
    require(tooltip, "hover shows a tooltip with the marker color");
}

static void check_map_discovery(view *v)
{
    const int tw = the_game->ftile_width(), th = the_game->ftile_height();
    constexpr int radius = 4;
    std::array<uint16_t, 9 * 9> original{};
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            auto &tile = current_level->get_fgline(map_test_tile.y + y)[map_test_tile.x + x];
            original[(y + radius) * 9 + x + radius] = tile;
            tile = x == 1 ? 28 : 0;
        }
    const vec2i old_position(v->focus->x, v->focus->y);
    v->focus->x = map_test_tile.x * tw + tw / 2;
    v->focus->y = map_test_tile.y * th + th / 2 + 10;
    int type = 0;
    while (type < total_objects && strcmp(object_names[type], "RESTART_POSITION")) ++type;
    require(type < total_objects, "stock save console exists");
    map_test_checkpoint = vec2i(v->focus->x - 4, v->focus->y);
    auto *checkpoint = create(type, map_test_checkpoint.x, map_test_checkpoint.y, 0);
    current_level->add_object(checkpoint);
    current_level->unactivate_all();
    current_level->add_drawables(v->focus->x - 100, v->focus->y - 100, v->focus->x + 100, v->focus->y + 100);
    current_level->explore_map(*v->focus);
    const auto &checkpoints = current_level->map_checkpoints();
    require(current_level->map_seen(map_test_tile.x, map_test_tile.y) &&
            current_level->map_seen(map_test_tile.x - 2, map_test_tile.y) &&
            !current_level->map_seen(map_test_tile.x + 2, map_test_tile.y),
            "the pause map reveals connected nearby space and hides tiles across a wall");
    require(std::find(checkpoints.begin(), checkpoints.end(), map_test_checkpoint) != checkpoints.end(),
            "a reached checkpoint is recorded for the map");

    type = 0;
    while (type < total_objects && strcmp(object_names[type], "SWITCH")) ++type;
    require(type < total_objects, "stock switch exists");
    auto *map_switch = create(type, v->focus->x - 2 * tw, v->focus->y, 0);
    current_level->add_object(map_switch);
    const auto size = screen->Size();
    const int center_y = (size.y - std::min(size.y - 16, 370)) / 2 +
                         (std::min(size.y - 16, 370) - 7) / 2;
    const int green = pal->find_closest(87, 163, 43), blue = pal->find_closest(111, 199, 230);
    verify_map_hover(v, vec2i(size.x / 2 - 2, center_y), green);
    // The switch's filled horizontal center sits halfway across its blue square.
    vec2i switch_marker(-1, -1);
    for (int y = center_y - 9; y <= center_y + 9; ++y)
        for (int x = size.x / 2 - 38; x < size.x / 2 - 9; ++x)
            if (screen->Pixel(vec2i(x - 1, y)) == blue && screen->Pixel(vec2i(x + 1, y)) == blue &&
                screen->Pixel(vec2i(x - 2, y)) != blue && screen->Pixel(vec2i(x + 2, y)) != blue)
                switch_marker = vec2i(x, y);
    require(switch_marker.x >= 0, "an explored switch is marked on the map");
    verify_map_hover(v, switch_marker, blue);
    current_level->delete_object(map_switch);

    v->focus->x = old_position.x;
    v->focus->y = old_position.y;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
            current_level->get_fgline(map_test_tile.y + y)[map_test_tile.x + x] =
                original[(y + radius) * 9 + x + radius];
    current_level->mark_seen(map_test_tile.x, map_test_tile.y);
    current_level->unactivate_all();
    current_level->add_drawables(v->xoff(), v->yoff(), v->xoff() + xres, v->yoff() + yres);
}

static void check_hud_recovers(view *v)
{
    int left, top, right, bottom;
    sbar.bounds(left, top, right, bottom);
    const auto pixels = [&] {
        std::vector<uint8_t> result;
        for (int y = top; y < bottom; ++y) {
            const auto *row = screen->scan_line(y);
            result.insert(result.end(), row + left, row + right);
        }
        return result;
    };
    the_game->draw(0);
    const auto expected = pixels();
    const int viewport_bottom = v->cy2;
    v->cy2 = bottom - 1; // Force a world redraw over the weapon bar.
    the_game->update_screen();
    const bool survived_world_redraw = pixels() == expected;
    v->cy2 = viewport_bottom;
    screen->bar(left, top, right - 1, bottom - 1, pal->find_closest(0, 0, 0));
    int saved_left, saved_top, saved_right, saved_bottom;
    screen->GetClip(saved_left, saved_top, saved_right, saved_bottom);
    screen->SetClip(0, 0, screen->Size().x, top);
    the_game->update_screen();
    int clip_left, clip_top, clip_right, clip_bottom;
    screen->GetClip(clip_left, clip_top, clip_right, clip_bottom);
    screen->SetClip(saved_left, saved_top, saved_right, saved_bottom);
    require(survived_world_redraw, "world redraw restores the complete HUD without inventory changes");
    require(pixels() == expected, "framebuffer loss restores the complete HUD without inventory changes");
    if (clip_left || clip_top || clip_right != screen->Size().x || clip_bottom != top)
        fprintf(stderr, "HUD clip (%d,%d)-(%d,%d) should be (0,0)-(%d,%d)\n",
                clip_left, clip_top, clip_right, clip_bottom, screen->Size().x, top);
    require(clip_left == 0 && clip_top == 0 && clip_right == screen->Size().x && clip_bottom == top,
            "HUD redraw preserves the world clip");
}

static void check_aim_cursor()
{
    set_aim_cursor();
    auto *source = cache.img(c_target);
    auto *cursor = wm->eh->mouse_sprite()->visual;
    require(cursor->Size() == source->Size() + vec2i(2, 2), "the aiming cursor includes a one-pixel outline");
    bool outlined = false;
    for (int y = 0; y < source->Size().y; ++y) {
        for (int x = 0; x < source->Size().x; ++x) {
            const auto pixel = source->Pixel(vec2i(x, y));
            const auto drawn = cursor->Pixel(vec2i(x + 1, y + 1));
            if (pixel) require(drawn == pixel, "the outline preserves the original aiming mark");
            else if (drawn) outlined = true;
        }
    }
    require(outlined, "transparent pixels beside the aiming mark receive a dark outline");
}

static void snapshot(const char *name);

static void check_interaction_prompt(view *v)
{
    int type = 0;
    while (type < total_objects && strcmp(object_names[type], "SWITCH_ONCE")) ++type;
    require(type < total_objects, "stock one-use switch exists");
    auto *object = create(type, v->focus->x, v->focus->y, 0);
    current_level->add_object(object);
    current_level->unactivate_all();
    current_level->add_drawables(v->xoff(), v->yoff(), v->xoff() + xres, v->yoff() + yres);
    const auto pixels = [&] {
        the_game->draw(0);
        std::vector<uint8_t> result;
        for (int y = v->cy2 - 24; y < v->cy2; ++y) {
            const auto *row = screen->scan_line(y);
            result.insert(result.end(), row + xres / 2 - 72, row + xres / 2 + 72);
        }
        return result;
    };
    object->set_aistate(1);
    const auto used = pixels();
    object->set_aistate(0);
    controller_keyboard_used();
    const auto ready = pixels();
    if (wide_view) snapshot("interaction-prompt.bmp");
    const int binding = control_binding(ControlAction::Use);
    control_binding(ControlAction::Use) = 't';
    const auto rebound = pixels();
    control_binding(ControlAction::Use) = binding;
    require(used != ready && ready != rebound, "a usable switch shows the current Use binding");
    SDL_Event press{};
    press.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    press.gbutton.which = virtual_id;
    press.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    controller_event(press);
    require(controller_use_hint() && pixels() != ready, "controller input shows the gamepad Use button");
    controller_keyboard_used();
    object->set_aistate(1);
    require(pixels() == used, "used one-shot switches no longer show an action prompt");
    current_level->delete_object(object);
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
    test.clear(gray);
    enhanced_light_screen(&test, 20000, 20000, unchanged.data(), 63, *pal, current_level->tick_counter());
    require(test.Pixel(vec2i(35, 50)) != gray && test.Pixel(vec2i(105, 50)) != gray &&
            test.Pixel(vec2i(35, 60)) == gray, "fixture halos surround their source without lifting the room");
    delete_light(brightness);
    delete_light(directional);
    delete_light(round);
}
static void check_foreground_contrast()
{
    image test(vec2i(24, 20), nullptr, 2);
    image surfaces(test.Size(), nullptr, 2);
    const int gray = pal->find_closest(108, 108, 108);
    const int black = pal->find_closest(0, 0, 0);
    test.clear(gray);
    test.scan_line(6)[9] = black;
    surfaces.clear(BackgroundReflection);
    surfaces.bar(5, 6, 16, 14, WallReflection);
    test.SetClip(6, 4, 18, 16);
    enhance_foreground_contrast(test, surfaces, *pal);
    require(pal->red(test.Pixel(vec2i(8, 6))) > pal->red(test.Pixel(vec2i(8, 7))) &&
            pal->red(test.Pixel(vec2i(8, 7))) >= pal->red(gray) && test.Pixel(vec2i(9, 6)) == black &&
            test.Pixel(vec2i(8, 5)) == gray && test.Pixel(vec2i(5, 6)) == gray,
            "foreground contrast favors edges and preserves backgrounds, black pixels and the clip");

    const auto outline_path = root / "ledge-outline.bin";
    const char points[] = {5, 0, 0, 11, 0, 11, 8, 0, 8, 0, 0};
    std::ofstream(outline_path, std::ios::binary).write(points, sizeof(points));
    jFILE outline(outline_path.c_str(), "rb");
    require(!outline.open_failure(), "open the test tile outline");
    boundary floor(&outline, "test ledge");
    surfaces.bar(14, 5, 14, 5, WallReflection);
    surfaces.bar(7, 5, 11, 5, DecorativeReflection);
    test.clear(gray);
    enhance_foreground_contrast(test, surfaces, *pal);
    const auto decorative_luma = pal->red(test.Pixel(vec2i(8, 5)));
    mark_walkable_edges(surfaces, floor, vec2i(5, 6), vec2i(12, 9));
    require(surfaces.Pixel(vec2i(8, 6)) == WalkableReflection &&
            surfaces.Pixel(vec2i(14, 6)) == WallReflection &&
            surfaces.Pixel(vec2i(8, 14)) == WallReflection,
            "only exposed landing edges are marked");

    test.clear(gray);
    test.scan_line(6)[9] = black;
    enhance_foreground_contrast(test, surfaces, *pal);
    require(pal->red(test.Pixel(vec2i(8, 6))) > pal->red(test.Pixel(vec2i(16, 7))) &&
            pal->red(test.Pixel(vec2i(8, 5))) < decorative_luma &&
            test.Pixel(vec2i(9, 6)) == black && test.Pixel(vec2i(17, 5)) == gray &&
            test.Pixel(vec2i(5, 5)) == gray,
            "the ledge highlight and backdrop shading stay local and respect the clip");
}
static void check_lava_lighting()
{
    image test(vec2i(72, 56), nullptr, 2);
    image surfaces(test.Size(), nullptr, 2);
    const int gray = pal->find_closest(108, 108, 108);
    const int black = pal->find_closest(0, 0, 0);
    const LavaSurface strip{20, 43, 36, 0};
    surfaces.clear(BackgroundReflection);
    surfaces.bar(16, 29, 26, 37, WallReflection);
    test.SetClip(4, 4, 68, 52);
    const auto render = [&](const std::vector<LavaSurface> &lava, bool embers) {
        test.clear(gray);
        test.scan_line(34)[32] = black;
        draw_lava_lighting(test, surfaces, *pal, lava, 8, embers);
    };
    render({strip}, false);
    const int bank = test.Pixel(vec2i(23, 34));
    const int backdrop = test.Pixel(vec2i(37, 34));
    require(pal->red(bank) > pal->red(backdrop) && pal->red(backdrop) > pal->red(gray) &&
            test.Pixel(vec2i(32, 34)) == black && test.Pixel(vec2i(36, 12)) == gray,
            "lava warms nearby terrain more than the background and preserves the surrounding dark");
    render({strip, strip}, false);
    require(test.Pixel(vec2i(23, 34)) == bank && test.Pixel(vec2i(37, 34)) == backdrop,
            "overlapping lava sprites don't stack their reflections");
    const vec2i spark(29, 27);
    const int empty = test.Pixel(spark);
    render({strip}, true);
    const int ember = test.Pixel(spark);
    require(ember != empty && pal->red(ember) > pal->green(ember), "lava emits a warm ember");
    surfaces.bar(29, 30, 29, 30, WallReflection);
    render({strip}, true);
    require(test.Pixel(spark) == empty, "embers stay below solid ledges");
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
    reflect_projectile_lights(test, surfaces, 20000, 20000, *pal, true);
    require(pal->red(test.Pixel(vec2i(60, 65))) > pal->red(gray) &&
            test.Pixel(vec2i(60, 68)) == gray,
            "the optional glow hugs the projectile independently of surface reflections");
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

static LObject *eval_lisp(const char *expression)
{
    return LObject::Compile(expression)->Eval();
}

static void check_checkpoint_lighting(view *v)
{
    int type = 0;
    while (type < total_objects && strcmp(object_names[type], "RESTART_POSITION")) ++type;
    require(type < total_objects, "stock save console type exists");
    auto *console = create(type, v->focus->x + 56, v->focus->y + 36, 0);
    current_level->add_object(console);
    current_level->unactivate_all();
    current_level->add_drawables(v->xoff(), v->yoff(), v->xoff() + xres, v->yoff() + yres);

    int32_t left, top, right, bottom;
    console->picture_space(left, top, right, bottom);
    const int width = right - left + 1, height = bottom - top + 1;
    left += v->cx1 - v->xoff();
    top += v->cy1 - v->yoff();
    require(left >= v->cx1 && left + width <= v->cx2 &&
            top >= v->cy1 && top + height <= v->cy2, "checkpoint fixture is inside the viewport");
    const auto brightness = [&](int distance, bool enabled, const char *capture) {
        v->focus->x = console->x - distance;
        game_settings.checkpoint_feedback = enabled;
        the_game->draw(0);
        int total = 0;
        for (int y = top + height / 8; y < top + height / 2; ++y)
            for (int x = left + width / 4; x < left + 3 * width / 4; ++x)
                total += pal->green(screen->Pixel(vec2i(x, y)));
        if (wide_view && capture) snapshot(capture);
        return total;
    };

    const int player_x = v->focus->x;
    const int idle = brightness(160, true, "checkpoint-idle.bmp");
    const int approaching = brightness(103, true, "checkpoint-approaching.bmp");
    const int nearby = brightness(56, true, "checkpoint-nearby.bmp");
    const int disabled = brightness(56, false, nullptr);
    const int leaving = brightness(160, true, nullptr);
    v->focus->x = player_x;
    current_level->delete_object(console);
    require(idle < approaching && approaching < nearby, "the monitor brightens gradually on approach");
    require(disabled == idle && leaving == idle, "the console returns to its idle appearance");
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
    static bool display_revert_seen = false;
    static int display_kept_phase = -1;
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
    } else if ((phase == 52 || phase == 53 || phase == 54 || phase == 62) && page == "KEEP DISPLAY?") {
        if (display_kept_phase == phase) return;
        display_kept_phase = phase;
        GameSettings saved;
        require(settings_load((root / "settings.cfg").string(), saved), "read the confirmed display settings");
        if (phase == 52)
            require(saved.scale == 3, "window scale preview is not saved before confirmation");
        else
            require(xres != view_width(saved.view_resolution) || yres != view_height(saved.view_resolution),
                    "view resolution applies before being persisted");
        SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
        enter();
    } else if (phase == 56 && page == "KEEP DISPLAY?") {
        if (!display_revert_seen && !wide_view) escape();
        display_revert_seen = true;
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
        SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
        if (!selected(menu, "View resolution")) { choose(menu, "View resolution"); return; }
        key(SDLK_RIGHT, SDL_SCANCODE_RIGHT);
        phase = 56;
    } else if (phase == 56 && page == "SETTINGS" && display_revert_seen) {
        const int expected = wide_view ? 3 : 0;
        GameSettings saved;
        require(xres == view_width(expected) && yres == view_height(expected) &&
                settings_load((root / "settings.cfg").string(), saved) && saved.view_resolution == expected,
                "an unconfirmed display change restores the framebuffer and leaves saved settings intact");
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
                menu->items.at(2).value == "[x]" && menu->items.at(3).value == "[ ]" && menu->items.at(4).value == "[x]" &&
                menu->items.at(6).value == "[x]" && menu->items.at(7).value == "[x]",
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
        if (selected(menu, "Combat feedback")) { enter(); phase = 77; }
        else choose(menu, "Combat feedback");
    } else if (phase == 77 && page == "ENHANCEMENTS") {
        if (game_settings.combat_feedback) return;
        if (selected(menu, "Smart crosshair")) { enter(); phase = 78; }
        else choose(menu, "Smart crosshair");
    } else if (phase == 78 && page == "ENHANCEMENTS") {
        if (game_settings.smart_crosshair) return;
        GameSettings reloaded;
        require(menu->items.at(1).value == "[ ]" && menu->items.at(2).value == "[ ]" && menu->items.at(3).value == "[x]" &&
                menu->items.at(4).value == "[ ]" && menu->items.at(6).value == "[ ]" && menu->items.at(7).value == "[ ]" &&
                settings_load((root / "settings.cfg").string(), reloaded) &&
                !reloaded.modern_hud && !reloaded.enhanced_lighting && !reloaded.checkpoint_feedback &&
                reloaded.smooth_pixels && !reloaded.enhanced_projectiles &&
                !reloaded.combat_feedback && !reloaded.smart_crosshair,
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
    } else if (phase == 71 && page == "PAUSED") {
        if (selected(menu, "Map")) {
            enter();
            key(SDLK_EQUALS, SDL_SCANCODE_EQUALS);
            escape();
            phase = 76;
        } else choose(menu, "Map");
    } else if (phase == 76 && page == "PAUSED") {
        require(player_list->focus->hp() == 23, "the pause map returns to the same run");
        if (selected(menu, "Restart checkpoint")) { enter(); phase = 72; }
        else choose(menu, "Restart checkpoint");
    } else if (phase == 72 && page == "RESTART?") {
        if (selected(menu, "Cancel")) { enter(); phase = 74; }
        else choose(menu, "Cancel");
    } else if (phase == 74 && page == "PAUSED") {
        require(player_list->focus->hp() == 23, "cancel keeps the current run");
        if (selected(menu, "Restart checkpoint")) { enter(); phase = 75; }
        else choose(menu, "Restart checkpoint");
    } else if (phase == 75 && page == "RESTART?") {
        if (selected(menu, "Restart checkpoint")) { enter(); phase = 73; }
        else choose(menu, "Restart checkpoint");
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

static void check_combat_feedback(view *v)
{
    const int saved_combat = game_settings.combat_feedback;
    const int saved_crosshair = game_settings.smart_crosshair;
    const int saved_ammo = v->weapons[v->current_weapon];
    const int saved_hp = v->focus->hp();
    const auto differs = [](image &a, image &b, int x1, int y1, int x2, int y2) {
        const int left = std::max(0, x1), top = std::max(0, y1);
        const int right = std::min(a.Size().x - 1, x2), bottom = std::min(a.Size().y - 1, y2);
        for (int y = top; y <= bottom; ++y)
            for (int x = left; x <= right; ++x)
                if (a.scan_line(y)[x] != b.scan_line(y)[x]) return true;
        return false;
    };

    const int32_t saved_px = v->pointer_x, saved_py = v->pointer_y;
    v->pointer_x = v->focus->x + 36;
    v->pointer_y = v->focus->y - 16;

    game_settings.combat_feedback = 0;
    game_settings.smart_crosshair = 0;
    the_game->update_screen();
    image baseline(screen->Size());
    screen->put_image(&baseline, 0, 0);

    int32_t sx = 0, sy = 0;
    the_game->game_to_mouse(v->pointer_x, v->pointer_y, v, sx, sy);

    game_settings.smart_crosshair = 1;
    the_game->note_player_fire(*v);
    the_game->update_screen();
    require(differs(*screen, baseline, sx - 10, sy - 10, sx + 10, sy + 10),
            "smart crosshair draws at the aim position");
    image idle_crosshair(screen->Size());
    screen->put_image(&idle_crosshair, 0, 0);

    v->weapons[v->current_weapon] = 0;
    the_game->update_screen();
    require(differs(*screen, idle_crosshair, sx - 5, sy + 5, sx + 5, sy + 15),
            "empty ammo renders an indicator below the smart crosshair");
    v->weapons[v->current_weapon] = saved_ammo;

    int enemy_type = 0;
    while (enemy_type < total_objects && strcmp(object_names[enemy_type], "ANT_ROOF")) ++enemy_type;
    require(enemy_type < total_objects, "find ANT_ROOF enemy type for combat feedback test");
    auto *enemy = create(enemy_type, v->focus->x + 36, v->focus->y);
    current_level->add_object(enemy);
    int32_t ex1 = 0, ey1 = 0, ex2 = 0, ey2 = 0;
    enemy->picture_space(ex1, ey1, ex2, ey2);
    v->pointer_x = (ex1 + ex2) / 2;
    v->pointer_y = (ey1 + ey2) / 2;
    the_game->game_to_mouse(v->pointer_x, v->pointer_y, v, sx, sy);

    game_settings.combat_feedback = 1;
    the_game->update_screen();
    image before_hit(screen->Size());
    screen->put_image(&before_hit, 0, 0);
    require(differs(*screen, baseline, sx - 8, sy - 8, sx + 8, sy + 8),
            "aiming over a hurtable enemy highlights the smart crosshair");

    enemy->do_damage(1, v->focus, enemy->x, enemy->y, 0, 0);
    require(enemy->hit_flash_ticks > 0, "damaging an enemy triggers hit flash ticks");
    the_game->update_screen();
    require(differs(*screen, before_hit, sx + 3, sy + 3, sx + 8, sy + 8),
            "damaging an enemy draws diagonal hit-marker ticks");
    enemy->picture_space(ex1, ey1, ex2, ey2);
    const int bar_x = std::clamp((ex1 + ex2) / 2 - v->xoff() + v->cx1 - 13,
                                 v->cx1 + 2, std::max(v->cx1 + 2, v->cx2 - 28));
    const int bar_y = std::clamp(ey1 - v->yoff() + v->cy1 - 7,
                                 v->cy1 + 2, std::max(v->cy1 + 2, v->cy2 - 5));
    require(differs(*screen, before_hit, bar_x - 1, bar_y - 1, bar_x + 26, bar_y + 3),
            "damaging an enemy draws a compact target health bar above it");

    v->pointer_x = saved_px;
    v->pointer_y = saved_py;

    v->focus->do_damage(5, enemy, v->focus->x, v->focus->y, 0, 0);
    the_game->update_screen();
    require(differs(*screen, baseline, v->cx1, v->cy1, v->cx1 + 4, v->cy1 + 4),
            "player damage draws a viewport edge vignette");
    current_level->delete_object(enemy);

    // Clear recent damage vignette via level reload? No—just test low-health pulse directly after expiry or on fresh state:
    game_settings.combat_feedback = 0;
    v->focus->set_hp(10);
    the_game->update_screen();
    image low_hp_off(screen->Size());
    screen->put_image(&low_hp_off, 0, 0);
    game_settings.combat_feedback = 1;
    the_game->update_screen();
    require(differs(*screen, low_hp_off, v->cx1, v->cy1, v->cx1 + 4, v->cy1 + 4),
            "low player health draws a pulsing edge warning");
    v->focus->set_hp(saved_hp);

    // Quick weapon hotkeys ('1'-'8'), last-weapon swap ('q'), and weapon switch badge
    const int saved_cur_weapon = v->current_weapon;
    const int saved_prev_weapon = v->previous_weapon;
    const int saved_w1 = total_weapons > 1 ? v->weapons[1] : -1;
    const int saved_w7 = total_weapons > 7 ? v->weapons[7] : -1;
    if (total_weapons > 1) {
        v->weapons[1] = 25;
        const auto apply_weapon_change = [&](int slot) {
            uint8_t bytes[4];
            const int32_t le = lltl(slot);
            memcpy(bytes, &le, 4);
            uint8_t *ptr = bytes;
            require(v->process_input(SCMD_WEAPON_CHANGE, ptr) == 1, "process weapon change command");
        };
        event key_ev;
        key_ev.type = EV_KEY;
        key_ev.key = '2';
        v->handle_event(key_ev);
        require(v->suggest.send_weapon_change && v->suggest.new_weapon == 1, "hotkey '2' selects weapon slot 1");
        apply_weapon_change(v->suggest.new_weapon);
        require(v->current_weapon == 1 && v->previous_weapon == saved_cur_weapon, "weapon change tracks previous weapon");
        if (total_weapons > 7) {
            v->weapons[7] = 15;
            key_ev.key = '8';
            v->handle_event(key_ev);
            require(v->suggest.send_weapon_change && v->suggest.new_weapon == 7, "hotkey '8' selects weapon slot 7");
            apply_weapon_change(v->suggest.new_weapon);
        }
        key_ev.key = 'q';
        require(v->handle_event(key_ev) == 1 && v->suggest.send_weapon_change &&
                v->suggest.new_weapon == (total_weapons > 7 ? 1 : saved_cur_weapon),
                "'q' swaps back to the previous weapon");
        apply_weapon_change(v->suggest.new_weapon);
        the_game->update_screen();
        const int mid_x = (v->cx1 + v->cx2) / 2;
        require(differs(*screen, baseline, mid_x - 40, v->cy2 - 30, mid_x + 40, v->cy2 - 4),
                "switching weapons displays a brief weapon/ammo badge");
        v->weapons[1] = saved_w1;
        if (total_weapons > 7) v->weapons[7] = saved_w7;
    }
    v->current_weapon = saved_cur_weapon;
    v->previous_weapon = saved_prev_weapon;
    v->suggest.send_weapon_change = 0;

    game_settings.combat_feedback = saved_combat;
    game_settings.smart_crosshair = saved_crosshair;
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
                    check_combat_feedback(v);
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
        check_title_frame();
        check_hud_recovers(v);
        check_aim_cursor();
        check_interaction_prompt(v);
        check_map_discovery(v);
        check_gameplay_feedback(v);
        if (wide_view) {
            std::vector<int> ammo;
            for (int i = 0; i < total_weapons; ++i) { ammo.push_back(v->weapons[i]); v->weapons[i] = 100 + i; }
            the_game->draw(0); snapshot("gameplay-wide.bmp");
            for (int i = 0; i < total_weapons; ++i) v->weapons[i] = ammo[i];
        }
        check_explosion_effect();
        check_projectile_lights();
        check_environment_lights();
        check_foreground_contrast();
        check_lava_lighting();
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
        check_checkpoint_lighting(v);
        current_level->set_time_played_ticks(15 * 70);
        eval_lisp("(save_game \"save0015.spe\")");
        require(save_notices == 1, "the checkpoint save command confirms a successful write");
        eval_lisp("(save_game \"missing/checkpoint.spe\")");
        require(save_notices == 1, "a save that cannot open its destination gives no success message");
        const auto saved = scan_save_slots(root.string()).at(14);
        require(saved.state == SaveSlotState::Saved && !saved.level.empty() && !saved.saved_at.empty() &&
                saved.width == 160 && saved.height == 100 && saved.preview.size() == 16000 &&
                saved.difficulty == 3 && saved.play_ticks == 15 * 70 && last_checkpoint_slot() == 15,
                "a real save supplies level, date, screenshot, difficulty and play time");
        auto *saved_object = current_object;
        auto *station = current_level->first_object();
        while (station && strcmp(object_names[station->otype], "RESTART_POSITION")) station = station->next;
        require(station, "checkpoint station needed for the metadata test");
        const auto station_id = station->xvel();
        station->set_xvel(7);
        current_object = station;
        phase = 21;
        require(lnumber_value(eval_lisp("(get_save_slot)")) == 15 && phase == 27,
                "the Lisp checkpoint picker cancels then confirms overwrite of slot 15");
        current_object = saved_object;
        station->set_xvel(station_id);
        // A modal screen can still be displayed when Lisp writes the save.
        screen->clear(211);
        eval_lisp("(save_game \"save0015.spe\")");
        const auto overwritten = scan_save_slots(root.string()).at(14);
        require(overwritten.preview == saved.preview && overwritten.station == 7,
                "checkpoint overwrite retains the gameplay frame and records the chosen station");
        phase = 25;
        require(lnumber_value(eval_lisp("(get_save_slot)")) == 0 && phase == 26,
                "escape cancels the Lisp checkpoint picker");
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
        require(current_level->time_played_ticks() >= 15 * 70, "load dialog restores campaign play time");
        require(v->cx2 < xres && v->cy2 < yres, "save loading adapts camera to current live resolution");
        check_hud();
        check_hud_recovers(v);
        eval_lisp("(save_game \"silent-checkpoint.spe\")");
        require(std::filesystem::exists(root / "silent-checkpoint.spe") && save_notices == 2,
                "switching checkpoint feedback off suppresses the notice without preventing saving");
        v->focus->set_hp(23);
        phase = 71; escape();
    } else if (phase == 73) {
        require(v->focus->hp() == 73 && last_checkpoint_slot() == 15,
                "pause-menu restart restores the last checkpoint");
        const auto &checkpoints = current_level->map_checkpoints();
        require(current_level->map_seen(map_test_tile.x, map_test_tile.y) &&
                std::find(checkpoints.begin(), checkpoints.end(), map_test_checkpoint) != checkpoints.end(),
                "checkpoint restart restores explored tiles and reached checkpoint markers");
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        escape();
        the_game->get_input();
        require(the_game->state == RUN_STATE, "Tab opens the map and Escape returns directly to gameplay");
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        key(SDLK_TAB, SDL_SCANCODE_TAB);
        the_game->get_input();
        require(the_game->state == RUN_STATE && v->focus->hp() == 73, "Tab also closes the map");
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
