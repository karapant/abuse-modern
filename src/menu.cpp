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

#include <math.h>
#include <algorithm>
#include <string>
#include "ui/textmenu.h"
#include "ui/textentry.h"
#include "sdlport/controls.h"
#include "sdlport/controller.h"
#include "configuration.h"
#include "sdlport/platform.h"
#include "sdlport/preferences.h"

#include "common.h"
#include "automap.h"

#include "dev.h"

#include "ui/volumewindow.h"

#include "menu.h"
#include "lisp.h"
#include "game.h"
#include "timing.h"
#include "game.h"
#include "id.h"
#include "pmenu.h"
#include "gui.h"
#include "property.h"
#include "clisp.h"
#include "gamma.h"
#include "dprint.h"
#include "demo.h"
#include "loadgame.h"
#include "scroller.h"
#include "netcfg.h"
#include "view.h"

#if !defined __CELLOS_LV2__
#   include "net/sock.h"
extern net_protocol *prot;
#else
static int const prot = 0;
#endif

static VolumeWindow *volume_window;

//percent is 0..256
void tint_area(int x1, int y1, int x2, int y2, int r_to, int g_to, int b_to, int percent)
{
  int x,y;
  int cx1, cy1, cx2, cy2;
  screen->GetClip(cx1, cy1, cx2, cy2);
  if (x1<cx1) x1=cx1;
  if (y1<cy1) y1=cy1;
  if (x2>cx2-1) x2=cx2-1;
  if (y2>cy2-1) y2=cy2-1;
  if (x2<x1 || y2<y1) return ;

  percent=256-percent;

  screen->Lock();
  for (y=y1; y<=y2; y++)
  {
    uint8_t *sl=screen->scan_line(y)+x1;
    for (x=x1; x<=x2; x++,sl++)
    {
      uint8_t *paddr=(uint8_t *)pal->addr()+(*sl)*3;
      uint8_t r=((*(paddr++))-r_to)*percent/256+r_to;
      uint8_t g=((*(paddr++))-g_to)*percent/256+g_to;
      uint8_t b=((*(paddr++))-b_to)*percent/256+b_to;
      *sl=color_table->Lookup((r)>>3,(g)>>3,(b)>>3);
    }
  }
  screen->AddDirty(x1, y1, x2 + 1, y2 + 1);
  screen->Unlock();
}

void darken_area(int x1, int y1, int x2, int y2, int amount)
{
  int x,y;
  int cx1, cy1, cx2, cy2;
  screen->GetClip(cx1, cy1, cx2, cy2);
  if (x1<cx1) x1=cx1;
  if (y1<cy1) y1=cy1;
  if (x2>cx2-1) x2=cx2-1;
  if (y2>cy2-1) y2=cy2-1;
  if (x2<x1 || y2<y1) return ;

  screen->Lock();
  for (y=y1; y<=y2; y++)
  {
    uint8_t *sl=screen->scan_line(y)+x1;
    for (x=x1; x<=x2; x++,sl++)
    {
      uint8_t *paddr=(uint8_t *)pal->addr()+(*sl)*3;
      uint8_t r=(*(paddr++))*amount/256;
      uint8_t g=(*(paddr++))*amount/256;
      uint8_t b=(*(paddr++))*amount/256;
      *sl=color_table->Lookup((r)>>3,(g)>>3,(b)>>3);
    }
  }
  screen->AddDirty(x1, y1, x2 + 1, y2 + 1);
  screen->Unlock();
}

void dark_widget(int x1, int y1, int x2, int y2, int br, int dr, int amount)
{
  screen->AddDirty(x1, y1, x2 + 1, y2 + 1);
  screen->line(x1,y1,x1,y2,br);
  screen->line(x1+1,y1,x2,y1,br);
  screen->line(x2,y1+1,x2,y2,dr);
  screen->line(x1+1,y2,x2,y2,dr);
  darken_area(x1+1,y1+1,x2-1,y2-1,amount);
}

char *men_str(void *arg)
{
  switch (item_type(arg))
  {
    case L_STRING :
    { return lstring_value(arg); } break;
    case L_CONS_CELL :
    { return lstring_value(CAR(arg)); } break;
    default :
    {
      ((LObject *)arg)->Print();
      printf(" is not a valid menu option\n");
      exit(0);
    }
  }
  return NULL;
}

int menu(void *args, JCFont *font)             // reurns -1 on esc
{
  main_menu();
  char *title=NULL;
  if (!NILP(CAR(args)))
    title=lstring_value(CAR(args));
  Cell *def=lcar(lcdr(lcdr(args)));
  args=CAR(CDR(args));

  int options = ((LList *)args)->GetLength();
  int mh=(font->height()+1)*options+10,maxw=0;

  Cell *c=(Cell *)args;
  for (; !NILP(c); c=CDR(c))
  {
    if( strlen(men_str(CAR(c))) > (unsigned)maxw)
      maxw = strlen(men_str(CAR(c)));
  }

  int mw=(font->width())*maxw+20;
  int mx=screen->Size().x/2-mw/2,
      my=screen->Size().y/2-mh/2;


  screen->AddDirty(mx, my, mx + mw, my + mh);

  if (title)
  {
    int tl=strlen(title)*font->width();
    int tx=screen->Size().x/2-tl/2;
    dark_widget(tx-2,my-font->height()-4,tx+tl+2,my-2,wm->medium_color(),wm->dark_color(),180);
    font->put_string(screen,tx+1,my-font->height()-2,title,wm->bright_color());
  }

  dark_widget(mx,my,mx+mw-1,my+mh-1,wm->medium_color(),wm->dark_color(),200);


  int y=my+5;
  for (c=(Cell *)args; !NILP(c); c=CDR(c))
  {
    char *ms=men_str(CAR(c));
    font->put_string(screen,mx+10+1,y+1,ms,wm->black());
    font->put_string(screen,mx+10,y,ms,wm->bright_color());
    y+=font->height()+1;
  }

  wm->flush_screen();
  event ev;
  int choice=0,done=0;
  int bh=font->height()+3;
  image *save = new image(vec2i(mw - 2,bh));
  int color=128,cdir=50;

  time_marker *last_color_time=NULL;
  if (!NILP(def))
    choice=lnumber_value(def);
  do
  {
    wm->flush_screen();
    if (wm->event_waiting())
    {
      wm->get_event(ev);
      if (ev.type==EV_KEY)
      {
    switch (ev.key)
    {
      case JK_ESC :
      { choice=-1; done=1; } break;
      case JK_ENTER :
      { done=1; } break;
      case JK_DOWN :
      { if (choice<options-1)
        choice++;
      else choice=0;
      } break;
      case JK_UP :
      {
        if (choice>0)
        choice--;
        else choice=options-1;
      } break;
    }
      } else if (ev.type==EV_MOUSE_BUTTON && ev.mouse_button)
      {
    if (ev.mouse_move.x>mx && ev.mouse_move.x<mx+mw && ev.mouse_move.y>my &&
        ev.mouse_move.y<my+mh)
    {
      int msel=(ev.mouse_move.y-my)/(font->height()+1);
      if (msel>=options) msel=options-1;
      if (msel==choice)                    // clicked on already selected item, return it
        done=1;
      else choice=msel;                    // selects an item
    }
      }
    }

    time_marker cur_time;
    if (!last_color_time || (int)(cur_time.diff_time(last_color_time)*1000)>120)
    {
      if (last_color_time)
        delete last_color_time;
      last_color_time=new time_marker;

      int by1=(font->height()+1)*choice+my+5-2;
      int by2=by1+bh-1;

      screen->put_part(save,0,0,mx+1,by1,mx+mw-2,by2);
      tint_area(mx+1,by1,mx+mw-2,by2,63,63,63,color);

      char *cur=men_str(nth(choice,args));
      font->put_string(screen,mx+10+1,by1+3,cur,wm->black());
      font->put_string(screen,mx+10,by1+2,cur,wm->bright_color());
      screen->rectangle(mx+1,by1,mx+mw-2,by2,wm->bright_color());

      color+=cdir;

      if (color<12 || color>256)
      {
    cdir=-cdir;
    color+=cdir;
      }
      wm->flush_screen();
      save->put_image(screen,mx+1,by1);
    } else { Timer tmp; tmp.WaitMs(10); }

  } while (!done);
  if (last_color_time)
    delete last_color_time;
  delete save;
  the_game->draw(the_game->state==SCENE_STATE);

  if (choice!=-1)
  {
    void *val=nth(choice,args);
    if (item_type(val)==L_CONS_CELL)   // is there another value that the user want us to return?
      return lnumber_value(lcdr(val));
  }
  return choice;
}

static void create_volume_window()
{
    volume_window = new VolumeWindow();
    volume_window->inm->allow_no_selections();
    volume_window->inm->clear_current();
    volume_window->show();

    wm->grab_focus(volume_window);
    wm->flush_screen();

    while(volume_window)
    {
        event ev;

        do
        {
            wm->get_event(ev);
        }
        while(ev.type == EV_MOUSE_MOVE && wm->event_waiting());

        wm->flush_screen();

        if(ev.type == EV_CLOSE_WINDOW
                 || (ev.type == EV_KEY && ev.key == JK_ESC))
        {
            wm->close_window(volume_window);
            volume_window = NULL;
        }

        if(!volume_window)
            break;

        if(ev.type == EV_MESSAGE)
        {
            char const *s;

            switch(ev.message.id)
            {
            case ID_SFX_UP:
                sfx_volume += 16;
                if(sfx_volume > 127)
                    sfx_volume = 127;
                volume_window->draw_sfx_vol();
                s = "sfx/ambtech1.wav";
                if(sound_avail & SFX_INITIALIZED)
                    cache.sfx(cache.reg(s, s, SPEC_EXTERN_SFX, 1))
                        ->play(sfx_volume);
                break;
            case ID_SFX_DOWN:
                sfx_volume -= 16;
                if(sfx_volume < 0)
                    sfx_volume = 0;
                volume_window->draw_sfx_vol();
                s = "sfx/ambtech1.wav";
                if(sound_avail & SFX_INITIALIZED)
                    cache.sfx(cache.reg(s, s, SPEC_EXTERN_SFX, 1))
                        ->play(sfx_volume);
                break;

            case ID_MUSIC_UP:
                music_volume += 16;
                if(music_volume > 127)
                    music_volume = 127;
                volume_window->draw_music_vol();
                if(current_song)
                    current_song->set_volume(music_volume);
                break;
            case ID_MUSIC_DOWN:
                music_volume -= 16;
                if(music_volume < 0)
                    music_volume = 0;
                volume_window->draw_music_vol();
                if(current_song)
                    current_song->set_volume(music_volume);
                break;
            }
        }
    }

    wm->close_window(volume_window);
}

void save_difficulty()
{
  FILE *fp=open_FILE("hardness.lsp","wb");
  if (!fp)
    dprintf("Unable to write to file hardness.lsp\n");
  else
  {
    fprintf(fp,"(setf difficulty '");
    if (DEFINEDP(symbol_value(l_difficulty)))
    {
      if (symbol_value(l_difficulty)==l_extreme)
        fprintf(fp,"extreme)\n");
      else if (symbol_value(l_difficulty)==l_hard)
        fprintf(fp,"hard)\n");
      else if (symbol_value(l_difficulty)==l_easy)
        fprintf(fp,"easy)\n");
      else
        fprintf(fp,"medium)\n");
    } else
       fprintf(fp,"medium)\n");
    fclose(fp);
  }
}

void fade_out(int steps);
void fade_in(image *im, int steps);


void show_sell(int abortable)
{
  LSymbol *ss = LSymbol::FindOrCreate("sell_screens");
  if (!DEFINEDP(ss->GetValue()))
  {
    int sp=current_space;
    current_space=PERM_SPACE;
//    char *prog="((\"art/help.spe\" . \"sell2\")(\"art/help.spe\" . \"sell4\")(\"art/help.spe\" . \"sell3\")(\"art/fore/endgame.spe\" . \"credit\"))";
//    char *prog="((\"art/fore/endgame.spe\" . \"credit\") (\"art/help.spe\" . \"sell6\"))";
    char const *prog = "((\"art/fore/endgame.spe\" . \"credit\"))";
    ss->SetValue(LObject::Compile(prog));
    current_space=sp;
  }

  if (DEFINEDP(ss->GetValue()))
  {
    image blank(vec2i(2, 2)); blank.clear();
    wm->set_mouse_shape(blank.copy(),0,0);      // don't show mouse

    LObject *tmp = (LObject *)ss->GetValue();
    int quit=0;
    while (tmp && !quit)
    {
      int im=cache.reg_object("art/help.spe",CAR(tmp),SPEC_IMAGE,1);
      fade_in(cache.img(im),16);

      event ev;
      do
      { wm->flush_screen();
    wm->get_event(ev);
      } while (ev.type!=EV_KEY);
      if (ev.key==JK_ESC && abortable)
        quit=1;
      fade_out(16);
      tmp = (LObject *)CDR(tmp);
    }
    wm->set_mouse_shape(cache.img(c_normal)->copy(),1,1);
  }
}


void menu_handler(event &ev, InputManager *inm)
{
  switch (ev.type)
  {
    case EV_MESSAGE :
    {
      switch (ev.message.id)
      {
    case ID_LIGHT_OFF :
    if (!volume_window)
    {
      gamma_correct(pal,1);
    } break;
    case ID_RETURN :
    if (!volume_window)
    {
      the_game->set_state(RUN_STATE);
    } break;
    case ID_START_GAME :
    if (!volume_window)
    {
      reset_checkpoint_slot();
      the_game->load_level(level_file, true);
      the_game->set_state(RUN_STATE);
      view *v;
      for (v=player_list; v; v=v->next)
        if (v->focus)
          v->reset_player();

    } break;


        case ID_LOAD_PLAYER_GAME :
    if (!volume_window)
    {
      int got_level=load_game(0,symbol_str("LOAD"));
      the_game->reset_keymap();
      if (got_level && !load_checkpoint_slot(got_level))
        text_notice("LOAD FAILED", "This save is no longer readable.");
    } break;


    case ID_VOLUME :
    if (!volume_window)
    { create_volume_window(); } break;

    case ID_MEDIUM :
    {
      l_difficulty->SetValue(l_medium);
      save_difficulty();
    } break;
    case ID_HARD :
    {
      l_difficulty->SetValue(l_hard);
      save_difficulty();
    } break;
    case ID_EXTREME :
    {
      l_difficulty->SetValue(l_extreme);
      save_difficulty();
    } break;
    case ID_EASY :
    {
      l_difficulty->SetValue(l_easy);
      save_difficulty();
    } break;

    case ID_NETWORKING :
    {
      if (!volume_window)
      {
        net_configuration *cfg=new net_configuration;
        if (cfg->input())
        {
          if (main_net_cfg) delete main_net_cfg;
          main_net_cfg=cfg;
        } else delete cfg;
        the_game->draw(0);
        inm->redraw();
      }
    } break;

    case ID_SHOW_SELL :
    if (!volume_window)
    {
      show_sell(1);
      draw_title_screen();
      inm->redraw();
      fade_in(NULL,8);
      wm->flush_screen();

    } break;
      } break;
    } break;
    case EV_CLOSE_WINDOW :
    {
      if (ev.window==volume_window)
      { wm->close_window(volume_window); volume_window=NULL; }
    } break;
  }
}

void *current_demo=NULL;

static ico_button *load_icon(int num, int id, int x, int y, int &h, ifield *next, char const *key)
{
  char name[20];
  char const *base = "newi";
  int a,b,c;
  sprintf(name,"%s%04d.pcx",base,num*3+1);
  a=cache.reg("art/icons.spe",name,SPEC_IMAGE,1);

  sprintf(name,"%s%04d.pcx",base,num*3+2);
  b=cache.reg("art/icons.spe",name,SPEC_IMAGE,1);

  sprintf(name,"%s%04d.pcx",base,num*3+3);
  c=cache.reg("art/icons.spe",name,SPEC_IMAGE,1);

  h=cache.img(a)->Size().y;

  return new ico_button(x,y,id,b,b,a,c,next,-1,key);
}

ico_button *make_default_buttons(int x,int &y, ico_button *append_list)
{
  int h;
  int diff_on;

  if (DEFINEDP(symbol_value(l_difficulty)))
  {
    if (symbol_value(l_difficulty)==l_extreme)
      diff_on=3;
    else if (symbol_value(l_difficulty)==l_hard)
      diff_on=2;
    else if (symbol_value(l_difficulty)==l_easy)
      diff_on=0;
    else
      diff_on=1;
  } else  diff_on=3;


  ico_button *start=load_icon(0,ID_START_GAME,x,y,h,NULL,"ic_start");                         y+=h;

  ico_switch_button *set=NULL;
  if (!main_net_cfg || (main_net_cfg->state!=net_configuration::SERVER && main_net_cfg->state!=net_configuration::CLIENT))
  {
    set=new ico_switch_button(x,y,ID_NULL,diff_on,
                         load_icon(3,ID_EASY,x,y,h,
                         load_icon(8,ID_MEDIUM,x,y,h,
                             load_icon(9,ID_HARD,x,y,h,
                                     load_icon(10,ID_EXTREME,x,y,h,NULL,"ic_extreme"),
                                  "ic_hard"),"ic_medium"),"ic_easy"),NULL);         y+=h;

  }

  ico_button *color=load_icon(4,ID_LIGHT_OFF,x,y,h,NULL,"ic_gamma");                          y+=h;
  ico_button *volume=load_icon(5,ID_VOLUME,x,y,h,NULL,"ic_volume");                            y+=h;
  ico_button *sell=NULL;

  if (prot)
  {
    sell=load_icon(11,ID_NETWORKING,x,y,h,NULL,"ic_networking");
    y+=h;
  } else
  {
    sell=load_icon(2,ID_SHOW_SELL,x,y,h,NULL,"ic_sell");
    y+=h;
  }
  ico_button *quit=load_icon(6,ID_QUIT,x,y,h,NULL,"ic_quit");                                y+=h;

  if (set)
  {
    start->next=set;
    set->next=color;
  }
  else start->next=color;


  color->next=volume;
  if (sell)
  {
    volume->next=sell;
    sell->next=quit;
  } else volume->next=quit;

  ico_button *list=append_list;

  if (append_list)
  {
    while (append_list->next)
      append_list=(ico_button *)append_list->next;
    append_list->next=start;
  } else list=start;

  return list;
}


ico_button *make_conditional_buttons(int x,int &y)
{
  ico_button *start_list=NULL;
  int h;
  if (current_level)       // should we include a return icon?
  {
    start_list=load_icon(7,ID_RETURN,x,y,h,NULL,"ic_return");                       y+=h;
  }


  ico_button *load;
  if (show_load_icon())
  { load= load_icon(1,ID_LOAD_PLAYER_GAME,x,y,h,NULL,"ic_load");                     y+=h; }
  else load=NULL;

  if (start_list) start_list->next=load;
  else start_list=load;

  return start_list;
}

struct MenuTextScope {
    ~MenuTextScope() { platform_end_menu_text(); }
};

static TextMenuAction run_menu(TextMenu &menu, const char *section, const char *hint, uint64_t deadline)
{
    ControllerMenuScope controller_menu;
    MenuTextScope menu_text;
    for (;;) {
        if (!deadline && controller_take_warning())
            text_notice("GAMEPAD WARNING", "This controller does not report two sticks. Controller gameplay is disabled because movement and aiming need separate sticks. Use keyboard and mouse, or connect a dual-stick controller. Menu navigation still works.");
        const auto now = SDL_GetTicks();
        if (deadline && now >= deadline) return {-1};
        const auto remaining = deadline ? (deadline - now + 999) / 1000 : 0;
        const std::string countdown = deadline ? "Reverting in " + std::to_string(remaining) + " s   Esc reverts now" : "";
        menu.draw(*screen, *big_font, *pal, "ABUSE MODERN", section, deadline ? countdown.c_str() : hint);
        wm->flush_screen();
        if (deadline) {
            const auto next_update = deadline - (remaining - 1) * 1000;
            while (!wm->event_waiting() && SDL_GetTicks() < next_update) SDL_Delay(10);
            if (SDL_GetTicks() >= next_update) continue;
        }
        event ev;
        wm->get_event(ev);
        if (ev.type == EV_KEY && ev.key == JK_F11) {
            if (!deadline) confirm_fullscreen_change();
            continue;
        }
        TextMenuAction action = menu.handle(ev);
        if (action.id) return action;
    }
}

TextMenuAction run_text_menu(TextMenu &menu, const char *section, const char *hint)
{
    return run_menu(menu, section, hint, 0);
}

template<class Apply, class Restore>
static void preview_display(Setting setting, int value, Apply apply, Restore restore)
{
    if (!apply()) {
        text_notice("DISPLAY ERROR", "Could not preview the display change.");
        return;
    }
    TextMenu menu;
    menu.items = {{1, "Keep changes", ""}, {2, "Revert", ""}};
    const auto result = run_menu(menu, "KEEP DISPLAY?", "", SDL_GetTicks() + 12000);
    const bool keep = result.id == 1;
    if (keep && save_setting(setting, value)) return;
    if (!restore())
        text_notice("DISPLAY ERROR", "Could not restore the previous display. Restart to use the saved settings.");
    else if (keep)
        text_notice("SAVE FAILED", "Could not save the display setting. Previous display restored.");
}

void confirm_fullscreen_change()
{
    const bool fullscreen = (SDL_GetWindowFlags(platform_window()) & SDL_WINDOW_FULLSCREEN) != 0;
    preview_display(Setting::Fullscreen, !fullscreen,
                    [&] { return platform_set_fullscreen(!fullscreen, false); },
                    [&] { return platform_set_fullscreen(fullscreen, false); });
    the_game->reset_keymap();
}

void text_notice(const char *title, const std::string &message)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < message.size()) {
        size_t end = std::min(start + 37, message.size());
        if (end < message.size()) { auto space = message.rfind(' ', end); if (space > start && space != std::string::npos) end = space; }
        lines.push_back(message.substr(start, end - start));
        start = end; while (start < message.size() && message[start] == ' ') ++start;
    }
    int page = 0;
    for (;;) {
        TextMenu menu;
        for (int i = page * 5; i < std::min(int(lines.size()), page * 5 + 5); ++i)
            menu.items.push_back({0, lines[i], "", false});
        if (page) menu.items.push_back({2, "Previous page", ""});
        menu.items.push_back({1, (page + 1) * 5 < int(lines.size()) ? "Next page" : "OK", ""});
        menu.selected = int(menu.items.size()) - 1;
        auto action = run_text_menu(menu, title, "ENTER CONTINUES   ESC CLOSES");
        if (action.id == -1) return;
        if (action.id == 2) --page;
        else if ((page + 1) * 5 < int(lines.size())) ++page;
        else return;
    }
}

bool text_entry(const char *title, const char *label, std::string &value, size_t maximum)
{
    ControllerMenuScope controller_menu;
    MenuTextScope menu_text;
    TextEntry entry(value, maximum);
    TextMenu menu;
    for (;;) {
        menu.items = {{0, label, (entry.value.size() > 19 ? "<" + entry.value.substr(entry.value.size() - 19) : entry.value) + "_", false}, {1, "Type using keyboard", "", false},
                      {2, "Enter: apply", "", false}, {3, "Esc: cancel", "", false}};
        menu.draw(*screen, *big_font, *pal, "ABUSE MODERN", title, "BACKSPACE DELETES   ENTER APPLIES");
        wm->flush_screen();
        event input; wm->get_event(input);
        const auto result = entry.handle(input);
        if (result == TextEntry::Result::Cancelled) return false;
        if (result == TextEntry::Result::Accepted) { value = entry.value; return true; }
    }
}

int text_confirm_quit()
{
    image *background = screen->copy();
    TextMenu menu;
    menu.items = {
        {ID_CANCEL, current_level ? "Keep playing" : "Return to menu", ""},
        {ID_QUIT_OK, "Quit to desktop", ""}
    };
    TextMenuAction action = run_text_menu(menu, "QUIT GAME?", "ENTER CONFIRMS   ESC GOES BACK");
    background->put_image(screen, 0, 0);
    delete background;
    the_game->reset_keymap();
    wm->flush_screen();
    return action.id == ID_QUIT_OK;
}

int current_difficulty_index()
{
    if (l_difficulty->GetValue() == l_easy) return 0;
    if (l_difficulty->GetValue() == l_medium) return 1;
    if (l_difficulty->GetValue() == l_hard) return 2;
    return 3;
}

static void text_controls()
{
    TextMenu menu;
    std::string hint = "ENTER TO REBIND   ESC BACK";
    for (;;) {
        menu.items.clear();
        for (int i = 0; i < control_count; ++i)
            menu.items.push_back({i + 1, control_label(ControlAction(i)), control_key_name(control_binding(ControlAction(i)))});
        menu.items.push_back({100, "Restore defaults", ""});
        menu.items.push_back({101, "Back", ""});
        const auto action = run_text_menu(menu, "KEY BINDINGS", hint.c_str());
        if (action.id == -1 || action.id == 101) return;
        const keys_struct previous = keys;
        const auto old_controller_options = controller_options;
        if (action.id == 100) { control_reset(); controller_options = old_controller_options; }
        else {
            ControllerMenuScope controller_menu;
            TextMenu prompt;
            prompt.items = {{0, control_label(ControlAction(action.id - 1)), "Press a key", false},
                            {0, "Esc cancels", "", false}, {0, "Conflicts swap keys", "", false}};
            bool cancel = false;
            for (;;) {
                prompt.draw(*screen, *big_font, *pal, "ABUSE MODERN", "REBIND KEY", "PRESS A KEY   ESC CANCELS");
                wm->flush_screen();
                event key; wm->get_event(key);
                if (key.type != EV_KEY) continue;
                if (key.key == JK_ESC) { cancel = true; break; }
                if (control_rebind(ControlAction(action.id - 1), key.key)) break;
                prompt.items[1].label = "Reserved key; try another";
            }
            if (cancel) continue;
        }
        if (!controls_save(std::string(get_save_filename_prefix()) + "controls.cfg")) {
            keys = previous; controller_options = old_controller_options;
            text_notice("SAVE FAILED", "Could not save controls. Previous bindings have been restored.");
        }
        get_key_bindings(); the_game->reset_keymap();
    }
}

static void text_controller()
{
    TextMenu menu;
    for (;;) {
        menu.items = {{0, "Device", controller_name(), false},
                      {0, "Gameplay", controller_requires_two_sticks() ? "Needs two sticks" : "Dual-stick layout", false},
                      {1, "Controller", controller_options.enabled ? "On  < >" : "Off  < >", true, true},
                      {2, "Dead zone", std::to_string(controller_options.deadzone * 100 / 32767) + "%  < >", true, true},
                      {3, "Button guide", ""}, {4, "Back", ""}};
        const auto action = run_text_menu(menu, "CONTROLLER", "LEFT / RIGHT ADJUST   ESC BACK");
        if (action.id == -1 || action.id == 4) return;
        if (action.id == 3) {
            text_notice("GAMEPAD GUIDE", "Left stick / D-pad: move. South: jump. East: use / crouch. Right stick: aim. Right trigger: fire. Left trigger / West: special. Bumpers: weapons. Start: pause / back. Menus: D-pad, South confirms, East cancels.");
            continue;
        }
        const auto previous = controller_options;
        if (action.id == 1) {
            if (!controller_options.enabled && controller_requires_two_sticks()) {
                text_notice("TWO STICKS NEEDED", "This controller cannot provide both movement and aiming. Use keyboard and mouse or a dual-stick controller.");
                continue;
            }
            controller_options.enabled = !controller_options.enabled;
        }
        else if (action.id == 2) controller_options.deadzone = std::clamp(controller_options.deadzone + action.direction * 1600, 2000, 16000);
        if (!controls_save(std::string(get_save_filename_prefix()) + "controls.cfg")) {
            controller_options = previous;
            text_notice("SAVE FAILED", "Could not save controller settings. Previous values have been restored.");
        }
        the_game->reset_keymap();
    }
}

static void text_enhancements()
{
    TextMenu menu;
    for (;;) {
        menu.items = {
            {1, "Modern HUD", game_settings.modern_hud ? "[x]" : "[ ]", true, true},
            {2, "Enhanced Lighting", game_settings.enhanced_lighting ? "[x]" : "[ ]", true, true},
            {3, "Checkpoint Feedback", game_settings.checkpoint_feedback ? "[x]" : "[ ]", true, true},
            {4, "Smooth pixels", game_settings.smooth_pixels ? "[x]" : "[ ]", true, true},
            {5, "Enhanced projectiles", game_settings.enhanced_projectiles ? "[x]" : "[ ]", true, true},
            {6, "Directional sound cues", game_settings.directional_sound ? "[x]" : "[ ]", true, true},
            {ID_CANCEL, "Back", ""}
        };
        const auto action = run_text_menu(menu, "ENHANCEMENTS", "ENTER TO TOGGLE   ESC BACK");
        if (action.id == -1 || action.id == ID_CANCEL) return;
        if (action.id == 4) {
            if (!platform_set_smooth_pixels(!game_settings.smooth_pixels))
                text_notice("DISPLAY CHANGE FAILED", SDL_GetError());
            continue;
        }
        Setting setting;
        bool enabled;
        switch (action.id) {
        case 1: setting = Setting::ModernHud; enabled = game_settings.modern_hud; break;
        case 2: setting = Setting::EnhancedLighting; enabled = game_settings.enhanced_lighting; break;
        case 3: setting = Setting::CheckpointFeedback; enabled = game_settings.checkpoint_feedback; break;
        case 5: setting = Setting::EnhancedProjectiles; enabled = game_settings.enhanced_projectiles; break;
        case 6: setting = Setting::DirectionalSound; enabled = game_settings.directional_sound; break;
        default: continue;
        }
        if (!save_setting(setting, !enabled)) {
            text_notice("SAVE FAILED", "Could not save enhancement setting.");
        } else if (setting == Setting::ModernHud) {
            recalc_local_view_space();
        }
    }
}

static void text_settings()
{
    constexpr int fullscreen_action = 9001;
    const char *names[] = {"Easy", "Normal", "Hard", "Extreme"};
    LSymbol *levels[] = {l_easy, l_medium, l_hard, l_extreme};
    const bool local = !main_net_cfg ||
        (main_net_cfg->state != net_configuration::SERVER && main_net_cfg->state != net_configuration::CLIENT);
    TextMenu menu;
    auto *auto_equip = LSymbol::FindOrCreate("change_on_pickup");
    for (;;) {
        const auto volume = [](int value) { return std::to_string((value * 100 + 63) / 127) + "%  < >"; };
        const bool auto_equip_enabled = auto_equip->GetValue() && DEFINEDP(auto_equip->GetValue());
        menu.items = {
            {ID_DIFFICULTY, "Difficulty", std::string(names[current_difficulty_index()]) + "  < >", local, true},
            {ID_SFX_UP, "Sound effects", volume(sfx_volume), (sound_avail & SFX_INITIALIZED) != 0, true},
            {ID_MUSIC_UP, "Music", volume(music_volume), (sound_avail & MUSIC_INITIALIZED) != 0, true},
            {fullscreen_action, "Display mode", SDL_GetWindowFlags(platform_window()) & SDL_WINDOW_FULLSCREEN ? "Fullscreen  < >" : "Windowed  < >", true, true},
            {9005, "View resolution", view_label(game_settings.view_resolution) + "  < >", true, true},
            {9004, "Window scale", std::to_string(game_settings.scale) + "x  < >",
                !(SDL_GetWindowFlags(platform_window()) & SDL_WINDOW_FULLSCREEN), true},
            {ID_LIGHT_OFF, "Brightness", "Adjust..."},
            {9006, "Mouse wheel", game_settings.wheel_weapons ? "Weapons  < >" : "Off  < >", true, true},
            {9008, "Auto-equip pickups", auto_equip_enabled ? "On  < >" : "Off  < >", local, true},
            {9002, "Key bindings", ""},
            {9003, "Controller", ""},
            {9007, "Enhancements", ""},
            {ID_CANCEL, "Back", ""}
        };
        TextMenuAction action = run_text_menu(menu, "SETTINGS", "LEFT / RIGHT ADJUST   ESC BACK");
        if (action.id == -1 || action.id == ID_CANCEL) return;
        if (action.id == 9002) text_controls();
        else if (action.id == 9003) text_controller();
        else if (action.id == 9007) text_enhancements();
        else if (action.id == ID_DIFFICULTY) {
            const int value = (current_difficulty_index() + action.direction + 4) % 4;
            if (save_setting(Setting::Difficulty, value)) l_difficulty->SetValue(levels[value]);
            else text_notice("SAVE FAILED", "Could not save difficulty. Previous value retained.");
        }
        else if (action.id == ID_SFX_UP) {
            const int value = std::clamp(sfx_volume + action.direction * 8, 0, 127);
            if (save_setting(Setting::Effects, value)) {
                sfx_volume = value;
                const char *sample = "sfx/ambtech1.wav";
                cache.sfx(cache.reg(sample, sample, SPEC_EXTERN_SFX, 1))->play(sfx_volume);
            } else text_notice("SAVE FAILED", "Could not save effects volume. Previous value retained.");
        } else if (action.id == ID_MUSIC_UP) {
            const int value = std::clamp(music_volume + action.direction * 8, 0, 127);
            if (save_setting(Setting::Music, value)) {
                music_volume = value;
                if (current_song) current_song->set_volume(music_volume);
            } else text_notice("SAVE FAILED", "Could not save music volume. Previous value retained.");
        } else if (action.id == fullscreen_action) {
            confirm_fullscreen_change();
        } else if (action.id == 9005) {
            const int previous = game_settings.view_resolution;
            const int value = (previous + action.direction + 4) % 4;
            preview_display(Setting::ViewResolution, value,
                            [&] { return the_game->set_view_resolution(value, false); },
                            [&] { return the_game->set_view_resolution(previous, false); });
        } else if (action.id == 9006) {
            if (!save_setting(Setting::WheelWeapons, !game_settings.wheel_weapons))
                text_notice("SAVE FAILED", "Could not save mouse wheel setting. Previous value retained.");
        } else if (action.id == 9004) {
            const int value = std::clamp(game_settings.scale + action.direction, 1, 8);
            int old_width, old_height; SDL_GetWindowSize(platform_window(), &old_width, &old_height);
            preview_display(Setting::Scale, value,
                            [&] { return SDL_SetWindowSize(platform_window(), xres * value, yres * value); },
                            [&] { return SDL_SetWindowSize(platform_window(), old_width, old_height); });
        } else if (action.id == 9008) {
            if (save_setting(Setting::AutoEquipPickups, !auto_equip_enabled))
                auto_equip->SetValue(auto_equip_enabled ? nullptr : true_symbol);
            else text_notice("SAVE FAILED", "Could not save the weapon preference.");
        } else if (action.id == ID_LIGHT_OFF) gamma_correct(pal, 1);
    }
}

void main_menu()
{
    constexpr int restart_checkpoint = 9002;
    constexpr int pause_map = 9003;
    TextMenu menu;
    // Retain the existing action handlers for savegames, networking and credits.
    InputManager legacy(screen, NULL);
    for (;;) {
        menu.items.clear();
        if (current_level) menu.items.push_back({ID_RETURN, "Resume game", ""});
        if (pause_map_available())
            menu.items.push_back({pause_map, "Map", ""});
        if (current_level && (!main_net_cfg || main_net_cfg->state == net_configuration::SINGLE_PLAYER)) {
            const bool checkpoint = last_checkpoint_slot() != 0;
            menu.items.push_back({restart_checkpoint, "Restart checkpoint", checkpoint ? "" : "No checkpoint", checkpoint});
        }
        menu.items.push_back({ID_START_GAME, "New game", ""});
        const bool saves = show_load_icon();
        menu.items.push_back({ID_LOAD_PLAYER_GAME, "Load game", saves ? "" : "No saves", saves});
        menu.items.push_back({ID_VOLUME, "Settings", ""});
        if (prot) menu.items.push_back({ID_NETWORKING, "Multiplayer", ""});
        menu.items.push_back({ID_SHOW_SELL, "Credits", ""});
        menu.items.push_back({ID_QUIT, "Quit game", ""});
        TextMenuAction action = run_text_menu(menu, current_level ? "PAUSED" : "MAIN MENU",
                                             "ARROWS / MOUSE   ENTER TO SELECT");
        if (action.id == -1) action.id = current_level ? ID_RETURN : ID_QUIT;
        if (action.id == ID_QUIT) {
            if (text_confirm_quit()) { the_game->end_session(); return; }
        } else if (action.id == pause_map) {
            show_pause_map(*current_level, *player_list->focus);
        } else if (action.id == restart_checkpoint) {
            TextMenu confirm;
            confirm.items = {{ID_CANCEL, "Cancel", ""}, {restart_checkpoint, "Restart checkpoint", ""}};
            const auto result = run_text_menu(confirm, "RESTART?", "UNSAVED PROGRESS WILL BE LOST");
            if (result.id == restart_checkpoint) {
                if (load_checkpoint_slot(last_checkpoint_slot())) {
                    controller_suppress_until_neutral();
                    return;
                }
                text_notice("LOAD FAILED", "The last checkpoint is no longer readable.");
            }
        } else if (action.id == ID_VOLUME) text_settings();
        else {
            event message(action.id, NULL);
            menu_handler(message, &legacy);
            if (the_game->state == RUN_STATE || (main_net_cfg && main_net_cfg->restart_state())) {
                controller_suppress_until_neutral();
                return;
            }
        }
    }
}
