// The game's monitors (docs/GAME_CODE.md, "The monitors"): the big screens of the menus and of some arenas' entrance
// sets (Souled Out, Bash at the Beach, Halloween Havoc) show the game's own picture. On the N64 the graphics chip draws
// each frame into one of three framebuffers in memory, and a monitor's display list loads the framebuffer the frame
// before was drawn into as a texture, piece by piece (72 pieces for the whole 480 x 240 picture). Fast3D draws on the
// graphics card and never writes those framebuffers, so after a frame whose display list reads one,
// CopyPictureForMonitors puts the picture Fast3D drew into the framebuffer that frame was drawn into, as the N64 keeps
// it (480 x 240, RGBA16, biggest byte first), and the next frame's monitor reads it. C++ only.
#ifndef TD_SCREEN_COPY_H
#define TD_SCREEN_COPY_H

#include <cstdint>

#include "frame.h"

namespace Fast {
class Interpreter;
} // namespace Fast

namespace td {

// The game's three framebuffers (the decompilation's D_8002EEB0 holds their addresses), each kGameWidth x kGameHeight
// RGBA16 pixels.
constexpr uint32_t kFramebufferBytes = kGameWidth * kGameHeight * 2;
struct Framebuffers {
    uint8_t* at[3];
};

// What a frame's display list does with the framebuffers: the one it is drawn into (the address of its G_SETCIMG,
// null when that is not one of them), and whether any texture it loads (G_SETTIMG) lies in one of them.
struct FramebufferUse {
    uint8_t* drawnInto;
    bool readsOne;
};
FramebufferUse FindFramebufferUse(const Gfx* frame, const Framebuffers& framebuffers);

// Call after Fast3D has drawn the frame's display list (Interpreter::Run), before the window shows it. While the frames
// read a framebuffer, and for two seconds after the last that did (a monitor that comes back into view then reads a
// fresh picture at once), it copies the picture into the framebuffer the frame was drawn into; otherwise it does
// nothing. OpenGL shrinks the picture on the graphics card and reads back the 480 x 240 copy; Direct3D 11 reads back
// Fast3D's game picture at 480 x 240 itself. On Direct3D 11, a picture that Fast3D draws straight into the window
// (a window exactly the picture's shape, without anti-aliasing) cannot be read back, and the monitors keep their last
// copy.
void CopyPictureForMonitors(Fast::Interpreter& interp, const Gfx* frame, const Framebuffers& framebuffers);

} // namespace td

#endif
