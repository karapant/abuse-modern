#pragma once
#include <SDL3/SDL.h>

// SDL owns presentation; gameplay continues using the original indexed framebuffer.
SDL_Window *platform_window();
SDL_Renderer *platform_renderer();
void platform_mouse_position(float window_x, float window_y, int &game_x, int &game_y);
void platform_warp_mouse(int game_x, int game_y);
bool platform_screenshot(const char *path);
