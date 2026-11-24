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

#include "common.h"
#include "ui/hud_skin.h"
#include "sdlport/preferences.h"
#include "palette.h"
#include <algorithm>
#include <array>
#include <memory>

#include "sbar.h"
#include "view.h"
#include "lisp.h"
#include "cache.h"
#include "demo.h"
#include "chars.h"
#include "objects.h"
#include "game.h"
#include "clisp.h"

status_bar sbar;

status_bar::status_bar()
{
  v=NULL;
  changed_cursor=0;
  icon_in_selection=-1;  // the weapon the mouse cursor is on top of, -1 if none
}

// defined in dev.c
void scale_put_trans(image *im, image *screen, int x, int y, short new_width, short new_height);
void scale_put(image *im, image *screen, int x, int y, short new_width, short new_height);
extern image *small_render;


void status_bar::load()
{
  char sbname[100];
  char iname[20];
  void *l_name = LSymbol::FindOrCreate("sbar_file");
  if (symbol_value(l_name)!=l_undefined)
    strcpy(sbname,lstring_value(symbol_value(l_name)));
  else strcpy(sbname,"art/statbar.spe");

  int i;
  for (i=0; i<TOTAL_WEAPONS; i++)
  {
    sprintf(iname,"bweap%04d.pcx",i+1);
    bweap[i]=cache.reg(sbname,iname,SPEC_IMAGE);

    sprintf(iname,"dweap%04d.pcx",i+1);
    dweap[i]=cache.reg(sbname,iname,SPEC_IMAGE);
  }

  for (i=0; i<30; i++)
  {
    sprintf(iname,"bnum%02d",i);
    bnum[i]=cache.reg(sbname,iname,SPEC_IMAGE);
  }


  sbar=cache.reg(sbname,"sbar",SPEC_IMAGE);
  sbar_select=cache.reg(sbname,"sbar_select",SPEC_IMAGE);
  sbar_numpad=cache.reg(sbname,"sbar_numpad",SPEC_IMAGE);
}

void status_bar::draw_num(image *screen, int x, int y, int num, int *offset)
{
  if (num<0 || num>999)
  {
    printf("bad number for statbar\n");
    return ;
  }

  image *im=cache.img(*offset);
  int dw=small_render ? im->Size().x*2 : im->Size().x;
  int dh=small_render ? im->Size().y*2 : im->Size().y;

  int n=num/100;
  scale_put(cache.img(offset[n]),screen,x,y,dw,dh);
  num-=n*100;

  x+=dw; n=num/10;
  scale_put(cache.img(offset[n]),screen,x,y,dw,dh);
  num-=n*10;

  x+=dw;
  scale_put(cache.img(offset[num]),screen,x,y,dw,dh);
}

// Convert the generated RGB skin to the current level palette only when it changes.
static image *hud_skin()
{
    static std::unique_ptr<image> skin;
    static std::array<unsigned char, 768> previous{};
    std::array<unsigned char, 768> current{};
    for (int i = 0; i < 256; ++i) {
        current[i * 3] = pal->red(i); current[i * 3 + 1] = pal->green(i); current[i * 3 + 2] = pal->blue(i);
    }
    if (!skin || current != previous) {
        skin.reset(new image(vec2i(hud_width, hud_height)));
        image_list.unlink(skin.get()); // Owned here; exclude from legacy image_uninit bulk deletion.
        std::array<int, 65536> mapped; mapped.fill(-1);
        for (int y = 0; y < hud_height; ++y) for (int x = 0; x < hud_width; ++x) {
            const auto rgb = hud_pixels[y * hud_width + x];
            int &color = mapped[rgb];
            if (color < 0) color = pal->find_closest(((rgb >> 11) & 31) * 255 / 31,
                ((rgb >> 5) & 63) * 255 / 63, (rgb & 31) * 255 / 31);
            skin->scan_line(y)[x] = color;
        }
        previous = current;
    }
    return skin.get();
}

void status_bar::redraw(image *screen)
{
    if (!v || !total_weapons || !playing_state(the_game->state)) return;
    int sx, sy, right, bottom; area(sx, sy, right, bottom);
    if (sx >= right || sy >= bottom) return;
    int clip_left, clip_top, clip_right, clip_bottom;
    screen->GetClip(clip_left, clip_top, clip_right, clip_bottom);
    screen->SetClip(0, 0, screen->Size().x, screen->Size().y);
    const int scale = small_render ? 2 : 1;
    image *panel = game_settings.modern_hud ? hud_skin() : cache.img(sbar);
    if (scale == 1)
        panel->put_image(screen, sx, sy);
    else
        scale_put(panel, screen, sx, sy, right - sx, bottom - sy);
    const int health = v->focus ? v->focus->hp() : 0;
    if (v->focus) v->last_hp = health;
    draw_health(screen, health);
    if (!game_settings.modern_hud) {
        for (int i = 0; i < std::min(TOTAL_WEAPONS, total_weapons); ++i) {
            if (!v->has_weapon(i)) continue;
            const int left = sx + (40 + i * 34) * scale;
            const bool selected = v->current_weapon == i;
            image *icon = cache.img(selected ? bweap[i] : dweap[i]);
            image *numpad = cache.img(sbar_numpad);
            scale_put_trans(icon, screen, left, sy, icon->Size().x * scale, icon->Size().y * scale);
            scale_put_trans(numpad, screen, left - 2, sy + 21 * scale,
                            numpad->Size().x * scale, numpad->Size().y * scale);
            draw_ammo(screen, i, v->weapon_total(i), selected);
            if (icon_in_selection == i) {
                image *highlight = cache.img(sbar_select);
                scale_put_trans(highlight, screen, left + 4 * scale, sy,
                                highlight->Size().x * scale, highlight->Size().y * scale);
            }
        }
    } else {
        const int accent = pal->find_closest(240, 169, 66), muted = pal->find_closest(62, 68, 73);
        const int empty = pal->find_closest(13, 16, 19);
        for (int i = 0; i < std::min(TOTAL_WEAPONS, total_weapons); ++i) {
            const int left = sx + (hud_health_width + i * hud_slot_width) * scale;
            if (!v->has_weapon(i)) {
                screen->bar(left + 4 * scale, sy + 8 * scale, left + 32 * scale, sy + 29 * scale, empty);
                screen->line(left + 16 * scale, sy + 18 * scale, left + 24 * scale, sy + 18 * scale, muted);
            } else {
                draw_ammo(screen, i, v->weapon_total(i), v->current_weapon == i);
            }
            if (v->current_weapon == i || icon_in_selection == i)
                screen->rectangle(left + scale, sy + scale, left + (hud_slot_width - 2) * scale, bottom - 2 * scale,
                    v->current_weapon == i ? accent : muted);
        }
    }
    screen->SetClip(clip_left, clip_top, clip_right, clip_bottom);
}

void status_bar::area(int &x1, int &y1, int &x2, int &y2)
{
  if (sbar<=0 || !total_weapons)
  {
    x2=xres;
    y2=yres;
    x1=x2;
    y1=y2;
    return ;
  }

  int sb_w=hud_width, sb_h=hud_height;
  if (!game_settings.modern_hud) {
    image *classic = cache.img(sbar);
    sb_w = classic->Size().x;
    sb_h = classic->Size().y;
  }

  if (small_render) { sb_w*=2; sb_h*=2; }

  x1=(xres-sb_w)/2;
  x2=x1+sb_w;
  y1=yres-sb_h;
  y2=yres;
}


void status_bar::draw_health(image *screen,int amount)
{
  if (total_weapons)
  {
    int x1,y1,x2,y2;
    area(x1,y1,x2,y2);
    const int scale = small_render ? 2 : 1;
    if (!game_settings.modern_hud) {
      draw_num(screen, x1 + 17 * scale, y1 + 11 * scale, std::clamp(amount, 0, 999), bnum);
      return;
    }
    draw_num(screen, x1 + 9 * scale, y1 + 32 * scale, std::clamp(amount, 0, 999), bnum);
    const int dark = pal->find_closest(35, 18, 18), red = pal->find_closest(220, 42, 38);
    screen->bar(x1 + 6 * scale, y1 + 43 * scale, x1 + 25 * scale, y1 + 44 * scale, dark);
    const int filled = std::clamp(amount, 0, 100) * 20 / 100;
    if (filled) screen->bar(x1 + 6 * scale, y1 + 43 * scale, x1 + (6 + filled) * scale - 1, y1 + 44 * scale, red);
  }
}


void status_bar::draw_ammo(image *screen, int weapon_num, int amount, int light)
{
  if (weapon_num < 0 || weapon_num >= std::min(hud_weapon_count, total_weapons)) return;
  if (total_weapons)
  {
    int x1,y1,x2,y2;
    area(x1,y1,x2,y2);
    const int scale = small_render ? 2 : 1;
    const int x = game_settings.modern_hud ? hud_health_width + 10 + weapon_num * hud_slot_width : 52 + weapon_num * 34;
    const int y = game_settings.modern_hud ? 32 : 25;
    draw_num(screen, x1 + x * scale, y1 + y * scale, std::clamp(amount, 0, 999), bnum + (light ? 20 : 10));
  }
}


int status_bar::mouse_in_area()
{
  if (!v) return 0;
  int x1,y1,x2,y2;
  area(x1,y1,x2,y2);

  int mx,my;
  if (small_render)
  {
    mx=(v->pointer_x-v->cx1)*2+v->cx1;
    my=(v->pointer_y-v->cy1)*2+v->cy1;
  } else
  {
    mx=v->pointer_x;
    my=v->pointer_y;
  }

  if (mx>=x1 && my>=y1 && mx<=x2 && my<=y2)
    return 1;
  else return 0;
}


void status_bar::draw_update()
{
  if (total_weapons && v)
  {
    if (DEFINEDP(symbol_value(l_mouse_can_switch)) && symbol_value(l_mouse_can_switch) &&
    mouse_in_area())
    {
      if ((current_level->tick_counter()&4)==0)
        wm->set_mouse_shape(cache.img(c_mouse1)->copy(),4,4);
      else wm->set_mouse_shape(cache.img(c_mouse2)->copy(),4,4);
      changed_cursor=1;
    } else if (changed_cursor)
    {
      if (!(dev&EDIT_MODE))
        wm->set_mouse_shape(cache.img(c_target)->copy(),8,8);
      else
        wm->set_mouse_shape(cache.img(c_normal)->copy(),1,1);
      changed_cursor=0;
    }

    redraw(screen);
  }
}


void status_bar::step()
{
  if (!v) return ;
  if (!DEFINEDP(symbol_value(l_mouse_can_switch)) || !symbol_value(l_mouse_can_switch)) return ;

  // see if the mouse is in the sbar region (demo_x already corrected for small_render)
  int sx1,sy1,sx2,sy2;
  area(sx1,sy1,sx2,sy2);

  int view_y2=small_render ? (v->cy2-v->cy1+1)*2+v->cy1 : v->cy2;
  if (sy1<view_y2)     // tell view to shrink if it is overlapping the status bar
  {
    v->suggest.send_view=1;
    v->suggest.cx1=v->cx1;
    v->suggest.cy1=v->cy1;
    v->suggest.cx2=v->cx2;
    v->suggest.cy2=small_render ? (sy1-v->cy1-2)/2+v->cy1 : sy1-2;
  }

  if (sbar<=0 || !total_weapons) return ;


  int mx=small_render ? (last_demo_mx-v->cx1)*2+v->cx1 : last_demo_mx;
  int my=small_render ? (last_demo_my-v->cy1)*2+v->cy1 : last_demo_my;


  if (mx>sx1 && my>sy1 && mx<sx2 && my<sy2)
  {

    mx -= sx1;
    if (small_render) mx /= 2;
    const int first_slot = game_settings.modern_hud ? hud_health_width : 40;
    const int slot_width = game_settings.modern_hud ? hud_slot_width : 34;
    const int new_target = std::clamp((mx - first_slot) / slot_width, 0, std::min(TOTAL_WEAPONS, total_weapons) - 1);

    if (v->has_weapon(new_target))
      icon_in_selection=new_target;

    if (last_demo_mbut==2 && icon_in_selection!=v->current_weapon &&
    icon_in_selection!=-1) // the user requested a weapon change
    {
      v->suggest.send_weapon_change=1;
      v->suggest.new_weapon=icon_in_selection;
    }

  } else
  {
    icon_in_selection=-1;
  }
}





