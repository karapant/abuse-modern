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
static std::vector<Uint32> scaled_rgba;
static Uint32 colors[256] = {};

static void video_error()
{
    fprintf(stderr, "Video: %s\n", SDL_GetError());
    exit(EXIT_FAILURE);
}

SDL_Window *platform_window() { return window; }
SDL_Renderer *platform_renderer() { return renderer; }

static SDL_Texture *create_frame_texture(int width, int height, bool smooth)
{
    const int scale = smooth ? 2 : 1;
    SDL_Texture *frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, width * scale, height * scale);
    if (frame && (!SDL_SetTextureScaleMode(frame, SDL_SCALEMODE_NEAREST) ||
                  !SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_NONE))) {
        SDL_DestroyTexture(frame);
        return nullptr;
    }
    return frame;
}

bool platform_set_smooth_pixels(bool enabled)
{
    if (!renderer) return SDL_SetError("The renderer is unavailable");
    SDL_Texture *next = create_frame_texture(xres, yres, enabled);
    if (!next) return false;
    if (!save_setting(Setting::SmoothPixels, enabled)) {
        SDL_DestroyTexture(next);
        return SDL_SetError("Could not save the Smooth pixels setting");
    }
    SDL_DestroyTexture(texture);
    texture = next;
    update_window_done();
    return true;
}

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
    SDL_Texture *next = create_frame_texture(width, height, game_settings.smooth_pixels);
    if (!next) return false;
    if (!SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
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
    scaled_rgba.clear();
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
    texture = create_frame_texture(xres, yres, game_settings.smooth_pixels);
    if (!texture && game_settings.smooth_pixels) {
        fprintf(stderr, "Smooth pixels is unavailable for this view: %s\n", SDL_GetError());
        game_settings.smooth_pixels = 0;
        texture = create_frame_texture(xres, yres, false);
    }
    if (!texture) video_error();
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
    scaled_rgba.clear();
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

static Uint32 interpolate_subpixel(Uint32 center, Uint32 horizontal, Uint32 vertical)
{
    // Widen each pair of channels before adding so rounding cannot carry into another channel.
    constexpr Uint32 channels = 0x00ff00ff;
    constexpr Uint32 round = 0x00020002;
    const Uint32 rb = 2 * (center & channels) + (horizontal & channels) + (vertical & channels);
    const Uint32 ag = 2 * ((center >> 8) & channels) +
        ((horizontal >> 8) & channels) + ((vertical >> 8) & channels);
    return (((rb + round) >> 2) & channels) | ((((ag + round) >> 2) & channels) << 8);
}

static void scale_frame_2x()
{
    scaled_rgba.resize(rgba.size() * 4);
    const size_t stride = size_t(xres) * 2;
    for (int y = 0; y < yres; ++y) {
        const Uint32 *row = rgba.data() + size_t(y) * xres;
        const Uint32 *above = y > 0 ? row - xres : row;
        const Uint32 *below = y + 1 < yres ? row + xres : row;
        Uint32 *top = scaled_rgba.data() + size_t(y) * 2 * stride;
        Uint32 *bottom = top + stride;
        for (int x = 0; x < xres; ++x) {
            const Uint32 center = row[x];
            const Uint32 north = above[x], south = below[x];
            const Uint32 west = x > 0 ? row[x - 1] : center;
            const Uint32 east = x + 1 < xres ? row[x + 1] : center;
            // Follow matching diagonal corners and interpolate the other subpixels.
            const bool corner = north != south && west != east;
            *top++ = corner && north == west ? west : interpolate_subpixel(center, west, north);
            *top++ = corner && north == east ? east : interpolate_subpixel(center, east, north);
            *bottom++ = corner && south == west ? west : interpolate_subpixel(center, west, south);
            *bottom++ = corner && south == east ? east : interpolate_subpixel(center, east, south);
        }
    }
}

void update_window_done()
{
    if (!texture) return;
    // Convert the entire retained framebuffer so palette fades affect unchanged pixels.
    for (size_t i = 0; i < indexed.size(); ++i) rgba[i] = colors[indexed[i]];
    if (game_settings.smooth_pixels) scale_frame_2x();
    else scaled_rgba.clear();
    const bool smooth = !scaled_rgba.empty();
    const auto &frame = smooth ? scaled_rgba : rgba;
    const int stride = xres * (smooth ? 2 : 1);
    if (!SDL_UpdateTexture(texture, nullptr, frame.data(), stride * sizeof(Uint32))) video_error();
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
    const int scale = scaled_rgba.empty() ? 1 : 2;
    auto &frame = scale == 2 ? scaled_rgba : rgba;
    SDL_Surface *snapshot = SDL_CreateSurfaceFrom(xres * scale, yres * scale, SDL_PIXELFORMAT_ARGB8888,
                                                 frame.data(), xres * scale * sizeof(Uint32));
    if (!snapshot) return false;
    bool ok = SDL_SaveBMP(snapshot, path);
    SDL_DestroySurface(snapshot);
    return ok;
}
