// Checks how the game pads are given controller ports (src/pad_ports.h), with SDL's virtual pads, which SDL makes up in
// memory, so no pad and no window is needed:
// - two pads: the first plays controller 1, the second controller 2, and only those; both are plugged in;
// - only the pad that can rumble gives its controller a Rumble Pak;
// - the first pad taken out: controller 2 keeps its pad, and controller 1 (the keyboard's) stays plugged in;
// - three pads plugged in then: the first takes the free controller 1, the next ones 3 and 4;
// - a fifth pad stays on controller 1, as LibUltraShip has it.
// Prints what failed and ends with code 1; ends with 0 when all is right.
#include <SDL3/SDL.h>

#include <cstdio>
#include <initializer_list>

#include "pad_ports.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"

namespace {

int gFailures = 0;

void Expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        gFailures++;
    }
}

bool RumbleCallback(void*, Uint16, Uint16) {
    return true;
}

SDL_JoystickID AttachPad(const char* name, bool rumbles) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = name;
    if (rumbles) {
        desc.Rumble = RumbleCallback;
    }
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        std::printf("FAILED: SDL could not make the virtual pad %s: %s\n", name, SDL_GetError());
        gFailures++;
    }
    return id;
}

// Whether port `port` reads exactly the pads `pads` (SDL's numbers).
bool PortReads(Ship::ConnectedPhysicalDeviceManager& devices, int port, std::initializer_list<SDL_JoystickID> pads) {
    auto reads = devices.GetConnectedSDLGamepadsForPort(static_cast<uint8_t>(port));
    if (reads.size() != pads.size()) {
        return false;
    }
    for (SDL_JoystickID pad : pads) {
        if (!reads.contains(pad)) {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        std::printf("FAILED: SDL's game pad part did not start: %s\n", SDL_GetError());
        return 1;
    }
    {
        Ship::ConnectedPhysicalDeviceManager devices;

        SDL_JoystickID a = AttachPad("pad A", true);
        SDL_JoystickID b = AttachPad("pad B", false);
        devices.RefreshConnectedSDLGamepads();
        unsigned plugged = td::RoutePads(devices);
        Expect(plugged == 0x3, "two pads: controllers 1 and 2 are plugged in");
        Expect(PortReads(devices, 0, { a }), "two pads: controller 1 reads the first pad only");
        Expect(PortReads(devices, 1, { b }), "two pads: controller 2 reads the second pad only");
        Expect(PortReads(devices, 2, {}) && PortReads(devices, 3, {}), "two pads: controllers 3 and 4 read none");
        Expect(td::RumblePorts(devices) == 0x1, "two pads: only controller 1's pad can rumble");

        SDL_DetachVirtualJoystick(a);
        devices.RefreshConnectedSDLGamepads();
        plugged = td::RoutePads(devices);
        Expect(plugged == 0x3, "first pad out: controller 1 (the keyboard's) and controller 2 stay plugged in");
        Expect(PortReads(devices, 0, {}), "first pad out: controller 1 reads no pad");
        Expect(PortReads(devices, 1, { b }), "first pad out: controller 2 keeps its pad");
        Expect(td::RumblePorts(devices) == 0, "first pad out: no Rumble Pak");

        SDL_JoystickID c = AttachPad("pad C", false);
        SDL_JoystickID d = AttachPad("pad D", false);
        SDL_JoystickID e = AttachPad("pad E", true);
        devices.RefreshConnectedSDLGamepads();
        plugged = td::RoutePads(devices);
        Expect(plugged == 0xF, "four pads: all four controllers are plugged in");
        Expect(PortReads(devices, 0, { c }), "four pads: the next pad takes the free controller 1");
        Expect(PortReads(devices, 2, { d }) && PortReads(devices, 3, { e }), "four pads: the others take 3 and 4");
        Expect(td::RumblePorts(devices) == 0x8, "four pads: controller 4's pad can rumble");

        SDL_JoystickID f = AttachPad("pad F", false);
        devices.RefreshConnectedSDLGamepads();
        plugged = td::RoutePads(devices);
        Expect(plugged == 0xF, "five pads: still four controllers");
        Expect(PortReads(devices, 0, { c, f }), "five pads: the fifth plays controller 1 too");

        for (SDL_JoystickID pad : { b, c, d, e, f }) {
            SDL_DetachVirtualJoystick(pad);
        }
        devices.RefreshConnectedSDLGamepads();
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    if (gFailures == 0) {
        std::printf("pad_ports: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
