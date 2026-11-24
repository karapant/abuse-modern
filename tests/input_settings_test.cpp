// SPDX-License-Identifier: GPL-2.0-or-later
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "common.h"
#include "event.h"
#include "sdlport/controls.h"
#include "sdlport/controller.h"
#include "ui/textentry.h"
#include "ui/save_slots.h"
#include "ui/network_options.h"

keys_struct keys = {};
static void require(bool ok, const char *message)
{
    if (!ok) { fprintf(stderr, "FAIL: %s (%s)\n", message, SDL_GetError()); exit(1); }
}
static void pump()
{
    SDL_UpdateJoysticks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) controller_event(event);
}
static void write_fixture(const std::filesystem::path &path, bool valid, bool menu_preview = false)
{
    const std::vector<std::string> names = {"fgmap", "bgmap", "object_list", "player_info", "thumb nail"};
    unsigned offset = 10;
    for (const auto &name : names) offset += 2 + name.size() + 1 + 9;
    std::ofstream file(path, std::ios::binary);
    const int width = menu_preview ? 160 : 2, height = menu_preview ? 100 : 2;
    const auto number = [&](unsigned value, int bytes) { for (int i = 0; i < bytes; ++i) file.put((value >> (8 * i)) & 255); };
    file.write("SPEC1.0\0", 8); number(names.size(), 2);
    for (const auto &name : names) {
        file.put(name == "thumb nail" ? 4 : 20); file.put(name.size() + 1);
        file.write(name.c_str(), name.size() + 1); file.put(0);
        unsigned size = name == "thumb nail" ? 4 + width * height : 4;
        number(size, 4); number(valid ? offset : 999999, 4); offset += size;
    }
    for (int i = 0; i < 4; ++i) number(0, 4);
    number(width, 2); number(height, 2);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            // Centered 288x192 picker and its three-pixel stripe at half scale.
            const bool panel = menu_preview && y >= 2 && y < 98 && x >= 8 && x < 152;
            file.put(panel ? (x < 10 ? 13 : 61) : 5);
        }
}
int main()
{
    const auto root = std::filesystem::current_path() / "input-settings-fixture";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    control_reset();
    require(control_rebind(ControlAction::Left, 'd'), "rebind accepts movement key");
    require(keys.left == 'd' && keys.right == 'a', "conflicting keys swap actions");
    require(!control_rebind(ControlAction::Left, JK_ESC) && keys.left == 'd', "escape cannot replace movement");
    require(!control_rebind(ControlAction::Fire, '1'), "fixed weapon shortcut remains reserved");
    controller_options.enabled = false; controller_options.deadzone = 12000;
    const auto config = (root / "controls.cfg").string();
    require(controls_save(config), "persist controls");
    control_reset(); require(controls_load(config), "reload controls");
    require(keys.left == 'd' && keys.right == 'a' && !controller_options.enabled && controller_options.deadzone == 12000,
            "bindings and gamepad configuration survive restart");
    std::ofstream(root / "invalid.cfg") << "key0=-1\nkey1=9999\ncontroller_deadzone=0\ncontroller_enabled=garbage\nkey4=12trailing\n";
    require(controls_load((root / "invalid.cfg").string()), "read malformed settings");
    require(keys.left == 'd' && keys.right == 'a' && controller_options.deadzone == 12000, "bad values preserve previous settings");
    require(!controls_save((root / "missing" / "controls.cfg").string()), "unwritable settings report failure");

    TextEntry edit("abc", 4);
    event input; input.type = EV_KEY; input.key = 'd'; edit.handle(input);
    input.key = 'e'; edit.handle(input); require(edit.value == "abcd", "text entry enforces field length");
    input.key = JK_BACKSPACE; edit.handle(input); require(edit.value == "abc", "backspace edits text");
    input.key = JK_ESC; require(edit.handle(input) == TextEntry::Result::Cancelled, "escape cancels entry");
    input.key = JK_ENTER; require(edit.handle(input) == TextEntry::Result::Accepted, "enter applies entry");

    auto slots = scan_save_slots(root.string());
    require(slots.size() == 15 && slots[14].number == 15 && slots[14].state == SaveSlotState::Empty, "all fifteen empty slots are enumerated");
    write_fixture(root / "save0015.spe", true);
    write_fixture(root / "save0002.spe", false);
    write_fixture(root / "save0004.spe", true, true);
    std::ofstream(root / "save0003.spe") << "not a save";
    slots = scan_save_slots(root.string());
    require(slots[14].state == SaveSlotState::Saved, "last save slot remains addressable");
    require(slots[3].state == SaveSlotState::Saved && slots[3].preview.empty() && !slots[14].preview.empty(),
            "hide legacy menu thumbnails while preserving gameplay previews and loadable slots");
    require(slots[1].state == SaveSlotState::Unreadable && slots[2].state == SaveSlotState::Unreadable,
            "truncated and out-of-bounds saves are rejected");
    require(save_slot_filename(15) == "save0015.spe" && save_slot_filename(16).empty(), "slot filenames are bounded");

    NetworkOptions options;
    require(validate_network_options(options).empty(), "default network form is valid");
    options.min_players = 8; options.max_players = 2;
    require(!validate_network_options(options).empty(), "inconsistent player limits rejected");
    options.min_players = 2; options.player = "bad\"name";
    require(!save_network_options(root.string(), options, true), "invalid names cannot generate Lisp configuration");
    options.player = "Test player"; options.port = 65534;
    require(!validate_network_options(options).empty(), "reserve room for engine's adjacent ports");
    options.port = 20202;
    require(save_network_options(root.string(), options, true), "network settings saved outside data directory");
    require(std::filesystem::exists(root / "config/levelset.lsp"), "host map configuration created");
    const auto assets = root / "read-only-assets";
    std::filesystem::create_directories(assets / "config");
    std::ofstream(assets / "config/username.lsp") << "original assets";
    std::ofstream(assets / "config/fallback.lsp") << "fallback";
    set_filename_prefix(assets.u8string().c_str());
    const auto save_path = root.string() + "/";
    set_save_filename_prefix(save_path.c_str());
    {
        jFILE local("config/username.lsp", "rb");
        require(!local.open_failure(), "engine resolves network configuration in user directory");
        char contents[128] = {}; local.read(contents, std::min<int>(local.file_size(), 127));
        require(std::string(contents).find("Test player") != std::string::npos, "user configuration overrides bundled defaults");
        jFILE fallback("config/fallback.lsp", "rb");
        require(!fallback.open_failure(), "missing user configuration falls back to bundled data");
        jFILE output("config/written.lsp", "wb"); output.write("local", 5);
    }
    require(std::filesystem::exists(root / "config/written.lsp") && !std::filesystem::exists(assets / "config/written.lsp"),
            "network writes never modify original assets");
    set_filename_prefix(nullptr); set_save_filename_prefix(nullptr);
    require(valid_join_address("127.0.0.1:20202") && valid_join_address("game-host.local"), "join supports IPv4 and hostnames");
    require(!valid_join_address("host:0") && !valid_join_address("host:999999") && !valid_join_address("host:bad") &&
            !valid_join_address("host/path") && !valid_join_address(":"), "malformed endpoints rejected");

    control_reset();
    require(SDL_Init(SDL_INIT_VIDEO), "initialize SDL for virtual controller");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    require(controller_init(), "initialize gamepad subsystem");
    SDL_VirtualJoystickDesc description;
    SDL_INIT_INTERFACE(&description);
    description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    description.naxes = SDL_GAMEPAD_AXIS_COUNT;
    description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    description.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    description.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    description.name = "Abuse virtual test controller";
    const auto id = SDL_AttachVirtualJoystick(&description);
    require(id != 0, "attach virtual gamepad");
    SDL_Joystick *joystick = SDL_OpenJoystick(id);
    require(joystick != nullptr, "open virtual joystick"); pump();
    require(!controller_requires_two_sticks(), "dual-stick pad supports controller gameplay");
    require(std::string(controller_name()).find("virtual") != std::string::npos, "hotplug opens gamepad");
    // SDL virtual trigger axes start at -32768 (unpressed).
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 1000); pump();
    require(controller_state().x == 0, "dead zone filters stick drift");
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 24000);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTY, -24000);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true); pump();
    auto state = controller_state();
    require(state.x == 1 && state.jump && state.fire && state.aim && state.aim_y < -.99f, "movement, jump, trigger and twin-stick aim");
    SDL_Event button = {}; button.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; button.gbutton.which = id;
    button.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    require(controller_key(button, false) == JK_ENTER && controller_key(button, true) == 0, "menu confirmation does not synthesize gameplay movement keys");
    {
        ControllerMenuScope save_console;
        require(controller_key(button, true) == JK_ENTER && controller_state().x == 0,
                "save console uses menu controls even while game state is running");
    }
    require(controller_state().x == 0, "confirm button cannot jump or fire after leaving a dialog");
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false);
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768); pump();
    require(controller_state().x == 1, "neutral buttons restore gameplay after a dialog");
    controller_mouse_used();
    require(!controller_state().aim, "mouse takes over from a held aiming stick");
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 18000); pump();
    require(controller_state().aim, "new stick movement takes aiming back");
    button.gbutton.button = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    require(controller_key(button, true) == keys.b4, "bumper follows current weapon binding");
    controller_focus(false); require(controller_state().x == 0 && !controller_state().fire && !controller_state().aim, "focus loss suppresses held controller input");
    controller_focus(true); controller_options.enabled = false;
    require(controller_state().x == 0 && !controller_state().fire, "disable gamepad leaves no held actions");
    button.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
    require(controller_key(button, false) == JK_ENTER, "disabled gameplay pad can still navigate setup to re-enable itself");
    controller_options.enabled = true;
    require(SDL_DetachVirtualJoystick(id), "unplug virtual controller"); pump();
    require(controller_state().x == 0 && !controller_state().jump && !controller_state().fire && !controller_state().aim, "unplug releases held controller actions");
    SDL_CloseJoystick(joystick);
    description.name = "One-stick test controller";
    description.naxes = 2;
    description.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
    const auto one_stick = SDL_AttachVirtualJoystick(&description);
    require(one_stick != 0, "attach one-stick controller");
    joystick = SDL_OpenJoystick(one_stick); pump();
    require(controller_requires_two_sticks(), "detect missing aiming stick");
    require(controller_take_warning() && !controller_take_warning(), "one-stick warning is shown once per connection");
    SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 30000);
    SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true); pump();
    require(controller_state().x == 0 && !controller_state().jump && !controller_state().aim,
            "one-stick controller cannot activate gameplay actions");
    button.gbutton.which = one_stick;
    require(controller_key(button, false) == JK_ENTER, "one-stick pad retains menu navigation");
    SDL_DetachVirtualJoystick(one_stick); pump(); SDL_CloseJoystick(joystick);
    controller_shutdown(); SDL_Quit();
    std::filesystem::remove_all(root);
    puts("Input/settings checks passed: rebinding, persistence, slot integrity, network validation and virtual gamepad.");
}
