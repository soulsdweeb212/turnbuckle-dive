// The game's monitors (screen_copy.h).
#include "screen_copy.h"

#include <cstring>
#include <vector>

#include "fast/backends/gfx_rendering_api.h"
#include "fast/interpreter.h"

namespace td {
namespace {

// Window frames to go on copying after the last frame that read a framebuffer: two seconds.
constexpr int kKeepCopying = 120;

int sFramesLeft = 0;
// Fast3D's own 480 x 240 framebuffer that OpenGL shrinks the picture into, made the first time it is needed, and the
// interpreter it belongs to (a new one would need its own).
int sSmallFb = -1;
const Fast::Interpreter* sSmallFbOwner = nullptr;
std::vector<uint16_t> sPixels;

uint8_t* FramebufferAt(const Framebuffers& framebuffers, uintptr_t address) {
    for (uint8_t* start : framebuffers.at) {
        uintptr_t first = reinterpret_cast<uintptr_t>(start);

        if (start != nullptr && address >= first && address - first < kFramebufferBytes) {
            return start;
        }
    }
    return nullptr;
}

// Reads the picture Fast3D drew into sPixels: kGameWidth x kGameHeight RGBA5551 values, the top row first.
bool ReadPicture(Fast::Interpreter& interp) {
    auto* rapi = interp.mRapi;
    // Where the picture is: in Fast3D's own framebuffer (mGameFb), which it fills, or in the window at the game's
    // place in it. The choice is Interpreter::CopyFrameBuffer's, which shrinks into a framebuffer of the window's
    // size, so it is made here again for a framebuffer of the N64's.
    int source = interp.mRendersToFb ? interp.mGameFb : 0;
    bool windowSized = interp.mCurDimensions.width == interp.mGameWindowViewport.width &&
                       interp.mCurDimensions.height == interp.mGameWindowViewport.height;
    bool inWindow = source == 0 || (interp.mMsaaLevel > 1 && windowSized);

    sPixels.resize(kGameWidth * kGameHeight);
    if (std::strcmp(rapi->GetName(), "OpenGL") == 0) {
        if (sSmallFb < 0 || sSmallFbOwner != &interp) {
            sSmallFb = interp.CreateFrameBuffer(kGameWidth, kGameHeight, kGameWidth, kGameHeight, 0, false);
            sSmallFbOwner = &interp;
        }
        int x0 = inWindow ? interp.mGameWindowViewport.x : 0;
        int y0 = inWindow ? interp.mGameWindowViewport.y : 0;
        int x1 = x0 + (int)(inWindow ? interp.mGameWindowViewport.width : interp.mCurDimensions.width);
        int y1 = y0 + (int)(inWindow ? interp.mGameWindowViewport.height : interp.mCurDimensions.height);
        rapi->CopyFramebuffer(sSmallFb, source, x0, y0, x1, y1, 0, 0, kGameWidth, kGameHeight);
        rapi->ReadFramebufferToCPU(sSmallFb, kGameWidth, kGameHeight, sPixels.data());
        return true;
    }
    if (std::strcmp(rapi->GetName(), "DirectX 11") == 0 && !inWindow) {
        rapi->ReadFramebufferToCPU(source, kGameWidth, kGameHeight, sPixels.data());
        return true;
    }
    return false;
}

} // namespace

FramebufferUse FindFramebufferUse(const Gfx* frame, const Framebuffers& framebuffers) {
    FramebufferUse use = { nullptr, false };

    WalkDisplayList(frame, [&](const DisplayListStep& step) {
        if (step.partOfPrevious) {
            return;
        }
        uint8_t opcode = (uint8_t)(step.cmd->words.w0 >> 24);
        uint8_t* framebuffer = FramebufferAt(framebuffers, step.cmd->words.w1);

        if (opcode == (uint8_t)G_SETCIMG) {
            use.drawnInto = framebuffer;
        } else if (opcode == (uint8_t)G_SETTIMG && framebuffer != nullptr) {
            use.readsOne = true;
        }
    });
    return use;
}

void CopyPictureForMonitors(Fast::Interpreter& interp, const Gfx* frame, const Framebuffers& framebuffers) {
    FramebufferUse use = FindFramebufferUse(frame, framebuffers);

    if (use.readsOne) {
        sFramesLeft = kKeepCopying;
    } else if (sFramesLeft > 0) {
        sFramesLeft--;
    }
    if (sFramesLeft == 0 || use.drawnInto == nullptr || !ReadPicture(interp)) {
        return;
    }
    // As the N64 keeps it: the biggest byte first, and the alpha bit set. In an N64 framebuffer that bit is the pixel's
    // coverage, full wherever the frame drew or cleared (the game clears with 0x0001), and the monitors blend by it.
    for (size_t i = 0; i < sPixels.size(); i++) {
        uint16_t pixel = (uint16_t)(sPixels[i] | 1);

        use.drawnInto[2 * i] = (uint8_t)(pixel >> 8);
        use.drawnInto[2 * i + 1] = (uint8_t)pixel;
    }
}

} // namespace td
