#pragma once
#include <SDL3/SDL.h>

// SDL owns presentation; gameplay continues using the original indexed framebuffer.
SDL_Window *platform_window();
SDL_Renderer *platform_renderer();
void platform_mouse_position(float window_x, float window_y, int &game_x, int &game_y);
void platform_warp_mouse(int game_x, int game_y);
bool platform_screenshot(const char *path);
bool platform_begin_menu_text();
void platform_end_menu_text();
void platform_menu_glyph(int x, int y, int width, int height, const Uint8 *mask, Uint8 color);

bool platform_set_fullscreen(bool enabled, bool persist = true);

bool platform_set_view_resolution(int preset, bool persist = true);
bool platform_set_smooth_pixels(bool enabled);
