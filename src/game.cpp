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

#if defined HAVE_CONFIG_H
#   include "config.h"
#endif

#include <ctype.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <setjmp.h>
#include <unistd.h>

#ifdef __APPLE__
// SDL for OSX needs to override main()
#   include <SDL3/SDL.h>
#endif

#include "common.h"

#include "sdlport/joy.h"
#include "sdlport/preferences.h"
#include "sdlport/platform.h"
#include "sdlport/controller.h"
#include "sdlport/controls.h"
#include "file_path.h"

#include "dev.h"
#include "game.h"

#include "id.h"
#include "timing.h"
#include "automap.h"
#include "help.h"
#include "ability.h"
#include "cache.h"
#include "lisp.h"
#include "jrand.h"
#include "configuration.h"
#include "light.h"
#include "scroller.h"
#include "dprint.h"
#include "nfserver.h"
#include "video.h"
#include "transp.h"
#include "clisp.h"
#include "guistat.h"
#include "menu.h"
#include "gamma.h"
#include "lisp_gc.h"
#include "demo.h"
#include "sbar.h"
#include "profile.h"
#include "compiled.h"
#include "lisp_gc.h"
#include "pmenu.h"
#include "timing.h"
#include "chat.h"
#include "demo.h"
#include "netcfg.h"
#include "ui/textmenu.h"

#define SHIFT_RIGHT_DEFAULT 0
#define SHIFT_DOWN_DEFAULT 30

extern CrcManager *net_crcs;

Game *the_game = NULL;
WindowManager *wm = NULL;
static vec2i aim_hotspot(8, 8);

void set_aim_cursor()
{
    auto *source = cache.img(c_target);
    auto *outlined = new image(source->Size() + vec2i(2, 2));
    outlined->clear();
    const uint8_t outline = pal->find_closest_non0(8, 8, 8);
    for (int y = 0; y < source->Size().y; ++y)
        for (int x = 0; x < source->Size().x; ++x)
            if (source->scan_line(y)[x])
                for (int row = y; row < y + 3; ++row)
                    for (int col = x; col < x + 3; ++col) outlined->scan_line(row)[col] = outline;
    source->put_image(outlined, 1, 1, 1);
    wm->set_mouse_shape(outlined, aim_hotspot.x + 1, aim_hotspot.y + 1);
}

void set_aim_cursor_hotspot(int x, int y)
{
    aim_hotspot = vec2i(x, y);
    if (screen && wm) set_aim_cursor();
}
int dev, shift_down = SHIFT_DOWN_DEFAULT, shift_right = SHIFT_RIGHT_DEFAULT;
double sum_diffs = 1, total_diffs = 12;
int total_active = 0;
int32_t map_xoff = 0, map_yoff = 0;
int32_t current_vxadd, current_vyadd;
int frame_panic = 0, massive_frame_panic = 0;
int demo_start = 0, idle_ticks = 0;
int req_end = 0;

extern palette *old_pal;
char **start_argv;
int start_argc;
int has_joystick = 0;
char req_name[100];

extern uint8_t chatting_enabled;

// Enabled TCPIP driver
#if !defined __CELLOS_LV2__
#include "tcpip.h"
tcpip_protocol tcpip;
#endif

FILE *open_FILE(char const *filename, char const *mode)
{
    const char *prefix = get_filename_prefix();
    const std::string path = prefix && !rooted_filename(filename) ? std::string(prefix) + filename : filename;
    return open_utf8_file(path.c_str(), mode);
}

void handle_no_space()
{
    static char const *no_space_msg =
        "\nYou are out of disk space or the game\n"
        "was unable to write to disk for some reason\n"
        "The game cannot continue, please check this out\n"
        "and try again.\n";

    if(!wm)
    {
        fprintf(stderr, "%s\n", no_space_msg);
        exit(0);
    }

    info_field *inf = new info_field(0, wm->font()->height() * 2, ID_NULL,
                                     no_space_msg, NULL);
    button *b = new button(0, 0, ID_QUIT_OK, "Quit", inf);
    Jwindow *no_space = wm->new_window(0, 0, -1, -1, b, "ERROR");

    event ev;
    do
    {
        wm->flush_screen();
        wm->get_event(ev);
    } while(ev.type != EV_MESSAGE || ev.message.id != ID_QUIT_OK);
    wm->close_window(no_space);

    close_graphics();
    exit(1);
}

void Game::play_sound(int id, int vol, int32_t x, int32_t y)
{
    const bool cue_enabled = game_settings.directional_sound && state == RUN_STATE && !(dev & (EDIT_MODE | MAP_MODE)) &&
        demo_man.current_state() == demo_manager::NORMAL;
    if (vol < 1 || !player_list || (!(sound_avail & SFX_INITIALIZED) && !cue_enabled)) return;

    int mindist = 500;
    view *cd = NULL;
    for(view *f = player_list; f; f = f->next)
    {
        if(!f->local_player())
            continue;

        int d, cx = abs(f->x_center() - x), cy = abs(f->y_center() - y);
        if(cx < cy)
            d = cx + cy - (cx >> 1);
        else
            d = cx + cy - (cy >> 1);

        if(d < mindist)
        {
            cd = f;
            mindist = d;
        }
    }
    if (!cd) return;

    if(mindist < 100)
        mindist = 0;
    else
        mindist -= 100;

    if (cue_enabled && cd->focus && mindist <= 400) {
        const auto sound = std::find(cue_sound_ids.begin(), cue_sound_ids.end(), id);
        if (sound != cue_sound_ids.end()) {
            const int sx = x - cd->xoff() + cd->cx1, sy = y - cd->yoff() + cd->cy1;
            if (sx < cd->cx1 || sy < cd->cy1 || sx > cd->cx2 || sy > cd->cy2) {
                const auto index = size_t(sound - cue_sound_ids.begin());
                sound_cues[index == 0 ? 0 : index <= 2 ? 1 : 2] = {vec2i(x, y), SDL_GetTicks() + 1650};
            }
        }
    }

    // Calculate the position of the sound relative to the player
    int p = (cd->x_center() - x) / 2 + 128;
    if(p < 0)
        p = 0;
    if(p > 255)
        p = 255;

    int v = (400 - mindist) * sfx_volume / 400 - (127 - vol);
    if(v > 0 && (sound_avail & SFX_INITIALIZED))
        cache.sfx(id)->play(v, 128, p);
}

void Game::note_weapon_pickup(view &v, int weapon, int ammo, bool acquired)
{
    if (state != RUN_STATE || &v != player_list || !v.local_player() || (dev & EDIT_MODE) ||
        demo_man.current_state() != demo_manager::NORMAL) return;
    const auto now = SDL_GetTicks();
    auto match = std::find_if(pickup_notices.begin(), pickup_notices.end(),
        [&](const PickupNotice &notice) { return notice.weapon == weapon && notice.expires > now; });
    if (match == pickup_notices.end()) {
        pickup_notices.back() = {};
        match = pickup_notices.end() - 1;
    }
    std::rotate(pickup_notices.begin(), match, match + 1);
    auto &notice = pickup_notices.front();
    notice.weapon = weapon;
    notice.ammo += ammo;
    notice.acquired |= acquired;
    notice.expires = now + 2200;
}

static std::string pickup_weapon_name(int weapon)
{
    const std::string_view type = object_names[weapon_types[weapon]];
    if (type == "MGUN_TOP") return "Machine gun";
    if (type == "GRENADE_TOP") return "Grenade launcher";
    if (type == "ROCKET_TOP") return "Rocket launcher";
    if (type == "FIREBOMB_TOP") return "Firebomb";
    if (type == "PGUN_TOP") return "Plasma gun";
    if (type == "LIGHT_SABER") return "Light saber";
    if (type == "DFRIS_TOP") return "Death frisbee";
    return "Weapon " + std::to_string(weapon + 1);
}

void Game::draw_gameplay_feedback(view &v)
{
    if (state != RUN_STATE || !v.focus || !v.local_player() || (dev & (EDIT_MODE | MAP_MODE))) return;
    const auto now = SDL_GetTicks();
    const auto active = [now](const auto &feedback) { return feedback.expires > now; };
    const bool show_pickups = &v == player_list && std::any_of(pickup_notices.begin(), pickup_notices.end(), active);
    const bool show_cues = game_settings.directional_sound && std::any_of(sound_cues.begin(), sound_cues.end(), active);
    if (!show_pickups && !show_cues) return;
    const auto center = vec2i((v.cx1 + v.cx2) / 2, (v.cy1 + v.cy2) / 2);
    const int height = wm->font()->height();
    int left, top, right, bottom;
    screen->GetClip(left, top, right, bottom);
    screen->SetClip(v.cx1, v.cy1, v.cx2 + 1, v.cy2 + 1);
    const int background = pal->find_closest(18, 24, 30);
    const int ink = pal->find_closest(236, 239, 226);
    const int amber = pal->find_closest(241, 177, 73);
    const int blue = pal->find_closest(105, 157, 194);
    const auto badge = [&](int x, int y, const std::string &label, int accent) {
        const int text_width = label.size() * wm->font()->width();
        const int half = (text_width + 12) / 2;
        if (v.cx2 - v.cx1 < 2 * half + 4 || v.cy2 - v.cy1 < height + 8) return;
        x = std::clamp(x, v.cx1 + half + 2, v.cx2 - half - 2);
        y = std::clamp(y, v.cy1 + 2, v.cy2 - height - 5);
        screen->bar(x - half, y, x + half, y + height + 3, background);
        screen->bar(x - half, y, x - half + 1, y + height + 3, accent);
        wm->font()->put_string(screen, x - text_width / 2, y + 2, label.c_str(), ink);
    };
    if (show_pickups) {
        int y = v.cy2 - height - 8;
        for (const auto &notice : pickup_notices) {
            if (notice.expires <= now) continue;
            auto label = (notice.acquired ? "New: " : "") + pickup_weapon_name(notice.weapon);
            if (notice.ammo) label += "  +" + std::to_string(notice.ammo) + " ammo";
            badge(center.x, y, label, amber);
            y -= height + 7;
        }
    }
    if (show_cues) {
        static const char *names[] = {"Door", "Lift", "Enemy alert"};
        for (size_t i = 0; i < sound_cues.size(); ++i) {
            const auto &cue = sound_cues[i];
            if (cue.expires <= now) continue;
            const auto pos = cue.position - vec2i(v.xoff() - v.cx1, v.yoff() - v.cy1);
            if (pos.x >= v.cx1 && pos.x <= v.cx2 && pos.y >= v.cy1 && pos.y <= v.cy2) continue;
            const auto delta = pos - center;
            const int half_width = std::max(1, center.x - v.cx1), half_height = std::max(1, center.y - v.cy1);
            const float horizontal = float(std::abs(delta.x)) / half_width;
            const float vertical = float(std::abs(delta.y)) / half_height;
            const float scale = std::max(horizontal, vertical);
            const int x = center.x + int(delta.x / scale);
            const int y = center.y + int(delta.y / scale) - height / 2;
            std::string label = names[i];
            if (horizontal >= vertical)
                label = delta.x < 0 ? "< " + label : label + " >";
            else
                label += delta.y < 0 ? "  ^" : "  v";
            const int row = height + 6;
            const int inset = 6 + int(i) * row;
            if (horizontal >= vertical) {
                const int last_row = std::max(v.cy1 + 2, v.cy2 - 3 * row);
                const int lane = std::clamp(y - row, v.cy1 + 2, last_row) + int(i) * row;
                badge(x, lane, label, blue);
            } else {
                badge(x, y + (delta.y < 0 ? inset : -inset), label, blue);
            }
        }
    }
    screen->SetClip(left, top, right, bottom);
}

int get_option(char const *name)
{
    int i;
    for(i = 1; i < start_argc; i++)
    {
        if(!strcmp(start_argv[i], name))
        {
            return i;
        }
    }
    return 0;
}


bool Game::set_view_resolution(int preset, bool persist)
{
    if (!platform_set_view_resolution(preset, persist)) return false;
    delete small_render; small_render = nullptr; small_render_on = 0;
    recalc_local_view_space();
    // Menu/title views are separate from player_list.
    for (view *v = first_view; v; v = v->next) {
        if (v->local_player()) continue;
        v->cx1 = 2; v->cy1 = 5;
        v->cx2 = xres - 2; v->cy2 = yres - (total_weapons ? 33 : 5);
    }
    mousex = xres / 2; mousey = yres / 2;
    wm->set_mouse_position(mousex, mousey);
    need_refresh();
    return true;
}

void make_screen_size(int w, int h)
{
    for(view *f = player_list; f; f = f->next)
    {
        if(!f->local_player())
            continue;

        if(w >= xres - 1)
            w = xres - 2;
        if(h >= yres - 1)
            h = yres - 2;
        f->suggest.cx1 = (xres + 1) / 2 - w / 2;
        f->suggest.cx2 = (xres + 1) / 2 + w / 2;
        f->suggest.cy1 = (yres - 31) / 2 + 5 - h / 2;
        f->suggest.cy2 = (yres - 51) / 2 + 5 + h / 2;
        f->suggest.shift_down = f->shift_down;
        f->suggest.shift_right = f->shift_right;
        f->suggest.pan_x = f->pan_x;
        f->suggest.pan_y = f->pan_y;
        f->suggest.send_view = 1;
    }
}

void Game::grow_views(int amount)
{
    view *f;

    for(f = first_view; f; f = f->next)
    {
        if(!f->local_player())
            continue;

        f->suggest.cx1=(f->cx1 - amount);
        f->suggest.cy1 = f->cy1 - amount / 2;
        f->suggest.cx2=(f->cx2 + amount);
        f->suggest.cy2 = f->cy2 + amount / 2;
        f->suggest.shift_down = f->shift_down;
        f->suggest.shift_right = f->shift_right;
        f->suggest.pan_x = f->pan_x;
        f->suggest.pan_y = f->pan_y;

        f->suggest.send_view = 1;
    }

    for(f = first_view; f; f = f->next)
    {
        if(!f->local_player())
            continue;

        if(f->suggest.cx2 - f->suggest.cx1 < 20
           || f->suggest.cy2 - f->suggest.cy1 < 15
           || f->suggest.cx1 < 0 || f->suggest.cy1 < 0)
            f->suggest.send_view = 0;

        if(f->next && f->next->local_player()
           && f->suggest.cy2 >= f->next->cy1)
            f->suggest.send_view = 0;
    }
}

void Game::pan(int xv, int yv)
{
    first_view->pan_x += xv;
    first_view->pan_y += yv;
}

view *Game::view_in(int mousex, int mousey)
{
    for(view *f = first_view; f; f = f->next)
        if(f->drawable() && mousex >= f->cx1 && mousey >= f->cy1
           && mousex <= f->cx2 && mousey <= f->cy2)
            return f;
    return NULL;
}

int playing_state(int state)
{
    return state == RUN_STATE || state == PAUSE_STATE;
}

void Game::ftile_on(int screenx, int screeny, int32_t &x, int32_t &y)
{
    mouse_to_game(screenx, screeny, x, y);
    x /= ftile_width();
    y /= ftile_height();
}

void Game::btile_on(int screenx, int screeny, int32_t &x, int32_t &y)
{
    view *f = view_in(screenx, screeny);
    if(f)
    {
        x = ((int32_t)screenx - (int32_t)f->cx1
                + f->xoff() * bg_xmul / bg_xdiv) / (int32_t)b_wid;
        y = ((int32_t)screeny - (int32_t)f->cy1
                + f->yoff() * bg_ymul / bg_ydiv) / (int32_t)b_hi;
    }
    else
    {
        x = -1;
        y = -1;
    }
}

void Game::mouse_to_game(int32_t x, int32_t y,
                         int32_t &gamex, int32_t &gamey, view *f)
{
    if(!f)
        f = view_in(x, y);
    if(!f)
        f = player_list;  // if not in a view use the first one

    if(f)
    {
        if(dev & MAP_MODE)
        {
            gamex = (x - (int32_t)f->cx1) * ftile_width() / AUTOTILE_WIDTH + map_xoff * ftile_width();
            gamey = (y - (int32_t)f->cy1) * ftile_height() / AUTOTILE_HEIGHT + map_yoff * ftile_height();
        }
        else
        {
            gamex = x - (int32_t)f->cx1 + f->xoff();
            gamey = y - (int32_t)f->cy1 + f->yoff();
        }
    }
}

void Game::game_to_mouse(int32_t gamex, int32_t gamey, view *which,
                         int32_t &x, int32_t &y)
{
    if(!(dev & MAP_MODE))
    {
        x = gamex - which->xoff() + which->cx1;
        y = gamey - which->yoff() + which->cy1;
        return;
    }

    int32_t x1, y1;

    if(dev & EDIT_MODE)
    {
        x1 = map_xoff;
        y1 = map_yoff;
    }
    else
    {
        if(which->focus)
        {
            x1 = which->focus->x / ftile_width()
                  - (which->cx2 - which->cx1) / AUTOTILE_WIDTH / 2;
            y1 = which->focus->y / ftile_height()
                  - (which->cy2 - which->cy1) / AUTOTILE_HEIGHT / 2;
        }
        else
            x1 = y1 = 0;
    }

    if(x1 < 0)
        x1 = 0;
    if(y1 < 0)
        y1 = 0;

    x = gamex * AUTOTILE_WIDTH / ftile_width()
          - x1 * AUTOTILE_WIDTH + which->cx1;
    if(x1 > 0)
        x -= (which->focus->x * AUTOTILE_WIDTH / ftile_width())
               % AUTOTILE_WIDTH;

    y = gamey * AUTOTILE_HEIGHT / ftile_height()
          - y1 * AUTOTILE_HEIGHT + which->cy1;
    if(y1 > 0)
        y -= (which->focus->y * AUTOTILE_HEIGHT / ftile_height())
               % AUTOTILE_HEIGHT;
}

int window_state(int state)
{
    switch (state)
    {
    case RUN_STATE:
    case PAUSE_STATE:
    case JOY_CALB_STATE:
        return 1;

    case INTRO_START_STATE:
    case HELP_STATE:
    case INTRO_MORPH_STATE:
    case MENU_STATE:
    case SCENE_STATE:
        return 0;
    }

    return 1;
}

void Game::set_state(int new_state)
{
    int d = 0;
    reset_keymap(); // we think all the keys are up right now

    if(playing_state(new_state) && !playing_state(state))
    {
        if(first_view && first_view != player_list)
        {
            while(first_view)
            {
                view *tmp = first_view;
                first_view = first_view->next;
                delete tmp;
            }
            first_view = old_view;
            old_view = NULL;
        }
        first_view = player_list;
        d = 1;
    }
    else if(!playing_state(new_state) && (playing_state(state) || state == START_STATE))
    {
        if(player_list)
        {
            first_view = new view(player_list->focus, NULL, -1);
            first_view->pan_x = player_list->xoff();
            first_view->pan_y = player_list->yoff();
        }
        else
            first_view = new view(NULL, NULL, 0);
        first_view->cx1 = (xres + 1) / 2 - 155;
        first_view->cy1 = (yres + 1) / 2 - 95;
        first_view->cx2 = (xres + 1) / 2 + 155;
        if(total_weapons)
            first_view->cy2 = (yres + 1) / 2 + 68;
        else
            first_view->cy2 = (yres + 1) / 2 + 95;
        d = 1;
    }

    // switching to / from scene mode cause the screen size to change and the border to change
    // so we need to redraw.
    if(window_state(new_state) && !window_state(state))
        wm->show_windows();
    else if(!window_state(new_state) && window_state(state))
        wm->hide_windows();

    int old_state = state;
    state = new_state;

    pal->load();    // restore old palette

    if(playing_state(state) &&  !(dev & EDIT_MODE))
        set_aim_cursor();
    else
        wm->set_mouse_shape(cache.img(c_normal)->copy(), 1, 1);

    if(old_state == SCENE_STATE && new_state != SCENE_STATE)
    {
        d = 1;
        scene_director.set_abort(0);   // don't skip any more scene stuff
    }
    else if(new_state == SCENE_STATE && old_state != SCENE_STATE)
        d = 1;

    if(d)
        draw(state == SCENE_STATE);

    dev_cont->set_state(new_state);
}

void Game::joy_calb(event &ev)
{
    if(!joy_win) // make sure the joystick calibration window is open
        return;

    if(ev.type == EV_SPURIOUS) // spurious means we should update our status
    {
        int b1, b2, b3 = 0, x, y;
        joy_status(b1, b2, b2, x, y);
        int but = b1|b2|b3;
        if(x > 0) x = 1; else if(x < 0) x = -1;
        if(y > 0) y = 1; else if(y < 0) y = -1;
        if(but) but = 1;
        int dx = 20, dy = 5;
        image *jim = cache.img(joy_picts[but * 9+(y + 1)*3 + x + 1]);
        joy_win->screen->bar(dx, dy, dx + jim->Size().x+6, dy + jim->Size().y+6, wm->black());
        jim->put_image(joy_win->screen, dx + 3, dy + 3);

        if(but)
            joy_calibrate();
    }
    else if(ev.type == EV_MESSAGE && ev.message.id == JOY_OK)
    {
        wm->close_window(joy_win);
        joy_win = NULL;
        set_state(MENU_STATE);
    }
}

void Game::menu_select(event &ev)
{
    state = DEV_MOUSE_RELEASE;
    if(top_menu)
    {
#if 0
        wm->push_event(new event(men_mess[((pick_list *)ev.message.data)->get_selection()], NULL));
        wm->close_window(top_menu);
        top_menu = NULL;
#endif
    }
}


void Game::show_help(char const *st)
{
    strcpy(help_text, st);
    help_text_frames = 0;
    refresh = 1;
}

void Game::draw_value(image *screen, int x, int y, int w, int h,
                      int val, int max)
{
    screen->bar(x, y, x + w - 1, y + h, wm->dark_color());
    screen->bar(x, y + 1, x + w * val / max, y + h - 1, wm->bright_color());
}


void Game::set_level(level *nl)
{
    if(current_level)
        delete current_level;
    current_level = nl;
}

void scale_put(image *source, image *target, int x, int y, short width, short height);

static void draw_fitted_art(image &art, int area_height, bool enlarge, int inset = 0)
{
    const auto original = art.Size();
    const int area_width = screen->Size().x;
    const int available_height = area_height - 2 * inset;
    const int height_limit = enlarge ? available_height : std::min(available_height, original.y);
    const int width = std::max(1, std::min(area_width - 2 * inset, height_limit * original.x / original.y));
    const int height = std::max(1, width * original.y / original.x);
    const int x = (area_width - width) / 2, y = (area_height - height) / 2;
    if (width == original.x && height == original.y) art.put_image(screen, x, y);
    else scale_put(&art, screen, x, y, width, height);
}

void draw_title_screen()
{
    int left, top, right, bottom;
    screen->GetClip(left, top, right, bottom);
    screen->SetClip(0, 0, screen->Size().x, screen->Size().y);
    platform_end_menu_text();
    screen->clear();
    if (title_screen >= 0) draw_fitted_art(*cache.img(title_screen), screen->Size().y, true);
    screen->SetClip(left, top, right, bottom);
}

static void show_loading_screen(const char *name, spec_directory &directory, bFILE *file)
{
    std::string label = name;
    if (const auto *entry = directory.find("first name"); entry && entry->size >= 2) {
        file->seek(entry->offset, 0);
        const int length = file->read_uint8();
        char original[256];
        if (length > 1 && uint32_t(length) < entry->size &&
            file->read(original, length) == length && original[length - 1] == '\0')
            label = original;
    }
    const auto slash = label.find_last_of("/\\");
    if (slash != std::string::npos) label.erase(0, slash + 1);
    if (label.size() > 4 && label.compare(label.size() - 4, 4, ".spe") == 0) label.resize(label.size() - 4);
    if (label.size() == 7 && label.compare(0, 5, "level") == 0 &&
        isdigit(static_cast<unsigned char>(label[5])) && isdigit(static_cast<unsigned char>(label[6])))
        label = "Level " + label.substr(5);

    int left, top, right, bottom;
    screen->GetClip(left, top, right, bottom);
    const auto size = screen->Size();
    screen->SetClip(0, 0, size.x, size.y);
    MenuText text(*big_font, platform_begin_menu_text());
    const int ink = pal->find_closest(236, 239, 226), muted = pal->find_closest(158, 171, 176);
    const int caption = size.y - 42;
    screen->clear(pal->find_closest(10, 14, 20));
    if (title_screen >= 0) draw_fitted_art(*cache.img(title_screen), caption, true, 6);
    const auto title = text.fit("Loading", size.x - 24, true);
    label = text.fit(label, size.x - 24);
    text.put(*screen, (size.x - text.width(title, true)) / 2, caption + 1, title, ink, true);
    text.put(*screen, (size.x - text.width(label)) / 2, caption + 22, label, muted);
    wm->flush_screen();
    screen->SetClip(left, top, right, bottom);
}

void Game::load_level(char const *name, bool new_game)
{
    const uint32_t previous_play_ticks = current_level && !new_game ? current_level->time_played_ticks() : 0;
    delete current_level;
    current_level = nullptr;
    pickup_notices = {};
    sound_cues = {};

    bFILE *fp = open_file(name, "rb");

    if(fp->open_failure())
    {
        delete fp;
        current_level = new level(100, 100, name);
        char msg[100];
        sprintf(msg, symbol_str("no_file"), name);
        show_help(msg);
    }
    else
    {
        spec_directory sd(fp);
        const bool show_loading = !(dev & EDIT_MODE) && demo_man.current_state() == demo_manager::NORMAL;
        if (show_loading) show_loading_screen(name, sd, fp);
        current_level = new level(&sd, fp, name);
        if (!sd.find("player_info")) current_level->set_time_played_ticks(previous_play_ticks);
        if (show_loading) platform_end_menu_text();
        delete fp;
    }

#if !defined __CELLOS_LV2__
    base->current_tick=(current_level->tick_counter()&0xff);
#endif

    current_level->level_loaded_notify();
    the_game->help_text_frames = 0;
}

int Game::done()
{
  return finished || (main_net_cfg && main_net_cfg->restart_state());
}

void Game::end_session()
{
  finished = true;
  if(main_net_cfg)
  {
    delete main_net_cfg;
    main_net_cfg = NULL;
  }
}

void Game::put_block_fg(int x, int y, TransImage *im)
{
  for(view *f = first_view; f; f = f->next)
  {
    if(f->drawable())
    {
      int xoff = f->xoff(), yoff = f->yoff(), viewx1 = f->cx1, viewy1 = f->cy1, viewx2 = f->cx2, viewy2 = f->cy2;
      if(xoff / ftile_width()>x || xoff / ftile_width()+(viewx2 - viewx1)/ftile_width()+1 < x ||
      yoff / ftile_height()>y || yoff / ftile_height()+(viewy2 - viewy1)/ftile_height()+1 < y) return;
      int cx1, cy1, cx2, cy2;
      screen->GetClip(cx1, cy1, cx2, cy2);
      screen->SetClip(viewx1, viewy1, viewx2 + 1, viewy2 + 1);
      im->PutImage(screen, vec2i((x - xoff / ftile_width())*ftile_width()+viewx1 - xoff % ftile_width(),
            (y - yoff / ftile_height())*ftile_height()+viewy1 - yoff % ftile_height()));
      screen->SetClip(cx1, cy1, cx2, cy2);
    }
  }
}

void Game::put_block_bg(int x, int y, image *im)
{
  for(view *f = first_view; f; f = f->next)
  {
    if(f->drawable())
    {
      int xoff = f->xoff(), yoff = f->yoff(), viewx1 = f->cx1, viewy1 = f->cy1, viewx2 = f->cx2, viewy2 = f->cy2;
      int xo = xoff * bg_xmul / bg_xdiv;
      int yo = yoff * bg_ymul / bg_ydiv;

      if(xo / btile_width()>x || xo / btile_width()+(viewx2 - viewx1)/btile_width()+1 < x ||
      yo / btile_height()>y || yo / btile_height()+(viewy2 - viewy1)/btile_height()+1 < y) return;
      int cx1, cy1, cx2, cy2;
      screen->GetClip(cx1, cy1, cx2, cy2);
      screen->SetClip(viewx1, viewy1, viewx2 + 1, viewy2 + 1);
      im->put_image(screen, (x - xo / btile_width())*btile_width()+viewx1 - xo % btile_width(),
            (y - yo / btile_height())*btile_height()+viewy1 - yo % btile_height(), 0);
      screen->SetClip(cx1, cy1, cx2, cy2);
    }
  }
}

int need_delay = 1;

void Game::dev_scroll()
{
  need_delay = 0;
  if(dev)
  {
    int xmargin, ymargin;
    if(xres > 400)
    {
      xmargin = 20;
      ymargin = 10;
    }
    else
    {
      xmargin = 10;
      ymargin = 5;
    }

    int xs, ys;
    if(mousex < xmargin &&  dev_cont->ok_to_scroll()) xs = -18;
    else if(mousex>(screen->Size().x-xmargin) &&  dev_cont->ok_to_scroll()) xs = 18;
    else if(wm->key_pressed(JK_LEFT) && !last_input && !dev_cont->need_arrows())
      xs = -18;
    else if(wm->key_pressed(JK_RIGHT) && !last_input && !dev_cont->need_arrows())
      xs = 18;
    else xs = 0;


    if(mousey < ymargin && dev_cont->ok_to_scroll()) ys = -18;
    else if(mousey>(screen->Size().y-ymargin) &&  dev_cont->ok_to_scroll()) ys = 18;
    else if(wm->key_pressed(JK_UP) && !last_input)
      ys = -18;
    else if(wm->key_pressed(JK_DOWN) && !last_input)
      ys = 18;
    else ys = 0;


    if(xs || ys)
    {
      need_delay = 1;
      if(dev & MAP_MODE)
      {
    map_xoff += xs / 2;
    map_yoff += ys / 2;
    if(map_xoff < 0) map_xoff = 0;
    if(map_yoff < 0) map_yoff = 0;
      }
      else
      {
    for(view *v = first_view; v; v = v->next)
    {
      if(xs >= 0 || v->xoff()>0)
        v->pan_x += xs;
      if(ys >= 0 || v->yoff()>0)
        v->pan_y += ys;
    }
      }
      refresh = 1;
    }
  }
}

void remap_area(image *screen, int x1, int y1, int x2, int y2, uint8_t *remap)
{
    screen->Lock();

    uint8_t *sl = (uint8_t *)screen->scan_line(y1) + x1;
    int step = screen->Size().x - (x2 - x1 + 1);

    for(int y = y1; y <= y2; y++)
    {
        for(int x = x1; x <= x2; x++)
        {
            uint8_t c = *sl;
            *(sl++) = remap[c];
        }
        sl += step;
    }
    screen->Unlock();
}

static void post_render()
{
  if(DEFINEDP(l_post_render->GetFunction()))
  {
    screen->dirt_off();
    clear_tmp();
    l_post_render->EvalFunction(NULL);
    clear_tmp();
    screen->dirt_on();
  }
}

static void draw_checkpoint_lighting(view *v, int world_x, int world_y, int scale, int frame)
{
    if (!game_settings.checkpoint_feedback || !v->focus || !v->local_player() ||
        !player_list || player_list->next || (dev & (EDIT_MODE | MAP_MODE)) ||
        !(dev & DRAW_PEOPLE_LAYER))
        return;

    const uint32_t tick = current_level->tick_counter();
    const vec2i player = v->focus->render_position(frame, tick);
    const float phase = (tick % 36 + float(frame) / frames_per_tick) * 6.2831853f / 36.f;
    const float pulse = 0.94f + 0.06f * std::cos(phase);
    screen->SetClip(v->cx1, v->cy1, v->cx2 + 1, v->cy2 + 1);
    for (auto *console = current_level->first_active_object(); console; console = console->next_active) {
        if (strcmp(object_names[console->otype], "RESTART_POSITION")) continue;
        const vec2i position = console->render_position(frame, tick);
        const float dx = (position.x - player.x) / 144.f;
        const float dy = (position.y - player.y) / 96.f;
        const float proximity = 1.f - dx * dx - dy * dy;
        if (proximity <= 0.f) continue;

        int32_t left, top, right, bottom;
        console->picture_space(left, top, right, bottom);
        const vec2i movement = position - vec2i(console->x, console->y);
        const vec2i screen_position = vec2i(v->cx1, v->cy1) +
            (vec2i(left - world_x, top - world_y) + movement) * scale;
        const vec2i size((right - left + 1) * scale, (bottom - top + 1) * scale);
        const float intensity = proximity * proximity * (3.f - 2.f * proximity) * pulse;
        draw_save_console_glow(*screen, *pal, screen_position, size, intensity);
    }
}

static void draw_interaction_prompt(view *v)
{
    if (!v->focus || !v->focus->hp() || !v->local_player() || !player_list || player_list->next ||
        (dev & (EDIT_MODE | MAP_MODE)) || !(dev & DRAW_PEOPLE_LAYER) ||
        demo_man.current_state() != demo_manager::NORMAL)
        return;

    int32_t left, top, right, bottom;
    v->focus->picture_space(left, top, right, bottom);
    const char *action = nullptr;
    int nearest = std::numeric_limits<int>::max();
    for (auto *object = current_level->first_active_object(); object; object = object->next_active) {
        const int dx = std::abs(object->x - v->focus->x);
        const int dy = std::abs(object->y - v->focus->y);
        if (dx + dy >= nearest) continue;
        const char *name = object_names[object->otype];
        const auto state = object->aistate();
        const char *candidate = nullptr;
        if (!strcmp(name, "SWITCH") || !strcmp(name, "SWITCH_DELAY") || !strcmp(name, "SWITCH_ONCE")) {
            if (dx < 20 && dy < 30 && (state == 0 || (state == 2 && !strcmp(name, "SWITCH"))))
                candidate = "Use switch";
        } else {
            const int links = object->total_objects();
            if (!strcmp(name, "RESTART_POSITION") && state <= 1)
                candidate = "Save game";
            else if ((!strcmp(name, "SMART_PLAT_SMALL") || !strcmp(name, "SMART_PLAT_BIG") ||
                      !strcmp(name, "SMART_PLAT_RED")) && state == 0 && object->aitype() <= 1 &&
                     (links == 2 || (links == 3 && object->get_object(2)->aistate())) &&
                     !object->get_object(object->aitype())->aistate())
                candidate = "Ride lift";
            if (candidate) {
                int32_t x1, y1, x2, y2;
                object->picture_space(x1, y1, x2, y2);
                if (x1 > right || x2 < left || y1 > bottom || y2 < top) candidate = nullptr;
            }
        }
        if (candidate) {
            nearest = dx + dy;
            action = candidate;
        }
    }
    if (!action) return;

    const char *button = controller_use_hint();
    const std::string key = button ? button : control_key_name(control_binding(ControlAction::Use));
    const std::string message = "[" + key + "] " + action;
    auto *font = wm->font();
    const int width = message.size() * font->width();
    const int x = (v->cx1 + v->cx2 - width) / 2;
    const int y = v->cy2 - font->height() - 8;
    screen->SetClip(v->cx1, v->cy1, v->cx2 + 1, v->cy2 + 1);
    screen->bar(x - 6, y - 3, x + width + 5, y + font->height() + 2, pal->find_closest(12, 17, 22));
    font->put_string(screen, x, y, message.c_str(), pal->find_closest(241, 219, 153));
}

void Game::draw_map(view *v, int frame)
{
  const bool in_between = frame < frames_per_tick;
  backtile *bt;
  int x1, y1, x2, y2, x, y, xo, yo, nxoff, nyoff;
  int cx1, cy1, cx2, cy2;
  screen->GetClip(cx1, cy1, cx2, cy2);

  if(!current_level || state == MENU_STATE)
  {
    if (state == SCENE_STATE && title_screen >= 0) {
        screen->SetClip(v->cx1, v->cy1, v->cx2 + 1, v->cy2 + 1);
        image *tit = cache.img(title_screen);
        tit->put_image(screen, (screen->Size().x - tit->Size().x) / 2,
                       (screen->Size().y - tit->Size().y) / 2);
        screen->SetClip(cx1, cy1, cx2, cy2);
    } else {
        draw_title_screen();
    }
    wm->flush_screen();
    return;
  }

  refresh = 0;


  // save the dirty rect routines some work by markinging evrything in the
  // view area dirty alreadt

  if(small_render)
    screen->AddDirty(v->cx1, v->cy1, (v->cx2 - v->cx1 + 1)*2 + v->cx1 + 1, v->cy1+(v->cy2 - v->cy1 + 1)*2 + 1);
  else
    screen->AddDirty(v->cx1, v->cy1, v->cx2 + 1, v->cy2 + 1);

  if(v->draw_solid != -1)      // fill the screen and exit..
  {
    if (in_between) return;
    int c = v->draw_solid;
    screen->Lock();
    for(int y = v->cy1; y <= v->cy2; y++)
      memset(screen->scan_line(y)+v->cx1, c, v->cx2 - v->cx1 + 1);
    screen->Unlock();
    v->draw_solid = -1;
    return;
  }

  int32_t old_cx1 = 0, old_cy1 = 0, old_cx2 = 0, old_cy2 = 0;   // if we do a small render, we need to restore these
  image *old_screen = NULL;
  if(small_render && (dev & DRAW_LIGHTS))  // cannot do this if we skip lighting
  {
    old_cx1 = v->cx1;
    old_cy1 = v->cy1;
    old_cx2 = v->cx2;
    old_cy2 = v->cy2;

    v->cx1 = 0;
    v->cy1 = 0;
    v->cx2 = small_render->Size().x-1;
    v->cy2 = small_render->Size().y-1;

    old_screen = screen;
    screen = small_render;
  } else
    screen->dirt_off();


  between_ticks = in_between;
  const vec2i camera(v->last_x, v->last_y);
  if (in_between && v->focus) {
    const vec2i sample = interpolate_motion(vec2i(v->last_last_x, v->last_last_y), camera, frame);
    // Use one camera origin for tiles, sprites, scripted draws, and lighting.
    v->last_x = sample.x;
    v->last_y = sample.y;
  }
  const int32_t xoff = v->xoff(), yoff = v->yoff();

//  if(xoff > max_xoff) xoff = max_xoff;
//  if(yoff > max_yoff) yoff = max_yoff;

  current_vxadd = xoff - v->cx1;
  current_vyadd = yoff - v->cy1;

  screen->SetClip(v->cx1, v->cy1, v->cx2 + 1, v->cy2 + 1);

  clear_projectile_lights();
  const bool foreground_contrast = game_settings.enhanced_lighting && light_detail != POOR_DETAIL &&
      (dev & DRAW_LIGHTS) && (dev & DRAW_FG_LAYER) && (dev & DRAW_BG_LAYER) &&
      !(dev & (MAP_MODE | EDIT_MODE));
  std::vector<LavaSurface> lava;
  std::unique_ptr<image> terrain_mask;
  if (game_settings.enhanced_lighting && (dev & DRAW_LIGHTS) &&
      (dev & DRAW_PEOPLE_LAYER) && !(dev & MAP_MODE)) {
    const auto tick = current_level->tick_counter();
    for (auto *object = current_level->first_active_object(); object; object = object->next_active) {
      const vec2i sample = object->render_position(frame, tick);
      object->collect_projectile_light(sample.x, sample.y);
      if (foreground_contrast && !strcmp(object_names[object->otype], "LAVA")) {
        int32_t left, top, right, bottom;
        object->picture_space(left, top, right, bottom);
        const int inset = (right - left + 1) / 5;
        // The luminous top row of the slanted sprite is shifted from its base.
        if (object->direction > 0) left += inset;
        else right -= inset;
        const vec2i shift = sample - vec2i(object->x + current_vxadd, object->y + current_vyadd);
        const uint32_t seed = uint32_t(object->x) * 73856093u ^ uint32_t(object->y) * 19349663u;
        lava.push_back({left + shift.x, right + shift.x, top + shift.y + 2, seed});
      }
    }
  }
  if (foreground_contrast || has_projectile_lights()) {
    terrain_mask = std::make_unique<image>(screen->Size(), nullptr, 1);
    terrain_mask->dirt_off();
    terrain_mask->SetClip(v->cx1, v->cy1, v->cx2 + 1, v->cy2 + 1);
    terrain_mask->clear(dev & DRAW_BG_LAYER ? BackgroundReflection : NoReflection);
  }

  nxoff = xoff * bg_xmul / bg_xdiv;
  nyoff = yoff * bg_ymul / bg_ydiv;


  x1 = nxoff / btile_width(); y1 = nyoff / btile_height();
  x2 = x1+(v->cx2 - v->cx1 + btile_width())/btile_width();
  y2 = y1+(v->cy2 - v->cy1 + btile_height())/btile_height();


  xo = v->cx1 - nxoff % btile_width();
  yo = v->cy1 - nyoff % btile_height();

  int xinc, yinc, draw_x, draw_y;


  if(!(dev & MAP_MODE) && (dev & DRAW_BG_LAYER))
  {
    xinc = btile_width();
    yinc = btile_height();

    int bh = current_level->background_height(), bw = current_level->background_width();
    uint16_t *bl;
    for(draw_y = yo, y = y1; y <= y2; y++, draw_y += yinc)
    {
      if(y >= bh)
        bl = NULL;
      else
        bl = current_level->get_bgline(y)+x1;

      for(x = x1, draw_x = xo; x <= x2; x++, draw_x += xinc)
      {
    if(x < bw && y < bh)
    {
          bt = get_bg(*bl);
      bl++;
    }
    else bt = get_bg(0);

        bt->im->put_image(screen, draw_x, draw_y);
//        if(!(dev & EDIT_MODE) && bt->next)
//      current_level->put_bg(x, y, bt->next);
      }
    }
  }

//  if(!(dev & EDIT_MODE))
//    server_check();

  uint8_t rescan = 0;

    int fw, fh;

    if(dev & MAP_MODE)
    {
      fw = AUTOTILE_WIDTH;
      fh = AUTOTILE_HEIGHT;
      if(dev & EDIT_MODE)
      {
    x1 = map_xoff;
    y1 = map_yoff;
      } else
      {
    if(v->focus)
    {
      x1 = v->focus->x / ftile_width()-(v->cx2 - v->cx1)/fw / 2;
      y1 = v->focus->y / ftile_height()-(v->cy2 - v->cy1)/fh / 2;
    } else x1 = y1 = 0;
      }
      if(x1 > 0)
        xo = v->cx1-((v->focus->x * fw / ftile_width()) %fw);
      else xo = v->cx1;
      if(y1 > 0)
        yo = v->cy1-((v->focus->y * fh / ftile_height()) %fh);
      else yo = v->cy1;
    } else
    {
      fw = ftile_width();
      fh = ftile_height();
      x1=(xoff)/fw; y1=(yoff)/fh;
      xo = v->cx1 - xoff % fw;
      yo = v->cy1 - yoff % fh;

    }
    if(x1 < 0) x1 = 0;
    if(y1 < 0) y1 = 0;

    x2 = x1+(v->cx2 - v->cx1 + fw)/fw;
    y2 = y1+(v->cy2 - v->cy1 + fh)/fh;
    if(x2 >= current_level->foreground_width())
      x2 = current_level->foreground_width()-1;
    if(y2 >= current_level->foreground_height())
      y2 = current_level->foreground_height()-1;


    xinc = fw;
    yinc = fh;

  if(dev & DRAW_FG_LAYER)
  {
    int ncx1, ncy1, ncx2, ncy2;
    screen->GetClip(ncx1, ncy1, ncx2, ncy2);

    int scr_w = screen->Size().x;
    if(dev & MAP_MODE)
    {
      if(dev & EDIT_MODE)
        screen->clear(wm->bright_color());
      else
        screen->clear(wm->black());
      screen->Lock();
      for(y = y1, draw_y = yo; y <= y2; y++, draw_y += yinc)
      {
    if(!(draw_y < ncy1 ||draw_y + yinc > ncy2))
    {
      uint16_t *cl = current_level->get_fgline(y)+x1;
      uint8_t *sl1 = screen->scan_line(draw_y)+xo;
      for(x = x1, draw_x = xo; x <= x2; x++, cl++, sl1 += xinc, draw_x += xinc)
      {
        if(!(draw_x < ncx1 || draw_x + xinc > ncx2))
        {
          int fort_num;
//          if(*cl & 0x8000 || (dev & EDIT_MODE))
            fort_num = fgvalue(*cl);
//          else fort_num = 0;

          uint8_t *sl2 = get_fg(fort_num)->micro_image->scan_line(0);
          uint8_t *sl3 = sl1;
          memcpy(sl3, sl2, AUTOTILE_WIDTH); sl2 += AUTOTILE_WIDTH; sl3 += scr_w;
          memcpy(sl3, sl2, AUTOTILE_WIDTH); sl2 += AUTOTILE_WIDTH; sl3 += scr_w;
          memcpy(sl3, sl2, AUTOTILE_WIDTH);
        }
      }
    }
      }
      screen->Unlock();

      if(dev & EDIT_MODE)
        current_level->draw_areas(v);
    } else
    {

      int fg_h = current_level->foreground_height(), fg_w = current_level->foreground_width();

      for(y = y1, draw_y = yo; y <= y2; y++, draw_y += yinc)
      {

    uint16_t *cl;
    if(y < fg_h)
      cl = current_level->get_fgline(y)+x1;
    else cl = NULL;

    for(x = x1, draw_x = xo; x <= x2; x++, draw_x += xinc, cl++)
    {
      if(x < fg_w && y < fg_h)
      {
        if(above_tile(*cl))
        rescan = 1;
        else
        {
          int fort_num = fgvalue(*cl);
          if(fort_num != BLACK)
          {
            const vec2i tile_position(draw_x, draw_y);
            auto *tile = get_fg(fort_num);
            tile->im->PutImage(screen, tile_position);
            if (terrain_mask) {
              const auto material = tile->points->tot ? WallReflection : DecorativeReflection;
              tile->im->PutColor(terrain_mask.get(), tile_position, material);
              if (foreground_contrast && !tile->damage)
                mark_walkable_edges(*terrain_mask, *tile->points, tile_position, tile->im->Size());
            }

          }
        }
      }
    }
      }
    }
  }

  if (terrain_mask) {
    if (foreground_contrast)
      enhance_foreground_contrast(*screen, *terrain_mask, *pal);
    draw_lava_lighting(*screen, *terrain_mask, *pal, lava, current_level->tick_counter(), true);
    reflect_projectile_lights(*screen, *terrain_mask, xoff, yoff, *pal, !(dev & EDIT_MODE));
  }

  int32_t ro = rand_on;
  if(dev & DRAW_PEOPLE_LAYER)
  {
    if(in_between && !(dev & MAP_MODE))
      current_level->interpolate_draw_objects(v, frame);
    else
      current_level->draw_objects(v);
  }

//  if(!(dev & EDIT_MODE))
//    server_check();

  if(!(dev & MAP_MODE))
  {

    draw_panims(v);

    if(dev & DRAW_FG_LAYER && rescan)
    {
      if (terrain_mask) terrain_mask->clear(NoReflection);
      for(y = y1, draw_y = yo; y <= y2; y++, draw_y += yinc)
      {
    uint16_t *cl = current_level->get_fgline(y)+x1;
    for(x = x1, draw_x = xo; x <= x2; x++, draw_x += xinc, cl++)
    {
      if(above_tile(*cl))
      {
        int fort_num = fgvalue(*cl);
        if(fort_num != BLACK)
        {
          if(dev & DRAW_BG_LAYER)
          get_fg(fort_num)->im->PutImage(screen, vec2i(draw_x, draw_y));
          else
          get_fg(fort_num)->im->PutFilled(screen, vec2i(draw_x, draw_y), 0);
          if (terrain_mask) {
            const vec2i tile_position(draw_x, draw_y);
            auto *tile = get_fg(fort_num);
            const auto material = tile->points->tot ? WallReflection : DecorativeReflection;
            tile->im->PutColor(terrain_mask.get(), tile_position, material);
            if (foreground_contrast && !tile->damage)
              mark_walkable_edges(*terrain_mask, *tile->points, tile_position, tile->im->Size());
          }

          if (dev & EDIT_MODE)
          {
        screen->line(draw_x, draw_y, draw_x + xinc, draw_y + yinc, wm->bright_color());
        screen->line(draw_x + xinc, draw_y, draw_x, draw_y + yinc, wm->bright_color());
          }
        }
      }
    }
      }
    }


    if (terrain_mask && (dev & DRAW_FG_LAYER) && rescan) {
      if (foreground_contrast)
        enhance_foreground_contrast(*screen, *terrain_mask, *pal);
      draw_lava_lighting(*screen, *terrain_mask, *pal, lava, current_level->tick_counter(), false);
      reflect_projectile_lights(*screen, *terrain_mask, xoff, yoff, *pal);
    }

    if(dev & DRAW_FG_BOUND_LAYER)
    {
      int b = wm->bright_color();
      int fg_h = current_level->foreground_height(), fg_w = current_level->foreground_width();

      for(y = y1, draw_y = yo; y <= y2; y++, draw_y += yinc)
      {
    uint16_t *cl;
    if(y < fg_h)
      cl = current_level->get_fgline(y)+x1;
    else cl = NULL;
    for(x = x1, draw_x = xo; x <= x2; x++, draw_x += xinc, cl++)
    {
      if(x < fg_w && y < fg_h)
      {
        int fort_num = fgvalue(*cl);
        if(fort_num != BLACK)
        {
          point_list *p = get_fg(fort_num)->points;
          uint8_t *d = p->data;
          if(p->tot)
          {
        for(int i = 1; i < p->tot; i++)
        {
          d += 2;
          screen->line(draw_x+*(d - 2), draw_y+*(d - 1), draw_x+*d, draw_y+*(d + 1), b);
        }
        screen->line(draw_x+*d, draw_y+*(d - 1), draw_x + p->data[0], draw_y + p->data[1], b);
          }
        }
      }
    }
      }
    }

//    if(!(dev & EDIT_MODE))
//      server_check();

    if(dev & DRAW_HELP_LAYER)
    {
      if(help_text_frames >= 0)
      {
    int color;

    if(help_text_frames < 10)
        color = 2;
    else
        color = 2+(help_text_frames - 10);

    int x1 = v->cx1, y1 = v->cy1, x2 = v->cx2, y2 = v->cy1 + wm->font()->height()+10;

    remap_area(screen, x1, y1, x2, y2, white_light + 40 * 256);
    screen->bar(x1, y1, x2, y1, color);
    screen->bar(x1, y2, x2, y2, color);

    wm->font()->put_string(screen, x1 + 5, y1 + 5,
                   help_text, color);
    if (!in_between)
      help_text_frames = color > 30 ? -1 : help_text_frames + 1;

      }
    }

    if(dev_cont)
    dev_cont->dev_draw(v);
    if(cache.in_use())
    cache.img(vmm_image)->put_image(screen, v->cx1, v->cy2 - cache.img(vmm_image)->Size().y+1);

    if(dev & DRAW_LIGHTS)
    {
      const bool enhanced = game_settings.enhanced_lighting;
      if (enhanced)
        enhanced_light_screen(screen, xoff, yoff, white_light, v->ambient, *pal, current_level->tick_counter());
      if(small_render)
      {
    double_light_screen(screen, xoff, yoff, white_light, enhanced ? 63 : v->ambient, old_screen, old_cx1, old_cy1);

    v->cx1 = old_cx1;
    v->cy1 = old_cy1;
    v->cx2 = old_cx2;
    v->cy2 = old_cy2;
    screen = old_screen;
      } else
      {
    screen->dirt_on();
    if (!enhanced) light_screen(screen, xoff, yoff, white_light, v->ambient);
      }

    } else
      screen->dirt_on();



  }  else
    screen->dirt_on();

  rand_on = ro;                // restore random start in case in draw funs moved it
                               // ... not every machine will draw the same thing

  post_render();
  if (state == RUN_STATE) {
    draw_checkpoint_lighting(v, xoff, yoff, old_screen ? 2 : 1, frame);
    if (!in_between && v == player_list && v->focus && v->local_player() && !(dev & (EDIT_MODE | MAP_MODE))) {
      const vec2i top_left(v->cx1, v->cy1);
      const auto bottom_right = old_screen ? top_left + small_render->Size() * 2 : vec2i(v->cx2 + 1, v->cy2 + 1);
      current_level->capture_save_thumbnail(*screen, top_left, bottom_right);
    }
    draw_interaction_prompt(v);
  }

  screen->SetClip(cx1, cy1, cx2, cy2);

  if(profiling())
    profile_update();

  if (in_between) {
    v->last_x = camera.x;
    v->last_y = camera.y;
  }
  between_ticks = false;
}

void Game::put_fg(int x, int y, int type)
{
  if(current_level->get_fg(x, y)!=type)
  {
    current_level->put_fg(x, y, type);
    for(view *f = first_view; f; f = f->next)
      if(f->drawable())
        draw_map(f);
  }
}

void Game::put_bg(int x, int y, int type)
{
  if(current_level->get_bg(x, y)!=type)
  {
    current_level->put_bg(x, y, type);
    for(view *f = first_view; f; f = f->next)
      if(f->drawable())
        draw_map(f);
  }
}

int Game::in_area(event &ev, int x1, int y1, int x2, int y2)
{
  return (last_demo_mx >= x1 && last_demo_mx <= x2 &&
      last_demo_my >= y1 && last_demo_my <= y2);
}

void Game::request_level_load(char *name)
{
  strcpy(req_name, name);
}

extern int start_doubled;

template<int N> static void Fade(image *im, int steps, bool enlarge = false)
{
    /* 25ms per step */
    float const duration = 25.f;

    palette *old_pal = pal->copy();

    if (im)
    {
        int left, top, right, bottom;
        screen->GetClip(left, top, right, bottom);
        screen->SetClip(0, 0, screen->Size().x, screen->Size().y);
        platform_end_menu_text();
        screen->clear();
        draw_fitted_art(*im, screen->Size().y, enlarge);
        screen->SetClip(left, top, right, bottom);
    }

    for (Timer total; total.PollMs() < duration * steps; )
    {
        Timer frame;
        uint8_t *sl1 = (uint8_t *)pal->addr();
        uint8_t *sl2 = (uint8_t *)old_pal->addr();
        int i = (int)(total.PollMs() / duration);
        int v = (N ? i + 1 : steps - i) * 256 / steps;

        for (int j = 0; j < 3 * 256; j++)
            *sl1++ = (int)*sl2++ * v / 256;

        pal->load();
        wm->flush_screen();
        frame.WaitMs(duration);
    }

    if (N == 0)
    {
        screen->clear();
        wm->flush_screen();
        old_pal->load();
    }
    delete pal;
    pal = old_pal;
}

void fade_in(image *im, int steps)
{
    Fade<1>(im, steps);
}

void fade_out(int steps)
{
    Fade<0>(NULL, steps);
}

int text_draw(int y, int x1, int y1, int x2, int y2, char const *buf, JCFont *font, uint8_t *cmap, char color);

void do_title()
{
    if(cdc_logo == -1)
        return;

    if(sound_avail & MUSIC_INITIALIZED)
    {
        if(current_song)
        {
            current_song->stop();
            delete current_song;
        }
        current_song = new song("music/intro.hmi");
        current_song->play(music_volume);
    }

    void *logo_snd = LSymbol::FindOrCreate("LOGO_SND")->GetValue();

    if(DEFINEDP(logo_snd) && (sound_avail & SFX_INITIALIZED))
        cache.sfx(lnumber_value(logo_snd))->play(sfx_volume);

    // This must be a dynamic allocated image because if it
    // is not and the window gets closed during do_title, then
    // exit() will try to delete (through the desctructor of
    // image_list in image.cpp) the image on the stack -> boom.
    image *blank = new image(vec2i(2, 2));
    blank->clear();
    wm->set_mouse_shape(blank->copy(), 0, 0); // hide mouse
    delete blank;
    Fade<1>(cache.img(cdc_logo), 32, true);
    Timer tmp; tmp.WaitMs(400);
    fade_out(32);

    void *space_snd = LSymbol::FindOrCreate("SPACE_SND")->GetValue();
    char *str = lstring_value(LSymbol::FindOrCreate("plot_start")->Eval());

    bFILE *fp = open_file("art/smoke.spe", "rb");
    if(!fp->open_failure())
    {
        spec_directory sd(fp);
        palette *old_pal = pal;
        pal = new palette(sd.find(SPEC_PALETTE), fp);
        pal->shift(1);

        image *gray = new image(fp, sd.find("gray_pict"));
        image *smoke[5];

        char nm[20];
        for (int i = 0; i < 5; i++)
        {
            sprintf(nm, "smoke%04d.pcx", i + 1);
            smoke[i] = new image(fp, sd.find(nm));
        }

        screen->clear();
        pal->load();

        int dx = (xres + 1) / 2 - gray->Size().x / 2, dy = (yres + 1) / 2 - gray->Size().y / 2;
        gray->put_image(screen, dx, dy);
        smoke[0]->put_image(screen, dx + 24, dy + 5);

        fade_in(NULL, 16);
        uint8_t cmap[32];
        for(int i = 0; i < 32; i++)
        cmap[i] = pal->find_closest(i * 256 / 32, i * 256 / 32, i * 256 / 32);

        event ev;
        ev.type = EV_SPURIOUS;
        Timer total;

        while (ev.type != EV_KEY && ev.type != EV_MOUSE_BUTTON)
        {
            Timer frame;

            // 120 ms per step
            int i = (int)(total.PollMs() / 120.f);
            if (i >= 400)
                break;

            gray->put_image(screen, dx, dy);
            smoke[i % 5]->put_image(screen, dx + 24, dy + 5);
            text_draw(205 - i, dx + 15, dy, dx + 320 - 15, dy + 199, str, wm->font(), cmap, wm->bright_color());
            wm->flush_screen();
            time_marker now;

            while(wm->event_waiting() && ev.type != EV_KEY)
                wm->get_event(ev);

            if((i % 5) == 0 && DEFINEDP(space_snd) && (sound_avail & SFX_INITIALIZED))
                cache.sfx(lnumber_value(space_snd))->play(sfx_volume * 90 / 127);

            frame.WaitMs(25.f);
            frame.GetMs();
        }

        the_game->reset_keymap();

        fade_out(16);

        for (int i = 0; i < 5; i++)
            delete smoke[i];
        delete gray;
        delete pal;
        pal = old_pal;
    }
    delete fp;

    if(title_screen >= 0)
        Fade<1>(cache.img(title_screen), 32, true);
}

extern int start_edit;

void Game::request_end()
{
  req_end = 1;
}

Game::Game(int argc, char **argv)
{
  state = START_STATE; // event handling is active during title/gamma initialization
  int i;
  req_name[0]=0;
  bg_xmul = bg_ymul = 1;
  bg_xdiv = bg_ydiv = 8;
  last_input = NULL;
  current_automap = NULL;
  current_level = NULL;
  refresh = 1;
  the_game = this;
  top_menu = joy_win = NULL;
  old_view = first_view = NULL;
  nplayers = 1;

  help_text_frames = 0;
  strcpy(help_text, "");


  for(i = 1; i < argc; i++)
    if(!strcmp(argv[i], "-no_delay"))
    {
      no_delay = 1;
      dprintf("Frame delay off (-nodelay)\n");
    }


  image_init();
  zoom = 15;
  no_delay = 0;

  if(get_option("-use_joy"))
  {
    has_joystick = joy_init(argc, argv);
    dprintf("Joystick : ");
    if(has_joystick) dprintf("detected\n");
    else dprintf("not detected\n");
  }
  else has_joystick = 0;

    // Clean up that old crap
    char *fastpath = (char *)malloc(strlen(get_save_filename_prefix()) + 13);
    sprintf(fastpath, "%sfastload.dat", get_save_filename_prefix());
    unlink(fastpath);
    free(fastpath);

//    ProfilerInit(collectDetailed, bestTimeBase, 2000, 200); //prof
    load_data(argc, argv);
    const char *cue_sounds[] = {"SWISH", "PLAT_A_SND", "PLAT_D_SND", "ASCREAM_SND"};
    for (size_t i = 0; i < cue_sound_ids.size(); ++i) {
        auto *symbol = LSymbol::Find(cue_sounds[i]);
        cue_sound_ids[i] = symbol && item_type(symbol->GetValue()) == L_NUMBER
                          ? lnumber_value(symbol->GetValue()) : -1;
    }
    if (game_settings.auto_equip_pickups >= 0)
        LSymbol::FindOrCreate("change_on_pickup")->SetValue(game_settings.auto_equip_pickups ? true_symbol : nullptr);
    if (game_settings.difficulty >= 0 && (!main_net_cfg ||
        (main_net_cfg->state != net_configuration::SERVER && main_net_cfg->state != net_configuration::CLIENT))) {
        LSymbol *levels[] = {l_easy, l_medium, l_hard, l_extreme};
        l_difficulty->SetValue(levels[game_settings.difficulty]);
    }
//    ProfilerDump("\pabuse.prof");  //prof
//    ProfilerTerm();

  get_key_bindings();

  reset_keymap();                   // we think all the keys are up right now
  finished = false;

  calc_light_table(pal);

#if !defined __CELLOS_LV2__
  if(current_level == NULL && net_start())  // if we joined a net game get level from server
  {
    if(!request_server_entry())
    {
      exit(0);
    }
    net_reload();
//    load_level(NET_STARTFILE);
  }
#endif

  set_mode(19, argc, argv);
  if(get_option("-2") && (xres < 639 || yres < 399))
  {
    close_graphics();
    fprintf(stderr, "Resolution must be > 640x400 to use -2 option\n");
    exit(0);
  }
  pal->load();

  recalc_local_view_space();   // now that we know what size the screen is...

  dark_color = get_color(cache.img(window_colors)->Pixel(vec2i(2, 0)));
  bright_color = get_color(cache.img(window_colors)->Pixel(vec2i(0, 0)));
  med_color = get_color(cache.img(window_colors)->Pixel(vec2i(1, 0)));

  morph_dark_color = get_color(cache.img(window_colors)->Pixel(vec2i(2, 1)));
  morph_bright_color = get_color(cache.img(window_colors)->Pixel(vec2i(0, 1)));
  morph_med_color = get_color(cache.img(window_colors)->Pixel(vec2i(1, 1)));
  morph_sel_frame_color = pal->find_closest(255, 255, 0);
  light_connection_color = morph_sel_frame_color;

  if(NILP(symbol_value(l_default_font)))
  {
    printf("No font defined, set symbol default-font to an image name\n");
    exit(0);
  }
  int font_pict;
  if(big_font_pict != -1)
  {
    if(small_font_pict != -1)
    {
      if(xres/(start_doubled ? 2 : 1)>400)
      {
    font_pict = big_font_pict;
      }
      else font_pict = small_font_pict;
    } else font_pict = big_font_pict;
  } else font_pict = small_font_pict;

  if(console_font_pict == -1) console_font_pict = font_pict;
  game_font = new JCFont(cache.img(font_pict));

  console_font = new JCFont(cache.img(console_font_pict));

  wm = new WindowManager(screen, pal, bright_color,
                         med_color, dark_color, game_font);

  delete stat_man;  // move to a graphical status manager
  gui_status_manager *gstat = new gui_status_manager();
  gstat->set_window_title("status");
  stat_man = gstat;


  chat = new chat_console( console_font, 50, 6);

  if(!wm->has_mouse())
  {
    close_graphics();
    image_uninit();
    printf("No mouse driver detected, please rectify.\n");
    exit(0);
  }


  gamma_correct(pal);

#if defined __CELLOS_LV2__
  if(!start_edit)
    do_title();
#else
  if(main_net_cfg == NULL || (main_net_cfg->state != net_configuration::SERVER &&
                 main_net_cfg->state != net_configuration::CLIENT))
  {
    if(!start_edit && !net_start())
      do_title();
  } else if(main_net_cfg && main_net_cfg->state == net_configuration::SERVER)
  {
    the_game->load_level(level_file);
    start_running = 1;
  }
#endif


  dev|= DRAW_FG_LAYER | DRAW_BG_LAYER | DRAW_PEOPLE_LAYER | DRAW_HELP_LAYER | DRAW_LIGHTS | DRAW_LINKS;

  if(dev & EDIT_MODE)
    set_frame_size(0);
//  do_intro();
  state = START_STATE;         // first set the state to one that has windows


  if(start_running)
    set_state(RUN_STATE);
  else
  {
    draw_title_screen();
    set_state(MENU_STATE);   // then go to menu state so windows will turn off
  }
}

time_marker *led_last_time = NULL;
static float avg_ms = 1000.0f / 15, possible_ms = 1000.0f / 15;

void Game::toggle_delay()
{
    no_delay = !no_delay;
    show_help(symbol_str(no_delay ? "delay_off" : "delay_on"));
    avg_ms = possible_ms = 1000.0f / 15;
}

void Game::show_time()
{
    if (!first_view || !fps_on)
        return;

    char str[16];
    sprintf(str, "%ld", (long)(10000.0f / avg_ms));
    console_font->put_string(screen, first_view->cx1, first_view->cy1, str);

    sprintf(str, "%d", total_active);
    console_font->put_string(screen, first_view->cx1, first_view->cy1 + 10, str);
}

void Game::update_screen(int frame)
{
  const Uint64 render_start = SDL_GetTicksNS();
  if(state == HELP_STATE)
    draw_help();
  else if(current_level)
  {
    if(!(dev & EDIT_MODE) || refresh)
    {
      view *f = first_view;
      current_level->clear_active_list();
      for(; f; f = f->next)
      {
    if(f->focus)
    {
      int w, h;

      w=(f->cx2 - f->cx1 + 1);
      h=(f->cy2 - f->cy1 + 1);

      const int drawn = current_level->add_drawables(f->xoff()-w / 4, f->yoff()-h / 4,
                             f->xoff()+w + w / 4, f->yoff()+h + h / 4);
      if (frame == frames_per_tick) total_active += drawn;

    }
      }

      for(f = first_view; f; f = f->next)
      {
        if(f->drawable())
    {
      if(frame == frames_per_tick && interpolate_draw && (dev & EDIT_MODE))
      {
            draw_map(f, frames_per_tick / 2);
        sbar.redraw(screen);
        wm->flush_screen();
      }
          draw_map(f, frame);
    }
      }
      if(current_automap)
      current_automap->draw();
    }
    if(state == PAUSE_STATE)
    {
      for(view *f = first_view; f; f = f->next)
        cache.img(pause_image)->put_image(screen, (f->cx1 + f->cx2)/2 - cache.img(pause_image)->Size().x/2,
                   f->cy1 + 5, 1);
    }

    show_time();
    // Composite the complete HUD after the world, including interpolated frames.
    if (playing_state(state))
      sbar.draw_update();
    for (view *player = player_list; player; player = player->next)
      draw_gameplay_feedback(*player);
  }

  if(state == RUN_STATE && cache.prof_is_on())
    cache.prof_poll_end();

  wm->flush_screen();
  last_render_ms = (SDL_GetTicksNS() - render_start) / 1000000.0f;
}

void Game::do_intro()
{

}

// FIXME: refactor this to use the Lol Engine main fixed-framerate loop?
int Game::calc_speed()
{
    static Timer frame_timer;
    static int first = 1;

    if (first)
    {
        first = 0;
        return 0;
    }

    // Find average fps for last 10 frames
    float deltams = Max(1.0f, frame_timer.PollMs());

    avg_ms = 0.9f * avg_ms + 0.1f * deltams;
    possible_ms = 0.9f * possible_ms + 0.1f * deltams;

    if (avg_ms < 1000.0f / 14)
        massive_frame_panic = Max(0, Min(20, massive_frame_panic - 1));

    int ret = 0;

    if (state == RUN_STATE && current_level && !(dev & (EDIT_MODE | MAP_MODE)) &&
        need_delay && !no_delay && !req_name[0] && !req_end && !done() &&
        !cache.prof_is_on() && !DEFINEDP(l_post_render->GetFunction()))
    {
        // Spend the original tick wait on evenly spaced renders. Skip missed slots
        // instead of drawing several frames in a burst on a busy machine.
        for (int frame = 1; frame < frames_per_tick; ++frame) {
            const float present_at = gameplay_tick_ms * frame / frames_per_tick;
            const float elapsed = frame_timer.PollMs();
            if (elapsed >= present_at) continue;
            const float start_at = Max(elapsed, present_at - last_render_ms);
            if (start_at + last_render_ms + 1.0f >= gameplay_tick_ms) break;
            frame_timer.WaitMs(start_at);
            update_screen(frame);
        }
        frame_timer.WaitMs(gameplay_tick_ms);
        avg_ms += 0.1f * (frame_timer.PollMs() - deltams);
        frame_panic = 0;
    }
    else if (dev & EDIT_MODE)
    {
        // ECS - Added this case and the wait.  It's a cheap hack to ensure
        // that we don't exceed 30FPS in edit mode and hog the CPU.
        frame_timer.WaitMs(33);
    }
    else if (avg_ms < 1000.0f / 15 && need_delay)
    {
        frame_panic = 0;
        if (!no_delay)
        {
            frame_timer.WaitMs(1000.0f / 15);
            avg_ms -= 0.1f * deltams;
            avg_ms += 0.1f * 1000.0f / 15;
        }
    }
    else if (avg_ms > 1000.0f / 14)
    {
        if(avg_ms > 1000.0f / 10)
            massive_frame_panic++;
        frame_panic++;
        // All is lost, don't sleep during this frame
        ret = 1;
    }

    // Ignore our wait time, we're more interested in the frame time
    frame_timer.GetMs();
    return ret;
}

extern int start_edit;

void Game::get_input()
{
    bool pause_requested = false;
    event ev;
    idle_ticks++;
    while(event_waiting())
    {
        get_event(ev);
        if ((ev.type == EV_KEY || ev.type == EV_KEYRELEASE) && ev.key == JK_F11) {
            if (ev.type == EV_KEY && (!main_net_cfg || main_net_cfg->state == net_configuration::SINGLE_PLAYER)) {
                confirm_fullscreen_change();
                update_screen();
            }
            continue;
        }
        if (ev.type == EV_KEY && ev.key == JK_TAB && !ev.window && !pause_requested &&
            state == RUN_STATE && !start_edit && !(dev & (EDIT_MODE | MAP_MODE)) &&
            demo_man.current_state() == demo_manager::NORMAL && pause_map_available()) {
            show_pause_map(*current_level, *player_list->focus);
            update_screen();
            continue;
        }
        if (ev.type == EV_KEY && ev.key == JK_ESC && state == RUN_STATE && !(dev & EDIT_MODE))
            pause_requested = true;

        if(ev.type == EV_MOUSE_MOVE)
        {
            last_input = ev.window;
        }
        // don't process repeated keys in the main window, it will slow down the game to handle such
        // useless events. However in other windows it might be useful, such as in input windows
        // where you want to repeatedly scroll down...
        if(ev.type != EV_KEY || !key_down(ev.key) || ev.window || (dev & EDIT_MODE))
        {
#if !defined __CELLOS_LV2__
            if(ev.type == EV_KEY)
            {
                set_key_down(ev.key, 1);
                if(playing_state(state))
                {
                    if(ev.key < 256)
                    {
                        if(chat && chat->chat_event(ev))
                            base->packet.write_uint8(SCMD_CHAT_KEYPRESS);
                        else
                            base->packet.write_uint8(SCMD_KEYPRESS);
                    }
                    else
                        base->packet.write_uint8(SCMD_EXT_KEYPRESS);
                    base->packet.write_uint8(client_number());
                    if(ev.key > 256)
                        base->packet.write_uint8(ev.key - 256);
                    else
                        base->packet.write_uint8(ev.key);
                }
            }
            else if(ev.type == EV_KEYRELEASE)
            {
                set_key_down(ev.key, 0);
                if(playing_state(state))
                {
                    if(ev.key < 256)
                        base->packet.write_uint8(SCMD_KEYRELEASE);
                    else
                        base->packet.write_uint8(SCMD_EXT_KEYRELEASE);
                    base->packet.write_uint8(client_number());
                    if(ev.key > 255)
                        base->packet.write_uint8(ev.key - 256);
                    else
                        base->packet.write_uint8(ev.key);
                }
            }
#endif

            if((dev & EDIT_MODE) || start_edit || ev.type == EV_MESSAGE)
            {
                dev_cont->handle_event(ev);
            }

            view *v = first_view;
            for(; v; v = v->next)
            {
                if(v->local_player() && v->handle_event(ev))
                    ev.type = EV_SPURIOUS;       // if the event was used by the view, gobble it up
            }

            if(current_automap)
            {
                current_automap->handle_event(ev);
            }

            help_handle_event(ev);
            mousex = last_demo_mx;
            mousey = last_demo_my;

            if(ev.type == EV_MESSAGE)
            {
                switch (ev.message.id)
                {
                    case CALB_JOY:
                    {
                        if(!joy_win)
                        {
                            joy_win = wm->new_window(80, 50, -1, -1,
                                    new button(70, 9, JOY_OK, "OK",
                                    new info_field(0, 30, DEV_NULL,
                                    " Center joystick and\n"
                                    "press the fire button", NULL)),
                                    "Joystick");
                            set_state(JOY_CALB_STATE);
                        }
                    }
                    case TOP_MENU:
                    {
                        menu_select(ev);
                    } break;
                    case DEV_QUIT:
                    {
                        finished = true;
                    } break;
                }
            }
            else if(ev.type == EV_CLOSE_WINDOW && ev.window == top_menu)
            {
                wm->close_window(top_menu);
                top_menu = NULL;
            }

            switch(state)
            {
                case JOY_CALB_STATE:
                {
                    joy_calb(ev);
                } break;
                case INTRO_START_STATE:
                {
                    do_intro();
                    if(dev & EDIT_MODE)
                        set_state(RUN_STATE);
                    else
                        set_state(MENU_STATE);
                } break;
                case PAUSE_STATE:
                {
                    if(ev.type == EV_KEY && (ev.key == JK_SPACE || ev.key == JK_ENTER))
                    {
                        set_state(RUN_STATE);
                    }
                } break;
                case RUN_STATE:
                {
                    if(ev.window == NULL)
                    {
                        switch (ev.type)
                        {
                            case EV_KEY:
                            {
                                switch (ev.key)
                                {
                                    case 'm':
                                    {
                                        if(dev & MAP_MODE)
                                            dev -= MAP_MODE;
                                        else if((player_list && player_list->next) || dev & EDIT_MODE)
                                            dev |= MAP_MODE;

                                        if(!(dev & MAP_MODE))
                                        {
                                            if(dev_cont->tbw)
                                                dev_cont->toggle_toolbar();
                                            edit_mode = ID_DMODE_DRAW;
                                        }
                                        need_refresh();
                                    } break;
                                    case 'v':
                                    {
                                        wm->push_event(new event(DO_VOLUME, NULL));
                                    } break;
                                    case 'p':
                                    {
                                        if(!(dev & EDIT_MODE) && (!main_net_cfg ||
                                            (main_net_cfg->state != net_configuration::SERVER &&
                                            main_net_cfg->state != net_configuration::CLIENT)))
                                        {
                                            set_state(PAUSE_STATE);
                                        }
                                    } break;
                                    case 'S':
                                    {
                                        if(start_edit)
                                        {
                                            wm->push_event(new event(ID_LEVEL_SAVE, NULL));
                                        }
                                    } break;
                                    case JK_TAB:
                                        if(start_edit)
                                            toggle_edit_mode();
                                        need_refresh();
                                        break;
                                    case 'c':
                                    case 'C':
                                        if(chatting_enabled && (!(dev & EDIT_MODE) && chat))
                                            chat->toggle();
                                        break;
                                    case '9':
                                        dev = dev ^ PERFORMANCE_TEST_MODE;
                                        need_refresh();
                                        break;
                                }
                            } break;
                            case EV_RESIZE:
                            {
                                recalc_local_view_space();
                                draw();
                            } break;
                            case EV_REDRAW:
                            {
                                screen->AddDirty(ev.redraw.x1, ev.redraw.y1,
                                    ev.redraw.x2 + 1, ev.redraw.y2 + 1);
                            } break;
                            case EV_MESSAGE:
                            {
                                switch (ev.message.id)
                                {
                                    case RAISE_SFX:
                                    case LOWER_SFX:
                                    case RAISE_MUSIC:
                                    case LOWER_MUSIC:
                                    {
                                        if(ev.message.id == RAISE_SFX && sfx_volume != 127)
                                            sfx_volume = Min(127, sfx_volume + 16);
                                        if(ev.message.id == LOWER_SFX && sfx_volume != 0)
                                            sfx_volume = Max(sfx_volume - 16, 0);
                                        if(ev.message.id == RAISE_MUSIC && music_volume != 126)
                                        {
                                            music_volume = Min(music_volume + 16, 127);
                                            if(current_song && (sound_avail & MUSIC_INITIALIZED))
                                                current_song->set_volume(music_volume);
                                        }

                                        if(ev.message.id == LOWER_MUSIC && music_volume != 0)
                                        {
                                            music_volume = Max(music_volume - 16, 0);
                                            if(current_song && (sound_avail & MUSIC_INITIALIZED))
                                                current_song->set_volume(music_volume);
                                        }

                                        ((button *)ev.message.data)->push();
/*                                        volume_window->inm->redraw();
                                        draw_value(volume_window->screen, 2, 43,
                                                (volume_window->x2()-volume_window->x1()-1), 8, sfx_volume, 127);
                                        draw_value(volume_window->screen, 2, 94,
                                                (volume_window->x2()-volume_window->x1()-1), 8, music_volume, 127);
*/
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } break;
            }
        }
    }
    // Keep short Escape/Start taps even when their release arrives in the same frame.
    if (pause_requested && state == RUN_STATE) set_state(MENU_STATE);
}


void net_send(int force = 0)
{
#if !defined __CELLOS_LV2__
  if((!(dev & EDIT_MODE)) || force)
  {
    if(demo_man.state == demo_manager::PLAYING)
    {
      base->input_state = INPUT_PROCESSING;
    } else
    {



      if(!player_list->focus)
      {
    dprintf("Players have not been created\ncall create_players");
    exit(0);
      }


      view *p = player_list;
      for(; p; p = p->next)
        if(p->local_player())
      p->get_input();


      base->packet.write_uint8(SCMD_SYNC);
      base->packet.write_uint16(make_sync());

      if(base->join_list)
      base->packet.write_uint8(SCMD_RELOAD);

      //      printf("save tick %d, pk size=%d, rand_on=%d, sync=%d\n", current_level->tick_counter(),
      //         base->packet.packet_size(), rand_on, make_sync());
      send_local_request();
    }
  }
#endif
}

void net_receive()
{
#if !defined __CELLOS_LV2__
  if(!(dev & EDIT_MODE) && current_level)
  {
    uint8_t buf[PACKET_MAX_SIZE + 1];
    int size;

    if(demo_man.state == demo_manager::PLAYING)
    {
      if(!demo_man.get_packet(buf, size))
        size = 0;
      base->packet.packet_reset();
      base->mem_lock = 0;
    } else
    {
      size = get_inputs_from_server(buf);
      if(demo_man.state == demo_manager::RECORDING)
    demo_man.save_packet(buf, size);
    }

    process_packet_commands(buf, size);
  }
#endif
}

void Game::step()
{
  clear_tmp();
  if(current_level)
  {
    current_level->unactivate_all();
    total_active = 0;
    for(view *f = first_view; f; f = f->next)
    {
      if(f->focus)
      {
        f->update_scroll();
        int32_t left, top, right, bottom;
        f->activation_area(left, top, right, bottom);
        total_active += current_level->add_actives(left, top, right, bottom);
      }
    }
  }

  if(state == RUN_STATE)
  {
    if((dev & EDIT_MODE) || (main_net_cfg && (main_net_cfg->state == net_configuration::CLIENT ||
                         main_net_cfg->state == net_configuration::SERVER)))
      idle_ticks = 0;

    if(demo_man.current_state()==demo_manager::NORMAL && idle_ticks > 420 && demo_start)
    {
      idle_ticks = 0;
      set_state(MENU_STATE);
    }
    else if(!(dev & EDIT_MODE))               // if edit mode, then don't step anything
    {
      if(key_down(JK_ESC))
      {
    set_state(MENU_STATE);
    set_key_down(JK_ESC, 0);
      }
      ambient_ramp = 0;
      cache.prof_poll_start();
      if (demo_man.current_state() == demo_manager::NORMAL)
        current_level->count_play_tick();
      current_level->tick();
      if (player_list && !player_list->next && player_list->focus &&
          demo_man.current_state() == demo_manager::NORMAL)
        current_level->explore_map(*player_list->focus);
      sbar.step();
    } else
      dev_scroll();
  } else if(state == JOY_CALB_STATE)
  {
    event ev;
    joy_calb(ev);
  } else if(state == MENU_STATE)
    main_menu();

  if((key_down('x') || key_down(JK_F4))
      && (key_down(JK_ALT_L) || key_down(JK_ALT_R))
      && confirm_quit())
    finished = true;
}

extern void *current_demo;

Game::~Game()
{
  current_demo = NULL;
  if(first_view == player_list) first_view = NULL;
  while(player_list)
  {
    view *p = player_list;
    game_object *o = p->focus;
    player_list = player_list->next;
    delete p;
    o->set_controller(NULL);
    if(current_level && o)
      current_level->delete_object(o);
    else delete o;
  }

  if(current_level) { delete current_level; current_level = NULL; }

  if(first_view != player_list)
  {
    while(player_list)
    {
      view *p = player_list;
      player_list = player_list->next;
      delete p;
    }
  }

  while(first_view)
  {
    view *p = first_view;
    first_view = first_view->next;
    delete p;
  }

  player_list = NULL;

  if(old_view)
  {
    first_view = old_view;
    while(first_view)
    {
      view *p = first_view;
      first_view = first_view->next;
      delete p;
    }
  }
  old_view = NULL;

  int i = 0;
  for(; i < total_objects; i++)
  {
    free(object_names[i]);
    delete figures[i];
  }
  free_pframes();
  delete pal;
  free(object_names);
  free(figures);

  free(backtiles);
  free(foretiles);
  if(total_weapons)
    free(weapon_types);

  config_cleanup();
  delete color_table;
  delete wm;
  delete game_font;
  delete big_font;
  delete console_font;
  if(total_help_screens)
    free(help_screens);

  close_graphics();
  image_uninit();
}



void Game::draw(int scene_mode)
{
    screen->AddDirty(0, 0, xres + 1, yres + 1);

    screen->clear();

    if(scene_mode)
    {
        char const *helpstr = "ARROW KEYS CHANGE TEXT SPEED";
        wm->font()->put_string(screen, screen->Size().x/2-(wm->font()->width()*strlen(helpstr))/2 + 1,
            screen->Size().y-wm->font()->height()-5 + 1, helpstr, wm->dark_color());
        wm->font()->put_string(screen, screen->Size().x/2-(wm->font()->width()*strlen(helpstr))/2,
            screen->Size().y-wm->font()->height()-5, helpstr, wm->bright_color());
    }
/*    else
    {
        char *helpstr="PRESS h FOR HELP";
        wm->font()->put_string(screen, screen->Size().x-wm->font()->width()*strlen(helpstr)-5,
            screen->Size().y-wm->font()->height()-5, helpstr);
    }*/
/*    int dc = cache.img(window_colors)->pixel(0, 2);
    int mc = cache.img(window_colors)->pixel(1, 2);
    int bc = cache.img(window_colors)->pixel(2, 2);
    screen->line(0, 0, screen->Size().x-1, 0, dc);
    screen->line(0, 0, 0, screen->Size().y-1, dc);
    screen->line(0, screen->Size().y-1, screen->Size().x-1, screen->Size().y-1, bc);
    screen->line(screen->Size().x-1, 0, screen->Size().x-1, screen->Size().y-1, bc); */

    for(view *f = first_view; f; f = f->next)
        draw_map(f);

    sbar.redraw(screen);
}

int external_print = 0;

void start_sound(int argc, char **argv)
{
  sfx_volume = game_settings.sfx_volume;
  music_volume = game_settings.music_volume;

  sound_avail = sound_init(argc, argv);
}

void game_printer(char *st)
{
  if(dev_console && !external_print)
  {
    dev_console->put_string(st);
  }
  else fprintf(stderr, "%s", st);
}


void game_getter(char *st, int max)
{
  if(!max) return;
  max--;
  *st = 0;
  if(dev_console && !external_print)
  {
    dev_console->show();
    int t = 0;
    event ev;
    do
    {
      get_event(ev);
      if(ev.type == EV_KEY)
      {
    if(ev.key == JK_BACKSPACE)
    {
      if(t)
      {
        dev_console->print_f("%c", ev.key);
        t--;
        st--;
        *st = 0;
        max++;
      }
    } else if(ev.key>=' ' && ev.key<='~')
    {
      dev_console->print_f("%c", ev.key);
      *st = ev.key;
      t++;
      max--;
      st++;
      *st = 0;
    }
      }
      wm->flush_screen();
    } while(ev.type != EV_KEY || ev.key != JK_ENTER);
    dprintf("\n");
  }
  else
  {
    if(fgets(st, max, stdin))
    {
      if(*st)
        st[strlen(st)-1]=0;
    }
  }
}


void show_startup()
{
    dprintf("Abuse version %s\n", PACKAGE_VERSION);
}

char *get_line(int open_braces)
{
  char *line=(char *)malloc(1000);
  fgets(line, 1000, stdin);

  char prev=' ';
  for(char *s = line; *s && (prev!=' ' || *s!=';'); s++)
  {
    prev=*s;
    if(*s=='(') open_braces++;
    else if(*s==')') open_braces--;
  }
  if(open_braces < 0)
    fprintf(stderr, "\nToo many)'s\n");
  else if(open_braces > 0)
  {
    char *s2 = get_line(open_braces);
    line=(char *)realloc(line, strlen(line)+strlen(s2)+1);
    strcat(line, s2);
    free(s2);
  }
  return line;
}

void check_for_lisp(int argc, char **argv)
{
    for(int i = 1; i < argc; i++)
    {
        if(!strcmp(argv[i], "-lisp"))
        {
            lisp_init();
            char const *eof_char = "Ctrl-D";
            fprintf(stderr,
                    " CLIVE (C) 1995 Jonathan Clark, all rights reserved\n"
                    "   (C LISP interpreter and various extentions)\n"
                    "Type (%s) to exit\n", eof_char);

            while(!feof(stdin))
            {
                fprintf(stderr, "Lisp> ");
                char *l = get_line(0);
                char const *s = l;
                while(*s)
                {
                    LObject *prog = LObject::Compile(s);
                    l_user_stack.push(prog);
                    while(*s==' ' || *s=='\t' || *s=='\r' || *s=='\n') s++;
                    prog->Eval()->Print();
                    l_user_stack.pop(1);
                }
                free(l);
            }
            fprintf(stderr, "End of input : bye\n");
            exit(0);
        }
    }
}


void music_check()
{
  if(sound_avail & MUSIC_INITIALIZED)
  {
    if(current_song && !current_song->playing())
    {
      current_song->play(music_volume);
      dprintf("song finished\n");
    }
    if(!current_song)
    {

      current_song = new song("music/intro.hmi");
      current_song->play(music_volume);

/*      if(DEFINEDP(symbol_function(l_next_song)))  // if user function installed, call it to load up next song
      {
    int sp = current_space;
    current_space = PERM_SPACE;
    ((LSymbol *)l_next_song)->EvalFunction(NULL);
    current_space = sp;
      } */
    }
  }
}

void setup(int argc, char **argv);

void share_end();
void show_end();

void show_sell(int abortable);

extern pmenu *dev_menu;

void game_net_init(int argc, char **argv)
{
#if !defined __CELLOS_LV2__
  int nonet=!net_init(argc, argv);
  if(nonet)
    dprintf("No network driver, or network driver returned failure\n");
  else
  {
    set_file_opener(open_nfs_file);
    if(main_net_cfg && main_net_cfg->state == net_configuration::CLIENT)
    {
      if(set_file_server(net_server))
      start_running = 1;
      else
      {
                dprintf("Unable to attach to server, quitting\n");
                exit(0);
      }
    } else
    {
      int i;
      for(i = 1; i < argc - 1; i++)
      if(!strcmp(argv[i], "-fs"))
      if(!set_file_server(argv[i + 1]))
      dprintf("could not set default file server to %s\n", argv[i + 1]);
    }
  }
#endif
}

int abuse_main(int argc, char *argv[])
{
    start_argc = argc;
    start_argv = argv;

    for (int i = 0; i < argc; i++)
    {
        if (!strcmp(argv[i], "-cprint"))
            external_print = 1;
    }

#if (defined(__APPLE__) && !defined(__MACH__))
    unsigned char km[16];

    fprintf(stderr, "Mac Options: ");
    xres = 320; yres = 200;
    GetKeys((uint32_t*)&km);
    if ((km[ 0x3a >>3] >> (0x3a & 7)) &1 != 0)
    {
        dev|=EDIT_MODE;
        start_edit = 1;
        start_running = 1;
        disable_autolight = 1;
        fprintf(stderr, "Edit Mode...");
    }
    if ((km[ 0x3b >>3] >> (0x3b & 7)) &1 != 0)
    {
        PixMult = 1;
        fprintf(stderr, "Single Pixel...");
    }
    else
    {
        PixMult = 2;
        fprintf(stderr, "Double Pixel...");
    }
    if ((km[ 0x38 >>3] >> (0x38 & 7)) &1 != 0)
    {
        xres *= 2;  yres *= 2;
        fprintf(stderr, "Double Size...");
    }
    fprintf(stderr, "\n");

    if (tcpip.installed())
        fprintf(stderr, "Using %s\n", tcpip.name());
#endif

    set_dprinter(game_printer);
    set_dgetter(game_getter);
    set_no_space_handler(handle_no_space);

    setup(argc, argv);

    show_startup();

    start_sound(argc, argv);

    stat_man = new text_status_manager();



    jrand_init();
    jrand(); // so compiler doesn't complain

    set_spec_main_file("abuse.spe");
    check_for_lisp(argc, argv);

    do
    {
        if (main_net_cfg && !main_net_cfg->notify_reset())
        {
            sound_uninit();
            exit(0);
        }

        game_net_init(argc, argv);
        lisp_init();

        dev_init(argc, argv);

        Game *g = new Game(argc, argv);

        dev_cont = new dev_controll();
        dev_cont->load_stuff();

        g->get_input(); // prime the net

#if !defined __CELLOS_LV2__
        for (int i = 1; i + 1 < argc; i++)
        {
            if (!strcmp(argv[i], "-server"))
            {
                if (!become_server(argv[i + 1]))
                {
                    dprintf("unable to become a server\n");
                    exit(0);
                }
                break;
            }
        }

        if (main_net_cfg)
            wait_min_players();

        net_send(1);
        if (net_start())
        {
            g->step(); // process all the objects in the world
            g->calc_speed();
            g->update_screen(); // redraw the screen with any changes
        }
#endif

        while (!g->done())
        {
            music_check();

            if (req_end)
            {
                delete current_level; current_level = NULL;

                show_end();

                the_game->set_state(MENU_STATE);
                req_end = 0;
            }

            if (demo_man.current_state() == demo_manager::NORMAL)
                net_receive();

            // see if a request for a level load was made during the last tick
            if (req_name[0])
            {
                g->load_level(req_name);
                req_name[0] = 0;
                g->draw(g->state == SCENE_STATE);
            }

            //if (demo_man.current_state() != demo_manager::PLAYING)
                g->get_input();

            if (demo_man.current_state() == demo_manager::NORMAL)
                net_send();
            else
                demo_man.do_inputs();

#if !defined __CELLOS_LV2__
            service_net_request();
#endif

            // process all the objects in the world
            g->step();
#if !defined __CELLOS_LV2__
            server_check();
#endif
            g->calc_speed();

            // see if a request for a level load was made during the last tick
            if (!req_name[0])
                g->update_screen(); // redraw the screen with any changes
        }

#if !defined __CELLOS_LV2__
        net_uninit();

        if (net_crcs)
            net_crcs->clean_up();
        delete net_crcs; net_crcs = NULL;
#endif

        delete chat;

        Timer tmp; tmp.WaitMs(500);

        delete small_render; small_render = NULL;

        if (current_song)
            current_song->stop();
        delete current_song; current_song = NULL;

        cache.empty();

        delete dev_console; dev_console = NULL;
        delete dev_menu; dev_menu = NULL;
        delete g; g = NULL;
        delete old_pal; old_pal = NULL;

        compiled_uninit();
        delete_all_lights();
        free(white_light_initial);

        for (int i = 0; i < TTINTS; i++)
            free(tints[i]);

        dev_cleanup();
        delete dev_cont; dev_cont = NULL;
        delete stat_man; stat_man = new text_status_manager();

        if (!(main_net_cfg && main_net_cfg->restart_state()))
        {
            LSymbol *end_msg = LSymbol::FindOrCreate("end_msg");
            if (DEFINEDP(end_msg->GetValue()))
                printf("%s\n", lstring_value(end_msg->GetValue()));
        }

        lisp_uninit();

#if !defined __CELLOS_LV2__
        base->packet.packet_reset();
#endif
    }
    while (main_net_cfg && main_net_cfg->restart_state());

    delete stat_man;
    delete main_net_cfg; main_net_cfg = NULL;

    set_filename_prefix(NULL);  // dealloc this mem if there was any
    set_save_filename_prefix(NULL);

    sound_uninit();

    return 0;
}

