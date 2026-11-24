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

#include "automap.h"
#include "game.h"
#include "sdlport/controller.h"
#include "sdlport/platform.h"
#include "ui/textmenu.h"
#include "netcfg.h"
#include <algorithm>

automap *current_automap=0;

void automap::draw()
{
  if (!automap_window) return ;
  image *screen=automap_window->screen;

  long sx,ex,sy,ey,x,y,window_xstart,window_ystart,
                       window_xend,window_yend,centerx,centery,
                       draw_xstart,draw_ystart,
                       i,j;

  x=the_game->first_view->x_center();
  y=the_game->first_view->y_center();


  window_xstart=automap_window->x1();
  window_ystart=automap_window->y1();
  window_xend=automap_window->x2();
  window_yend=automap_window->y2();
  centerx=(window_xstart+window_xend)/2;
  centery=(window_ystart+window_yend)/2;

  sx=x/f_wid-w/2;                // start drawing with this foretile
  sy=y/f_hi-h/2;
  ex=sx+w;
  ey=sy+h;

  if (sx<0)                       // does the map scroll past the left side ?
  { sx=0;                         // yes, start drawing at 0
    draw_xstart=centerx-(x*AUTOTILE_WIDTH/f_wid);
  }
  else
    draw_xstart=centerx-(x*AUTOTILE_WIDTH/f_wid-sx*AUTOTILE_WIDTH);

  if (sy<0)
  {
    sy=0;
    draw_ystart=centery-(y*AUTOTILE_HEIGHT/f_hi);
  }
  else
    draw_ystart=centery-(y*AUTOTILE_HEIGHT/f_hi-sy*AUTOTILE_HEIGHT);

  // if view position hasn't changed, only update the blinking dot and return
  if (draw_xstart==old_dx && draw_ystart==old_dy)
  {
   automap_window->screen->Lock();
   automap_window->screen->AddDirty(centerx,centery,centerx + 1,centery + 1);
    if ((tick++)&4)
      automap_window->screen->PutPixel(vec2i(centerx,centery),255);
    else
      automap_window->screen->PutPixel(vec2i(centerx,centery),27);
   automap_window->screen->Unlock();
    return ;
  }

  old_dx=draw_xstart;
  old_dy=draw_ystart;


  if (ex>=cur_lev->foreground_width())
    ex=cur_lev->foreground_width()-1;
  if (ey>=cur_lev->foreground_height())
    ey=cur_lev->foreground_height()-1;


  screen->bar(window_xstart,window_ystart,draw_xstart,window_yend,0);
  screen->bar(window_xstart,window_ystart,window_xend,draw_ystart,0);


/*  if (ex>=cur_lev->foreground_width())
  {
    draw_xend=center
    ex=foreground_width()-1; */




  // we are going to redraw the whole map, so make the dirty rect work easier by marking
  // everything dirty
  screen->AddDirty(window_xstart,window_ystart,window_xend+1,window_yend+1);




  // draw the tiles that will be around the border of the automap with put_image
  // because it handles clipping, but for ths reason is slower, the rest
  // we will slam on as fast as possible

  screen->SetClip(window_xstart,window_ystart,window_xend+1,window_yend+1);
/*  for (i=draw_xstart,j=draw_ystart,x=sx,y=sy; y<=ey; j+=AUTOTILE_HEIGHT,y++)
    foretiles[cur_lev->get_fg(x,y)]->micro_image->put_image(screen,i,j,0);

  for (i=draw_xstart+ex*AUTOTILE_WIDTH,j=draw_ystart,y=sy,x=ex; y<=ey; j+=AUTOTILE_HEIGHT,y++)
    foretiles[cur_lev->get_fg(x,y)]->micro_image->put_image(screen,i,j,0);

  for (i=draw_xstart,j=draw_ystart,x=sx,y=sy; x<=ex; i+=AUTOTILE_WIDTH,x++)
    foretiles[cur_lev->get_fg(x,y)]->micro_image->put_image(screen,i,j,0);

  for (i=draw_xstart,j=draw_ystart+ey*AUTOTILE_HEIGHT,x=sx,y=ex; x<=ex; i+=AUTOTILE_WIDTH,x++)
    foretiles[cur_lev->get_fg(x,y)]->micro_image->put_image(screen,i,j,0); */



  unsigned short *fgline;
  for (j=draw_ystart,y=sy; y<=ey; j+=AUTOTILE_HEIGHT,y++)
  {
    fgline=cur_lev->get_fgline(y)+sx;
    for (i=draw_xstart,x=sx; x<=ex; i+=AUTOTILE_WIDTH,x++,fgline++)
    {
      if ((*fgline)&0x8000)
      {
    int id=foretiles[fgvalue(*fgline)];
    if (id>=0)
          cache.foret(id)->micro_image->put_image(screen,i,j,0);
    else
          cache.foret(foretiles[0])->micro_image->put_image(screen,i,j,0);
      }
      else
        screen->bar(i,j,i+AUTOTILE_WIDTH-1,j+AUTOTILE_HEIGHT-1,0);
    }
  }

  // draw the person as a dot, no need to add a dirty because we marked the
  // whole screen already
  automap_window->screen->Lock();
  if ((tick++)&4)
    automap_window->screen->PutPixel(vec2i(centerx,centery),255);
  else
    automap_window->screen->PutPixel(vec2i(centerx,centery),27);
  automap_window->screen->Unlock();

  // set the clip back to full window size because soemthing else could mess with the area
  automap_window->screen->SetClip(0,0,screen->Size().x,screen->Size().y);
}

bool pause_map_available()
{
    return current_level && player_list && !player_list->next && player_list->focus &&
           player_list->local_player() &&
           (!main_net_cfg || main_net_cfg->state == net_configuration::SINGLE_PLAYER);
}

namespace {
enum class ChartPixel : uint8_t { Hidden, Passage, Terrain };

// Sample the authored collision shape rather than coloring the whole tile.
bool chart_solid(const boundary &shape, vec2i point, vec2i cell)
{
    const int px = (2 * point.x + 1) * f_wid;
    const int py = (2 * point.y + 1) * f_hi;
    auto vertex = [&](int i) {
        const int x = shape.data[2 * i], y = shape.data[2 * i + 1];
        return vec2i((x == f_wid - 1 ? f_wid : x) * 2 * cell.x,
                     (y == f_hi - 1 ? f_hi : y) * 2 * cell.y);
    };
    bool solid = false;
    for (int i = 0; i + 1 < shape.tot; ++i) {
        auto a = vertex(i), b = vertex(i + 1);
        if (a.y > b.y) std::swap(a, b);
        if (a.y <= py && py < b.y &&
            (px - a.x) * (b.y - a.y) < (py - a.y) * (b.x - a.x))
            solid = !solid;
    }
    return solid;
}

void draw_chart(level &map, vec2i origin, vec2i cell, vec2i first, vec2i last)
{
    int left, top, right, bottom;
    screen->GetClip(left, top, right, bottom);
    const int width = right - left, height = bottom - top;
    std::vector<ChartPixel> tiles(size_t(width) * height, ChartPixel::Hidden);
    for (int ty = first.y; ty <= last.y; ++ty) {
        const int sy = origin.y + ty * cell.y;
        for (int tx = first.x; tx <= last.x; ++tx) {
            if (!map.map_seen(tx, ty)) continue;
            const int sx = origin.x + tx * cell.x;
            const int tile = map.get_fg(tx, ty);
            const auto *shape = tile ? the_game->get_fg(tile)->points : nullptr;
            for (int py = std::max(top, sy); py < std::min(bottom, sy + cell.y); ++py) {
                auto *row = tiles.data() + size_t(py - top) * width;
                for (int px = std::max(left, sx); px < std::min(right, sx + cell.x); ++px) {
                    row[px - left] = shape && chart_solid(*shape, vec2i(px - sx, py - sy), cell)
                        ? ChartPixel::Terrain : ChartPixel::Passage;
                }
            }
        }
    }

    const auto material = [&](int x, int y) -> ChartPixel {
        if (x < 0 || y < 0 || x >= width || y >= height) return ChartPixel::Hidden;
        return tiles[size_t(y) * width + x];
    };
    const int fog = pal->find_closest(10, 16, 23);
    const int grid = pal->find_closest(32, 45, 54);
    const int passage = pal->find_closest(50, 77, 96);
    const int frontier = pal->find_closest(28, 48, 63);
    const int rock = pal->find_closest(19, 30, 40);
    const int hatch = pal->find_closest(30, 44, 54);
    const int contour = pal->find_closest(123, 169, 186);
    for (int y = 0; y < height; ++y) {
        auto *row = screen->scan_line(top + y) + left;
        for (int x = 0; x < width; ++x) {
            const int map_x = left + x - origin.x, map_y = top + y - origin.y;
            switch (material(x, y)) {
            case ChartPixel::Hidden:
                row[x] = map_x % (4 * cell.x) == 0 && map_y % (4 * cell.y) == 0 ? grid : fog;
                break;
            case ChartPixel::Passage:
                row[x] = material(x - 1, y) == ChartPixel::Hidden ||
                         material(x + 1, y) == ChartPixel::Hidden ||
                         material(x, y - 1) == ChartPixel::Hidden ||
                         material(x, y + 1) == ChartPixel::Hidden ? frontier : passage;
                break;
            case ChartPixel::Terrain:
                if (material(x - 1, y) == ChartPixel::Passage || material(x + 1, y) == ChartPixel::Passage ||
                    material(x, y - 1) == ChartPixel::Passage || material(x, y + 1) == ChartPixel::Passage)
                    row[x] = contour;
                else
                    row[x] = (map_x + map_y) % 5 == 0 ? hatch : rock;
                break;
            }
        }
    }
}

void chart_diamond(vec2i position, int radius, int color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        const int span = radius - std::abs(dy);
        screen->bar(position.x - span, position.y + dy, position.x + span, position.y + dy, color);
    }
}

struct ChartInterest {
    vec2i position;
    const char *label;
    bool checkpoint;
};

std::vector<ChartInterest> chart_interests(level &map)
{
    std::vector<ChartInterest> interests;
    for (const auto &checkpoint : map.map_checkpoints())
        interests.push_back({vec2i(checkpoint.x, std::max(0, checkpoint.y - 10)), "Save checkpoint", true});

    for (auto *object = map.first_object(); object; object = object->next) {
        const char *name = object_names[object->otype];
        const char *label = nullptr;
        if (!strcmp(name, "SWITCH")) label = "Switch";
        else if (!strcmp(name, "SWITCH_ONCE")) label = "One-use switch";
        else if (!strcmp(name, "SWITCH_DELAY")) label = "Timed switch";
        if (!label) continue;
        int32_t left, top, right, bottom;
        object->picture_space(left, top, right, bottom);
        const vec2i position((left + right) / 2, (top + bottom) / 2);
        if (position.x >= 0 && position.y >= 0 && map.map_seen(position.x / f_wid, position.y / f_hi))
            interests.push_back({position, label, false});
    }
    return interests;
}

void chart_switch(vec2i pos, int color, int shadow)
{
    screen->bar(pos.x - 4, pos.y - 4, pos.x + 4, pos.y + 4, shadow);
    screen->rectangle(pos.x - 3, pos.y - 3, pos.x + 3, pos.y + 3, color);
    screen->bar(pos.x - 1, pos.y, pos.x + 1, pos.y, color);
}

std::string chart_level_label(level &map)
{
    std::string name = map.original_name();
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".spe") == 0) name.resize(name.size() - 4);
    if (name.size() == 7 && name.compare(0, 5, "level") == 0)
        return "Level " + name.substr(5);
    return name;
}
}

void show_pause_map(level &map, game_object &player)
{
    ControllerMenuScope controller;
    wm->set_mouse_shape(cache.img(c_normal)->copy(), 1, 1);
    int clip_left, clip_top, clip_right, clip_bottom;
    screen->GetClip(clip_left, clip_top, clip_right, clip_bottom);
    screen->SetClip(0, 0, screen->Size().x, screen->Size().y);
    map.explore_map(player);
    const auto interests = chart_interests(map);
    const vec2i player_tile(player.x / f_wid, std::max(0, player.y - 10) / f_hi);
    vec2i first = player_tile, last = player_tile;
    for (int y = 0; y < map.foreground_height(); ++y)
        for (int x = 0; x < map.foreground_width(); ++x)
            if (map.map_seen(x, y)) {
                first = vec2i(std::min(first.x, x), std::min(first.y, y));
                last = vec2i(std::max(last.x, x), std::max(last.y, y));
            }

    vec2i center = player_tile;
    const auto level_label = chart_level_label(map);
    int zoom = last.x - first.x < 12 && last.y - first.y < 11 ? 2 : 1;
    for (;;) {
        MenuText text(*big_font, platform_begin_menu_text());
        const auto size = screen->Size();
        const int width = std::min(size.x - 20, 600), height = std::min(size.y - 16, 370);
        const int x = (size.x - width) / 2, y = (size.y - height) / 2;
        const int left = x + 9, right = x + width - 9, top = y + 31, bottom = y + height - 38;
        const int ink = pal->find_closest(232, 237, 226);
        const int muted = pal->find_closest(158, 171, 176);
        const int amber = pal->find_closest(245, 182, 67);
        const int green = pal->find_closest(87, 163, 43);
        const int blue = pal->find_closest(111, 199, 230);
        const int border = pal->find_closest(65, 90, 106);
        const int shadow = pal->find_closest(7, 11, 17);
        screen->clear(pal->find_closest(10, 14, 20));
        screen->bar(x, y, x + width - 1, y + height - 1, pal->find_closest(18, 24, 30));
        screen->rectangle(x, y, x + width - 1, y + height - 1, border);
        screen->bar(x + 1, y + 1, x + 3, y + 25, amber);
        text.put(*screen, left, y + 6, "Explored map", ink, true);
        const std::string zoom_label = std::to_string(1 << zoom) + "x";
        text.put(*screen, right - text.width(zoom_label), y + 8, zoom_label, amber);
        const int mid = left + text.width("Explored map", true) + 12;
        text.put(*screen, mid, y + 8,
                 text.fit(level_label, right - text.width(zoom_label) - mid - 12), muted);
        const vec2i cell(4 << zoom, 2 << zoom);
        const vec2i midpoint((left + right) / 2, (top + bottom) / 2);
        const vec2i origin = midpoint - vec2i(center.x * cell.x, center.y * cell.y) - cell / 2;
        const auto project = [&](vec2i point) {
            return origin + vec2i(point.x * cell.x / f_wid, point.y * cell.y / f_hi);
        };
        screen->SetClip(left, top, right, bottom);
        const int start_x = std::max(first.x, center.x - width / (2 * cell.x) - 1);
        const int end_x = std::min(last.x, center.x + width / (2 * cell.x) + 1);
        const int start_y = std::max(first.y, center.y - (bottom - top) / (2 * cell.y) - 1);
        const int end_y = std::min(last.y, center.y + (bottom - top) / (2 * cell.y) + 1);
        draw_chart(map, origin, cell, vec2i(start_x, start_y), vec2i(end_x, end_y));
        vec2i mouse(0, 0);
        int buttons;
        wm->mouse_status(mouse.x, mouse.y, buttons);
        const ChartInterest *hovered = nullptr;
        int closest = 8 * 8;
        const auto inside = [&](vec2i point) {
            return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
        };
        const auto draw_interest = [&](const ChartInterest &interest, vec2i pos, int color) {
            if (interest.checkpoint) {
                chart_diamond(pos, 4, shadow);
                chart_diamond(pos, 3, color);
                chart_diamond(pos, 1, shadow);
            } else {
                chart_switch(pos, color, shadow);
            }
        };
        for (const auto &interest : interests) {
            const auto pos = project(interest.position);
            if (!inside(pos)) continue;
            draw_interest(interest, pos, interest.checkpoint ? green : blue);
            const auto delta = pos - mouse;
            const int distance = delta.x * delta.x + delta.y * delta.y;
            if (inside(mouse) && distance < closest) {
                hovered = &interest;
                closest = distance;
            }
        }
        const auto position = project(vec2i(player.x, std::max(0, player.y - 10)));
        if (inside(position)) {
            chart_diamond(position, 5, shadow);
            chart_diamond(position, 4, amber);
            const int facing = player.direction < 0 ? -1 : 1;
            screen->line(position.x + facing * 4, position.y, position.x + facing * 7, position.y, amber);
        }
        screen->SetClip(0, 0, size.x, size.y);
        screen->rectangle(left, top, right - 1, bottom - 1, border);
        if (hovered) {
            const auto pos = project(hovered->position);
            const int accent = hovered->checkpoint ? green : blue;
            const int label_width = text.width(hovered->label) + 12, label_height = text.height() + 8;
            const int label_x = std::clamp(pos.x + 8, left + 1, right - label_width - 1);
            const int label_y = pos.y - label_height - 5 >= top + 1
                              ? pos.y - label_height - 5 : pos.y + 6;
            screen->SetClip(left + 1, top + 1, right - 1, bottom - 1);
            draw_interest(*hovered, pos, ink);
            screen->bar(label_x, label_y, label_x + label_width - 1, label_y + label_height - 1, shadow);
            screen->rectangle(label_x, label_y, label_x + label_width - 1, label_y + label_height - 1, accent);
            text.put(*screen, label_x + 6, label_y + 4, hovered->label, ink);
            screen->SetClip(0, 0, size.x, size.y);
        }
        chart_diamond(vec2i(left + 4, bottom + 9), 3, amber);
        text.put(*screen, left + 11, bottom + 3, "You", ink);
        chart_diamond(vec2i(left + 41, bottom + 9), 3, green);
        chart_diamond(vec2i(left + 41, bottom + 9), 1, shadow);
        text.put(*screen, left + 48, bottom + 3, "Save checkpoint", ink);
        const int switch_x = left + 48 + text.width("Save checkpoint") + 13;
        chart_switch(vec2i(switch_x, bottom + 9), blue, shadow);
        text.put(*screen, switch_x + 7, bottom + 3, "Switch", ink);
        const char *controls = controller_use_hint() ? "D-pad pan   LB/RB zoom   Confirm center   Back close" :
                                                     "Arrows pan   +/- zoom   Enter center   Tab back";
        text.put(*screen, left, bottom + 18, text.fit(controls, right - left), muted);
        wm->flush_screen();

        event input;
        wm->get_event(input);
        if (input.type == EV_MOUSE_BUTTON && (input.mouse_button & LEFT_BUTTON) &&
            input.mouse_move.x >= left && input.mouse_move.x < right &&
            input.mouse_move.y >= top && input.mouse_move.y < bottom) {
            center = vec2i((input.mouse_move.x - origin.x) / cell.x, (input.mouse_move.y - origin.y) / cell.y);
        } else if (input.type == EV_KEY) {
            switch (input.key) {
            case JK_TAB: case JK_ESC: case 'm':
                platform_end_menu_text();
                screen->SetClip(clip_left, clip_top, clip_right, clip_bottom);
                the_game->reset_keymap();
                if (playing_state(the_game->state)) set_aim_cursor();
                return;
            case JK_UP: center.y -= 4; break;
            case JK_DOWN: center.y += 4; break;
            case JK_LEFT: center.x -= 2; break;
            case JK_RIGHT: center.x += 2; break;
            case '+': case '=': case JK_PAGEUP: zoom = std::min(2, zoom + 1); break;
            case '-': case JK_PAGEDOWN: zoom = std::max(0, zoom - 1); break;
            case JK_HOME: case JK_ENTER: center = player_tile; break;
            }
        }
        center.x = std::clamp(center.x, first.x, last.x);
        center.y = std::clamp(center.y, first.y, last.y);
    }
}

void automap::toggle_window()
{
    if (automap_window)
    {
        wm->close_window(automap_window);
        automap_window = NULL;
    }
    else
    {
        old_dx = -1000; // make sure the map gets drawn the first time
        old_dy = -1000;

        automap_window = wm->new_window(0, 0, w * AUTOTILE_WIDTH,
                                        h * AUTOTILE_HEIGHT, NULL, "Map");
        automap_window->screen->bar(17, 1, 17 + 8 * 6 + 3, 6,
                                    wm->medium_color());
        wm->font()->put_string(automap_window->screen, 20, 2, "Automap",
                               wm->dark_color());
        draw();
    }
}


automap::automap(level *l, int width, int height)
{
  w=width;
  h=height;

  tick=0;
  cur_lev=l;
  automap_window=NULL;
  toggle_window();
}

void automap::handle_event(event &ev)
{

  //only respond to stuff in our window or on the main screen
  if (ev.window==NULL || ev.window==automap_window)
  {
    switch (ev.type)
    {
      case EV_KEY :
        switch(ev.key)
    {
      case 'A' :
      case 'a' :
          toggle_window();
        break;
    }
    break;
      case EV_CLOSE_WINDOW :
        wm->close_window(automap_window);
    automap_window=NULL;
    break;
    }
  }
}







