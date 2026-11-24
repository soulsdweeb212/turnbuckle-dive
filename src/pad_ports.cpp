// The game pads on the controller ports (pad_ports.h).
#include "pad_ports.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "ship/controller/controldeck/ControlDeck.h"
#include "ship/controller/controldevice/controller/Controller.h"
#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"

namespace td {
namespace {

constexpr int kPorts = 4;

// The pad on each port, by SDL's number for it; 0 for none (SDL never numbers a pad 0, and numbers each pad it finds
// one higher than the last, so the numbers give the order the pads were plugged in).
std::array<int32_t, kPorts> sPortPad = {};

} // namespace

unsigned RoutePads(Ship::ConnectedPhysicalDeviceManager& devices) {
    auto connected = devices.GetConnectedSDLGamepadNames();

    for (auto& pad : sPortPad) {
        if (pad != 0 && !connected.contains(pad)) {
            pad = 0;
        }
    }
    std::vector<int32_t> fresh;
    for (const auto& [pad, name] : connected) {
        if (std::find(sPortPad.begin(), sPortPad.end(), pad) == sPortPad.end()) {
            fresh.push_back(pad);
        }
    }
    std::sort(fresh.begin(), fresh.end());
    for (int32_t pad : fresh) {
        auto free = std::find(sPortPad.begin(), sPortPad.end(), 0);
        if (free == sPortPad.end()) {
            break; // a fifth pad: LibUltraShip leaves it on controller 1
        }
        *free = pad;
        const int port = static_cast<int>(free - sPortPad.begin());
        for (int other = 0; other < kPorts; other++) {
            if (other == port) {
                devices.UnignoreInstanceIdForPort(static_cast<uint8_t>(other), pad);
            } else {
                devices.IgnoreInstanceIdForPort(static_cast<uint8_t>(other), pad);
            }
        }
    }

    unsigned plugged = 1;
    for (int port = 1; port < kPorts; port++) {
        if (sPortPad[port] != 0) {
            plugged |= 1u << port;
        }
    }
    return plugged;
}

unsigned RumblePorts(Ship::ConnectedPhysicalDeviceManager& devices) {
    unsigned ports = 0;

    for (int port = 0; port < kPorts; port++) {
        for (const auto& [id, pad] : devices.GetConnectedSDLGamepadsForPort(static_cast<uint8_t>(port))) {
            if (SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) {
                ports |= 1u << port;
            }
        }
    }
    return ports;
}

void Rumble(Ship::ControlDeck& deck, int port, bool on) {
    auto controller = deck.GetControllerByPort(static_cast<uint8_t>(port));
    if (controller == nullptr) {
        return;
    }
    if (on) {
        controller->GetRumble()->StartRumble();
    } else {
        controller->GetRumble()->StopRumble();
    }
}

void StopRumble(Ship::ControlDeck& deck) {
    for (int port = 0; port < kPorts; port++) {
        Rumble(deck, port, false);
    }
}

} // namespace td
