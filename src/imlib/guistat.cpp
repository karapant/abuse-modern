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

#include <string.h>
#include <algorithm>
#include <string>

#include "common.h"

#include "input.h"
#include "status.h"
#include "timing.h"
#include "guistat.h"

class gui_status_node
{
  public :
  char *name;
  gui_status_node *next;
  visual_object *show;
  Jwindow *stat_win;
  int last_update;
  time_marker last_time;
  gui_status_node(char const *Name, visual_object *Show, gui_status_node *Next)
  { name = strdup(Name);
    show=Show;
    next=Next;
    last_update=0;
    stat_win=NULL;
  }
  ~gui_status_node();
} ;


gui_status_node::~gui_status_node()
{
  free(name);
  if (show)
    delete show;
  if (stat_win)
  {
    wm->close_window(stat_win);
    wm->flush_screen();
  }
}

void gui_status_manager::draw_bar(gui_status_node *whom, int perc)
{
  long l=whom->stat_win->x2()-whom->stat_win->x1()-6;
  long h=wm->font()->height();

  whom->stat_win->screen->bar(whom->stat_win->x1()+1,whom->stat_win->y2()-h-1,whom->stat_win->x2()-1,
              whom->stat_win->y2()-1,wm->black());
  whom->stat_win->screen->bar(whom->stat_win->x1()+2,whom->stat_win->y2()-h,whom->stat_win->x2()-2,
              whom->stat_win->y2()-2,wm->dark_color());
  if (perc)
    whom->stat_win->screen->bar(whom->stat_win->x1()+3,whom->stat_win->y2()-h+1,
                whom->stat_win->x1()+l*perc/100,
                whom->stat_win->y2()-3,wm->bright_color());
}

void gui_status_manager::push(char const *name, visual_object *show)
{
  first=new gui_status_node(name,show,first);
}

gui_status_manager::gui_status_manager()
{
  first=NULL;
  strcpy(title,"STATUS");
  last_perc=0;
}

void gui_status_manager::update(int percentage)
{
    last_perc = percentage;
    if (!first) return;
    if (!first->stat_win) {
        time_marker now;
        if (now.diff_time(&first->last_time) <= 1) return;
        const int frame_width = Jwindow::left_border() + Jwindow::right_border();
        const int available = wm->screen->Size().x - frame_width - 12;
        const int chars = std::max(0, (available - 10) / wm->font()->width());
        std::string name(first->name);
        if (name.size() > size_t(chars)) name = name.substr(0, std::max(0, chars - 3)) + "...";
        const int text_width = int(name.size()) * wm->font()->width() + 10;
        const int visual_width = first->show ? first->show->width() : 0;
        const int width = std::min(available, std::max(text_width, visual_width));
        const int row_height = wm->font()->height() + 5;
        const int visual_height = first->show ? first->show->height() : 0;
        int y = 10;
        for (auto *status = first->next; status; status = status->next) {
            if (!status->stat_win) continue;
            y = status->stat_win->y + status->stat_win->h + 5;
            break;
        }

        first->stat_win = wm->new_window((wm->screen->Size().x - width - frame_width) / 2, y,
                                        width, row_height * 2 + visual_height, NULL, title);
        auto *window = first->stat_win;
        const int text_y = window->y1() + wm->font()->height() / 2;
        wm->font()->put_string(window->screen, window->x1() + 5, text_y, name.c_str(), wm->bright_color());
        if (first->show)
            first->show->draw(window->screen, window->x1() + (width - visual_width) / 2,
                              text_y + row_height, NULL);
    } else if (percentage <= first->last_update) {
        return;
    }
    first->last_update = percentage;
    draw_bar(first,percentage);
    wm->flush_screen();
}

void gui_status_manager::force_display()
{
  update(last_perc);
}

void gui_status_manager::pop()
{
  CONDITION(first,"No status's to pop!");
  gui_status_node *p=first;
  first=first->next;
  delete p;
}









