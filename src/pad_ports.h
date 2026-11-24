// The game pads on the N64's four controller ports, as the other LibUltraShip ports give them (docs/GAME_CODE.md,
// "Playing the match"). LibUltraShip shows every game pad to controller 1 and hides it from controllers 2 to 4, which
// suits a game for one player; Revenge takes four. So each pad gets a port of its own, in the order the pads were
// plugged in: the first pad controller 1 (with the keyboard), the second controller 2, and so on. A pad keeps its port
// while it stays plugged in, and one plugged in later takes the lowest free port. A fifth pad stays on controller 1, as
// LibUltraShip has it. The player can still change which pads a port reads in F1, Settings, Input Editor. C++ only.
#ifndef TD_PAD_PORTS_H
#define TD_PAD_PORTS_H

namespace Ship {
class ConnectedPhysicalDeviceManager;
class ControlDeck;
} // namespace Ship

namespace td {

// Gives each newly plugged-in pad its port, and forgets the pads taken out. Call it once a frame, before the pads are
// read; `devices` is the control deck's (GetConnectedPhysicalDeviceManager). Returns which controllers are plugged in:
// bit n for controller n + 1. Controller 1 always is (the keyboard). tests/pad_ports checks it with SDL's virtual pads.
unsigned RoutePads(Ship::ConnectedPhysicalDeviceManager& devices);

// Which controllers have a pad that can rumble (bit n for controller n + 1): those have a Rumble Pak for the game.
unsigned RumblePorts(Ship::ConnectedPhysicalDeviceManager& devices);

// Starts or stops controller `port`'s rumble (0 to 3), on the pads its rumble setting names (F1, Settings, Input
// Editor). StopRumble stops all four, as the program ends: SDL could otherwise leave a pad rumbling.
void Rumble(Ship::ControlDeck& deck, int port, bool on);
void StopRumble(Ship::ControlDeck& deck);

} // namespace td

#endif
