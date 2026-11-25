/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 1995 Crack dot Com
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This software was released into the Public Domain. As with most public
 *  domain software, no warranty is made or implied by Crack dot Com, by
 *  Jonathan Clark, or by Sam Hocevar.
 */

#ifndef __GAME_HPP_
#define __GAME_HPP_

#include "loader2.h"

#include "image.h"
#include "video.h"
#include "event.h"
#include "fonts.h"
#include "items.h"
#include "jwindow.h"
#include "filter.h"
#include "level.h"
#include "cache.h"
#include "director.h"
#include "view.h"
#include "id.h"
#include "motion.h"
#include <array>

#define MAPFW                100
#define MAPFH                100
#define MAPBW                100
#define MAPBH                100

#define RUN_STATE 0
#define PAUSE_STATE 1
#define HELP_STATE 2
#define INTRO_START_STATE 3
#define INTRO_MORPH_STATE 4
#define JOY_CALB_STATE    5
#define MENU_STATE        6
#define SCENE_STATE       7
#define START_STATE       8
#define BLACK 0

/* Cleaned-up externs */
extern WindowManager *wm;
void set_aim_cursor();
void update_gameplay_cursor();
void set_aim_cursor_hotspot(int x, int y);
void draw_title_screen();


#define tile_type unsigned short
class Game;
extern Game *the_game;
extern int dev;
extern int morph_sel_frame_color;

extern char **start_argv;
extern int start_argc;
extern int32_t current_vxadd,current_vyadd;
extern int frame_panic,massive_frame_panic;
extern int demo_start,idle_ticks;

extern FILE *open_FILE(char const *filename, char const *mode);

class Game
{
private:
  JCFont *fnt;
  bool finished;
  int bg_top,fg_top;                         // in the fg/bg window which tile is at the top?
  int bright_color,med_color,dark_color,     // for boundaries and windows, etc
      morph_bright_color,morph_med_color,morph_dark_color;

  int32_t last_time,fps;
  char mapname[100],command[200],help_text[200];
  int refresh,mousex,mousey,help_text_frames;
  int has_joystick,no_delay;
  bool between_ticks = false;
  float last_render_ms = 0;
  struct PickupNotice {
    int weapon = -1, ammo = 0;
    bool acquired = false;
    uint64_t expires = 0;
  };
  struct SoundCue {
    vec2i position{0, 0};
    uint64_t expires = 0;
  };
  struct CombatFeedback {
    uint64_t hit_confirm_expires = 0;
    uint64_t hit_confirm_duration = 0;
    bool hit_confirm_kill = false;
    uint64_t damage_vignette_expires = 0;
    uint64_t damage_vignette_duration = 0;
    uint64_t fire_pulse_expires = 0;
    uint64_t fire_pulse_duration = 0;
    game_object *target_enemy = nullptr;
    int target_max_hp = 0;
    uint64_t target_bar_expires = 0;
    int switched_weapon = -1;
    uint64_t weapon_switch_expires = 0;
  };
  std::array<PickupNotice, 2> pickup_notices{};
  std::array<SoundCue, 3> sound_cues{};
  std::array<int, 4> cue_sound_ids{};
  CombatFeedback combat_feedback{};
  void draw_gameplay_feedback(view &v);


  Jwindow *top_menu,*joy_win,*last_input;
  JCFont *game_font;
  uint8_t keymap[512/8];

public :
  int key_down(int key) { return keymap[key/8]&(1<<(key%8)); }
  void set_key_down(int key, int x) { if (x) keymap[key/8]|=(1<<(key%8)); else keymap[key/8]&=~(1<<(key%8)); }
  void reset_keymap() { memset(keymap,0,sizeof(keymap)); }

  int nplayers;
  view *first_view,*old_view;
  int state,zoom;

  Game(int argc, char **argv);
  ~Game();

  void step();
  void show_help(char const *st);
  void note_weapon_pickup(view &v, int weapon, int ammo, bool acquired);
  void note_weapon_switch(view &v, int weapon);
  void note_hit_confirm(view &v, game_object *target = nullptr, bool kill = false);
  void note_player_damage(view &v, int amount);
  void note_player_fire(view &v);
  void draw_value(image *screen, int x, int y, int w, int h, int val, int max);
  unsigned char get_color(int x) { return x; }
  int done();
  void draw(int scene_mode=0);

  backtile *get_bg(int x) { if (x<0 || x>=nbacktiles || backtiles[x]<0)
                           return cache.backt(backtiles[BLACK]);
                           else return cache.backt(backtiles[x]); }
  foretile *get_fg(int x) { if (x<0 || x>=nforetiles || foretiles[x]<0)
                           return cache.foret(foretiles[BLACK]); else
               return cache.foret(foretiles[x]); }

  void ftile_on(int screenx, int screeny, int32_t &x, int32_t &y);
  void btile_on(int screenx, int screeny, int32_t &x, int32_t &y);
  bool set_view_resolution(int preset, bool persist = true);
  void toggle_delay();
  void set_delay(int on) { no_delay=!on; }
  void pan(int xv, int yv);

  void mouse_to_game(int32_t x, int32_t y, int32_t &gamex, int32_t &gamey, view *v=NULL);
  void game_to_mouse(int32_t gamex, int32_t gamey, view *which, int32_t &x, int32_t &y);
  view *view_in(int mousex, int mousey);

  int calc_speed();
  int ftile_width()  { return f_wid; }
  int ftile_height() { return f_hi; }

  int btile_width()  { return b_wid; }
  int btile_height() { return b_hi; }


  void put_fg(int x, int y, int type);
  void put_bg(int x, int y, int type);
  void draw_map(view *v, int frame = frames_per_tick);
  void dev_scroll();
  void put_block_fg(int x, int y, TransImage *im);
  void put_block_bg(int x, int y, image *im);


  int in_area(event &ev, int x1, int y1, int x2, int y2);
  void load_level(char const *name, bool new_game = false);
  void set_level(level *nl);
  void show_time();
  tile_type get_map_bg(int x, int y) { return current_level->get_bg(x,y); }
  tile_type get_map_fg(int x, int y) { return current_level->get_fg(x,y); }
  void end_session();
  void need_refresh() { refresh=1; }       // for development mode only
  palette *current_palette() { return pal; }

  void update_screen(int frame = frames_per_tick);
  bool drawing_between_ticks() const { return between_ticks; }
  void get_input();
  void do_intro();
  void joy_calb(event &ev);
  void menu_select(event &ev2);
  int can_morph_into(int type);
  void morph_into(int type);
  void set_state(int new_state);
  int game_over();
  void grow_views(int amount);
  void play_sound(int id, int vol, int32_t x, int32_t y);
  void request_level_load(char *name);
  void request_end();
} ;

extern int playing_state(int state);
#endif


