#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "common.h"
#include "image.h"
#include "palette.h"
#include "video.h"
#include "setup.h"
#include "platform.h"
#include "event.h"
#include "game.h"
#include "ui/textmenu.h"
#include <memory>
#include <filesystem>
#include "preferences.h"

extern unsigned char fnt6x13[192 * 104];

Game *the_game = nullptr;
void Game::show_help(const char *) {}
int get_key_binding(const char *name, int) { return !strcmp(name, "b4") ? JK_INSERT : JK_CTRL_R; }

flags_struct flags = {};
keys_struct keys = {};
extern int xres, yres;
static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError()); exit(1); }
}
static void pixel(int x, int y, Uint8 r, Uint8 g, Uint8 b)
{
    require(platform_screenshot("platform-test.bmp"), "save framebuffer");
    SDL_Surface *bmp = SDL_LoadBMP("platform-test.bmp");
    require(bmp != nullptr, "load framebuffer");
    Uint8 rr, gg, bb, aa;
    require(SDL_ReadSurfacePixel(bmp, x, y, &rr, &gg, &bb, &aa), "read framebuffer pixel");
    require(rr == r && gg == g && bb == b, "framebuffer pixel matches palette");
    SDL_DestroySurface(bmp);
}
int main(int argc, char **argv)
{
    require(SDL_Init(SDL_INIT_VIDEO), "initialize SDL3");
    xres = 320; yres = 200;
    flags.xres = 960; flags.yres = 600;
    set_mode(0, 0, nullptr);
    SDL_Rect display_bounds;
    int window_x, window_y;
    require(SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(platform_window()), &display_bounds), "read monitor work area");
    require(SDL_GetWindowPosition(platform_window(), &window_x, &window_y), "read initial window position");
    require(window_x == display_bounds.x + (display_bounds.w - flags.xres) / 2 &&
            window_y == display_bounds.y + (display_bounds.h - flags.yres) / 2,
            "startup window is centered in monitor work area");
    palette pal;
    pal.set(1, 255, 0, 0);
    pal.set(2, 0, 255, 0);
    pal.load();
    for (int y = 0; y < yres; ++y) memset(screen->scan_line(y), 1, xres);
    put_image(screen, 0, 0);
    update_window_done();
    pixel(0, 0, 255, 0, 0);
    pixel(319, 199, 255, 0, 0); // catch exclusive/inclusive edge regressions
    pal.set(1, 0, 0, 255);
    pal.load();
    pixel(319, 199, 0, 0, 255); // palette fades must update retained pixels
    image patch(vec2i(3, 3));
    patch.clear(2);
    put_part_image(&patch, -1, -1, 0, 0, 3, 3);
    update_window_done();
    pixel(0, 0, 0, 255, 0);
    pixel(1, 1, 0, 255, 0);
    pixel(2, 2, 0, 0, 255);
    int gx, gy;
    platform_mouse_position(480, 300, gx, gy);
    require(gx == 160 && gy == 100, "initial mouse coordinates");
    require(SDL_SetWindowSize(platform_window(), 1000, 1000), "resize window");
    SDL_SyncWindow(platform_window());
    SDL_PumpEvents();
    update_window_done();
    platform_mouse_position(500, 500, gx, gy);
    require(gx == 160 && gy == 100, "letterbox mouse coordinates after resize");
    platform_mouse_position(-10, -10, gx, gy);
    require(gx == 0 && gy == 0, "mouse clamps outside viewport");
    {
        event_handler handler(screen, &pal);
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        const auto next = [&]() {
            event ev;
            do { handler.get_event(ev); } while (ev.type == EV_SPURIOUS);
            return ev;
        };
        SDL_Event input = {};
        input.type = SDL_EVENT_KEY_DOWN;
        input.key.key = SDLK_A;
        input.key.scancode = SDL_SCANCODE_A;
        input.key.mod = SDL_KMOD_SHIFT;
        require(SDL_PushEvent(&input), "push key press");
        event ev = next();
        require(ev.type == EV_KEY && ev.key == 'A', "shifted key press");
        input.type = SDL_EVENT_KEY_UP;
        input.key.mod = 0;
        require(SDL_PushEvent(&input), "push key release");
        ev = next();
        require(ev.type == EV_KEYRELEASE && ev.key == 'A', "release matches press after shift lifted");
        input.type = SDL_EVENT_KEY_DOWN;
        require(SDL_PushEvent(&input), "push held movement");
        ev = next();
        require(ev.type == EV_KEY && ev.key == 'a', "movement key");
        input = {};
        input.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        require(SDL_PushEvent(&input), "push focus loss");
        ev = next();
        require(ev.type == EV_MOUSE_BUTTON && ev.mouse_button == 0, "focus loss releases buttons");
        ev = next();
        require(ev.type == EV_KEYRELEASE && ev.key == 'a', "focus loss releases held keys");
        input = {};
        input.type = SDL_EVENT_MOUSE_WHEEL;
        input.wheel.y = 1;
        require(SDL_PushEvent(&input), "push wheel");
        ev = next();
        require(ev.type == EV_KEY && ev.key == JK_UP, "wheel navigates menu up");
        ev = next();
        require(ev.type == EV_KEYRELEASE && ev.key == JK_UP, "wheel releases menu key");
        input = {};
        input.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        input.button.button = SDL_BUTTON_MIDDLE;
        input.button.x = 500; input.button.y = 500;
        require(SDL_PushEvent(&input), "push middle button press");
        input.type = SDL_EVENT_MOUSE_BUTTON_UP;
        require(SDL_PushEvent(&input), "push middle button release");
        ev = next();
        require(ev.type == EV_MOUSE_BUTTON && ev.mouse_button == (LEFT_BUTTON | RIGHT_BUTTON), "middle press sets both actions");
        ev = next();
        require(ev.type == EV_MOUSE_BUTTON && ev.mouse_button == 0, "middle release clears both actions");
    }
    {
        // Menu navigation must skip unavailable saves and use the visible row as its click target.
        image letters(vec2i(192, 104), fnt6x13);
        JCFont font(&letters);
        pal.defaults();
        palette *menu_palette = &pal;
        std::unique_ptr<palette> original_palette;
        if (argc > 1) {
            jFILE source(argv[1], "rb");
            require(!source.open_failure(), "open optional preview palette");
            spec_directory directory(&source);
            spec_entry *entry = directory.find(SPEC_PALETTE);
            require(entry != nullptr, "find optional preview palette");
            original_palette.reset(new palette(entry, &source));
            menu_palette = original_palette.get();
        }
        menu_palette->load();
        TextMenu menu;
        menu.items = {{1, "New game", ""}, {2, "Load game", "No saves", false},
                      {3, "Settings", ""}, {4, "Multiplayer", ""},
                      {5, "Credits", ""}, {6, "Quit game", ""}};
        menu.draw(*screen, font, *menu_palette, "ABUSE MODERN", "MAIN MENU", "ARROWS / MOUSE   ENTER TO SELECT");
        put_image(screen, 0, 0);
        update_window_done();
        require(platform_screenshot("menu-preview.bmp"), "save menu preview");
        event key;
        key.type = EV_KEY; key.key = JK_DOWN;
        menu.handle(key);
        key.key = JK_ENTER;
        require(menu.handle(key).id == 3, "menu skips unavailable load action");
        key.key = JK_LEFT;
        require(menu.handle(key).id == 0, "left does not activate a normal action");
        event click;
        click.type = EV_MOUSE_BUTTON; click.mouse_button = LEFT_BUTTON;
        click.mouse_move = {40, 52};
        require(menu.handle(click).id == 1, "first visible row activates with one click");
        click.mouse_move = {40, 69};
        require(menu.handle(click).id == 0, "disabled save row cannot activate");
        click.mouse_move = {40, 188};
        require(menu.handle(click).id == 0, "footer cannot activate an item");
        menu.items = {{1, "Difficulty", "Hard  < >", true, true},
                      {2, "Sound effects", "100%  < >", true, true},
                      {3, "Music", "75%  < >", true, true},
                      {4, "Fullscreen", "Off  < >", true, true},
                      {5, "Brightness", "Adjust..."}, {6, "Back", ""}};
        menu.selected = 1;
        menu.draw(*screen, font, *menu_palette, "ABUSE MODERN", "SETTINGS", "LEFT / RIGHT ADJUST   ESC BACK");
        key.key = JK_LEFT;
        const TextMenuAction adjustment = menu.handle(key);
        require(adjustment.id == 2 && adjustment.direction == -1, "left adjusts selected volume down");
        key.key = JK_ESC;
        require(menu.handle(key).id == -1, "escape returns to previous menu");
        put_image(screen, 0, 0);
        update_window_done();
        require(platform_screenshot("settings-preview.bmp"), "save settings preview");
        menu.items.clear();
        for (int i = 1; i <= 15; ++i) menu.items.push_back({i, "Slot " + std::to_string(i), "Saved"});
        menu.items.push_back({100, "Back", ""});
        menu.selected = 0;
        key.key = JK_END; menu.handle(key);
        menu.draw(*screen, font, *menu_palette, "ABUSE MODERN", "LOAD GAME", "ARROWS / MOUSE   ENTER TO SELECT");
        require(menu.selected == 15 && menu.first_visible == 9, "long slot lists scroll to Back without drawing over footer");
        click.mouse_move = {40, 52};
        require(menu.handle(click).id == 10, "scrolled mouse hitbox addresses visible slot ten");
        key.key = JK_HOME; menu.handle(key);
        menu.draw(*screen, font, *menu_palette, "ABUSE MODERN", "LOAD GAME", "ARROWS / MOUSE   ENTER TO SELECT");
        require(menu.selected == 0 && menu.first_visible == 0, "Home restores first page");
        key.key = JK_PAGEDOWN; menu.handle(key);
        require(menu.selected == 7, "page navigation advances a visible page");
    }
    // Display changes persist, and a failed write restores the prior display mode.
    const auto preferences = std::filesystem::current_path() / "platform-preferences-fixture";
    std::filesystem::create_directories(preferences);
    set_save_filename_prefix((preferences.string() + "/").c_str());
    require(platform_set_fullscreen(true), "enable and save fullscreen");
    SDL_SyncWindow(platform_window());
    GameSettings reloaded;
    require(settings_load((preferences / "settings.cfg").string(), reloaded) && reloaded.fullscreen == 1,
            "fullscreen survives configuration reload");
    require(platform_set_fullscreen(false), "disable and save fullscreen");
    SDL_SyncWindow(platform_window());
    set_save_filename_prefix((preferences.string() + "/missing/").c_str());
    require(!platform_set_fullscreen(true), "fullscreen reports failed preference write");
    SDL_SyncWindow(platform_window());
    require(!(SDL_GetWindowFlags(platform_window()) & SDL_WINDOW_FULLSCREEN) && flags.fullscreen == 0 && game_settings.fullscreen == 0,
            "failed preference write restores window mode and runtime values");
    std::filesystem::remove_all(preferences);
    close_graphics();
    SDL_Quit();
    std::remove("platform-test.bmp");
    puts("Platform checks passed: pixels, palette changes, clipping, resize, mouse mapping, keyboard, wheel and focus loss.");
}
