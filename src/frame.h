// What hello and the viewer share about one frame of the game's picture: its size, the N64 fixed-point matrices that
// place things in it, the corner markers and the read-back that the --check runs use, and a walk through a frame's
// display lists in the order Fast3D runs them. C++ only; it uses LibUltraShip's Gfx (16-byte commands).
#ifndef TD_FRAME_H
#define TD_FRAME_H

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "libultraship/libultraship.h"

namespace Fast {
class Fast3dWindow;
} // namespace Fast

namespace td {

// The game draws a 480x240 picture, which the N64 shows at 4:3.
constexpr uint32_t kGameWidth = 480;
constexpr uint32_t kGameHeight = 240;

// The background colour of the test pictures, a dark blue. CheckCorners tells it apart from the white markers.
constexpr uint32_t kBackgroundR = 16;
constexpr uint32_t kBackgroundG = 24;
constexpr uint32_t kBackgroundB = 72;

// The N64's fixed-point matrix layout: 16 words of integer halves, then 16 of fractions, two elements per word.
void MatrixFromFloat(Mtx* m, const float f[4][4]);

// Multiplies two matrices the N64 way round: a point (a row) is transformed by a first, then by b.
void MultiplyMatrices(const float a[4][4], const float b[4][4], float out[4][4]);

// A fill colour for gDPSetFillColor in a 16-bit colour image: the RGBA5551 colour twice, one per pixel of a word.
uint32_t Fill(uint32_t r, uint32_t g, uint32_t b);

// A colour as it reads back: 5 bits per channel, without the alpha bit.
constexpr uint16_t Rgb5(uint32_t r, uint32_t g, uint32_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1));
}

// Appends one-pixel white markers at (0,0) and (479,239), drawn in fill mode, and returns the next free command.
// CheckCorners finds them in the picture read back. Drawn last, so nothing covers them.
Gfx* AddCornerMarkers(Gfx* g);

// The game picture read back from Fast3D's framebuffer: w x h RGBA5551 pixels for the 480x240 picture, row after row
// in the order they were read. Fast3D's framebuffer may be stored upside down; CheckCorners finds out which way.
struct Picture {
    uint32_t w = 0;
    uint32_t h = 0;
    std::vector<uint16_t> px;
    bool topDown = false; // the first row read is the picture's top row

    // The pixel whose framebuffer area holds the centre of picture pixel (x, y), counted from the picture's top left.
    uint16_t At(uint32_t x, uint32_t y) const;
};

// Reads the picture of the frame just drawn into pic. Only works when Fast3D draws into its own framebuffer, which
// ShowGamePicture(window, true) (src/context.h) arranges.
bool ReadPicture(const std::shared_ptr<Fast::Fast3dWindow>& window, Picture* pic);

// Checks that the markers are in two opposite corners and the other two corners show the background. Either diagonal
// counts if it is consistent, and it tells which way up the rows were read (pic->topDown).
bool CheckCorners(Picture* pic);

// Writes the picture read back, top row first, at the framebuffer's size, to a PNG file at path. Returns false after
// printing why if the file cannot be written. The file holds the picture as drawn from the user's ROM: it belongs in
// the build folder or elsewhere outside the repository, never in it.
bool SavePicturePng(const Picture& pic, const char* path);

// One command met on a walk through a frame's display lists (WalkDisplayList).
struct DisplayListStep {
    const Gfx* cmd;     // the command
    uint32_t depth;     // 0 in the frame's own list, 1 inside a list it calls, and so on
    bool partOfPrevious; // a word that the command before it reads as its own (the two that follow G_TEXRECT)
};

// Visits every command of the display list at list in the order Fast3D runs it, the way its interpreter steps
// (Interpreter::Run and gfx_step in LibUltraShip's src/fast/interpreter.cpp): a G_DL call goes into the list it names
// and comes back after that list's G_ENDDL; a G_DL branch (the "1" in DE01) continues in the list it names and does
// not come back; G_ENDDL returns to the list that called this one, or ends the frame; G_TEXRECT and G_TEXRECTFLIP take
// the two commands after them as their own. Addresses must be whole pointers, as the port writes them (DL_ADDR in the
// decompilation's port.h): a segmented address (bit 0 set) is not followed, and is reported through visit as a step
// whose depth is that of the G_DL. maxSteps stops a list that never ends.
void WalkDisplayList(const Gfx* list, const std::function<void(const DisplayListStep&)>& visit,
                     uint32_t maxSteps = 100000);

// Prints every command of the display list at list, in the order WalkDisplayList visits them: its address, its two
// words, and its name as LibUltraShip's interpreter knows it (GfxGetOpcodeName), indented by how deep it is called.
// Returns the number of commands Fast3D runs, not counting the words G_TEXRECT reads as its own.
uint32_t DumpDisplayList(const Gfx* list, std::FILE* out);

} // namespace td

#endif
