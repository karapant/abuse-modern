/*
 *  Abuse - dark 2D side-scrolling platform game
 *  Copyright (c) 2001 Anthony Kruize <trandor@labyrinth.net.au>
 *  Copyright (c) 2005-2011 Sam Hocevar <sam@hocevar.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "common.h"
#include "filter.h"
#include "video.h"
#include "image.h"
#include "setup.h"
#include "platform.h"
#include "preferences.h"

image *screen = nullptr;
int xres, yres;
extern palette *lastl;
extern flags_struct flags;
static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture *texture = nullptr;
static std::vector<Uint8> indexed;
static std::vector<Uint32> rgba;
static Uint32 colors[256] = {};

static void video_error()
{
    fprintf(stderr, "Video: %s\n", SDL_GetError());
    exit(EXIT_FAILURE);
}

SDL_Window *platform_window() { return window; }
SDL_Renderer *platform_renderer() { return renderer; }

bool platform_set_fullscreen(bool enabled)
{
    const bool previous = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(window, enabled)) return false;
    if (!save_setting(Setting::Fullscreen, enabled)) {
        SDL_SetWindowFullscreen(window, previous);
        SDL_SetError("Cannot save fullscreen preference; previous value restored");
        return false;
    }
    flags.fullscreen = enabled;
    return true;
}

bool platform_set_view_resolution(int preset)
{
    if (preset < 0 || preset > 3 || !screen || !renderer) return false;
    const int width = view_width(preset), height = view_height(preset);
    SDL_Texture *next = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!next) return false;
    if (!SDL_SetTextureScaleMode(next, SDL_SCALEMODE_NEAREST) ||
        !SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE) ||
        !SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_DestroyTexture(next); return false;
    }
    if (!save_setting(Setting::ViewResolution, preset)) {
        SDL_SetRenderLogicalPresentation(renderer, xres, yres, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_DestroyTexture(next); return false;
    }
    // Keep the image object stable: window managers and cursor sprites retain its address.
    screen->m_special->ClearDirties();
    screen->SetSize(vec2i(width, height));
    screen->m_special->Resize(vec2i(width, height));
    screen->clear();
    xres = width; yres = height;
    indexed.assign(size_t(width) * height, 0);
    rgba.assign(indexed.size(), colors[0]);
    SDL_DestroyTexture(texture); texture = next;
    screen->AddDirty(0, 0, width, height);
    return true;
}

void set_mode(int, int, char **)
{
    SDL_WindowFlags options = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (flags.fullscreen) options |= SDL_WINDOW_FULLSCREEN;
    window = SDL_CreateWindow("Abuse Modern", flags.xres, flags.yres, options);
    if (!window) video_error();
    if (!flags.fullscreen) {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        const int centered = display ? SDL_WINDOWPOS_CENTERED_DISPLAY(display) : SDL_WINDOWPOS_CENTERED;
        if (!SDL_SetWindowPosition(window, centered, centered)) video_error();
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) video_error();
    // Keep presentation from introducing waits into the inherited simulation loop.
    SDL_SetRenderVSync(renderer, 0);
    if (!SDL_SetRenderLogicalPresentation(renderer, xres, yres, SDL_LOGICAL_PRESENTATION_LETTERBOX))
        video_error();
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, xres, yres);
    if (!texture || !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) video_error();
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    indexed.assign(size_t(xres) * yres, 0);
    rgba.resize(indexed.size());
    screen = new image(vec2i(xres, yres), nullptr, 2);
    screen->clear();
    SDL_HideCursor();
    SDL_SetWindowMouseGrab(window, flags.grabmouse != 0);
    int output_width = 0, output_height = 0;
    SDL_GetRenderOutputSize(renderer, &output_width, &output_height);
    printf("Video: SDL3 renderer %s, view %dx%d, output %dx%d (%s)\n",
           SDL_GetRendererName(renderer), xres, yres, output_width, output_height,
           flags.fullscreen ? "native fullscreen" : "windowed");
    update_dirty(screen);
}

void close_graphics()
{
    delete lastl; lastl = nullptr;
    delete screen; screen = nullptr;
    SDL_DestroyTexture(texture); texture = nullptr;
    SDL_DestroyRenderer(renderer); renderer = nullptr;
    SDL_DestroyWindow(window); window = nullptr;
    indexed.clear(); rgba.clear();
}

// Source bounds are exclusive. Dirty rectangles and put_image share this contract.
void put_part_image(image *im, int x, int y, int x1, int y1, int x2, int y2)
{
    if (x < 0) { x1 -= x; x = 0; }
    if (y < 0) { y1 -= y; y = 0; }
    const int w = std::min({x2 - x1, xres - x, im->Size().x - x1});
    const int h = std::min({y2 - y1, yres - y, im->Size().y - y1});
    if (w <= 0 || h <= 0 || x1 < 0 || y1 < 0) return;
    for (int row = 0; row < h; ++row)
        memcpy(indexed.data() + size_t(y + row) * xres + x,
               im->scan_line(y1 + row) + x1, size_t(w));
}

void update_window_done()
{
    if (!texture) return;
    // Convert the entire retained framebuffer so palette fades affect unchanged pixels.
    for (size_t i = 0; i < indexed.size(); ++i) rgba[i] = colors[indexed[i]];
    if (!SDL_UpdateTexture(texture, nullptr, rgba.data(), xres * sizeof(Uint32))) video_error();
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    if (!SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer, texture, nullptr, nullptr)
        || !SDL_RenderPresent(renderer)) video_error();
}

void palette::load()
{
    delete lastl;
    lastl = copy();
    for (int i = 0; i < 256; ++i)
        colors[i] = i < ncolors ? 0xff000000u | (Uint32(red(i)) << 16)
                     | (Uint32(green(i)) << 8) | Uint32(blue(i)) : 0xff000000u;
    update_window_done();
}
void palette::load_nice() { load(); }

void platform_mouse_position(float wx, float wy, int &gx, int &gy)
{
    float rx = 0, ry = 0;
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &rx, &ry);
    gx = std::clamp(int(rx), 0, xres - 1);
    gy = std::clamp(int(ry), 0, yres - 1);
}
void platform_warp_mouse(int gx, int gy)
{
    float wx = 0, wy = 0;
    SDL_RenderCoordinatesToWindow(renderer, float(gx), float(gy), &wx, &wy);
    SDL_WarpMouseInWindow(window, wx, wy);
}
bool platform_screenshot(const char *path)
{
    SDL_Surface *snapshot = SDL_CreateSurfaceFrom(xres, yres, SDL_PIXELFORMAT_ARGB8888,
                                                 rgba.data(), xres * sizeof(Uint32));
    if (!snapshot) return false;
    bool ok = SDL_SaveBMP(snapshot, path);
    SDL_DestroySurface(snapshot);
    return ok;
}
