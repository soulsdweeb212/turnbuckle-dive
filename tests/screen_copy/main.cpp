// Checks how a frame's display list is read for the monitors (src/screen_copy.h), with small lists made here, so no
// window is needed:
// - a frame drawn into the second framebuffer whose called list loads a texture inside the first: it is drawn into the
//   second, and it reads one;
// - a texture at the first byte past the end of a framebuffer, and one in other memory: no read;
// - a colour image that is not a framebuffer: drawn into none.
// Prints what failed and ends with code 1; ends with 0 when all is right.
#include <cstdio>
#include <cstdint>
#include <vector>

#include "screen_copy.h"

namespace {

int gFailures = 0;

void Expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        gFailures++;
    }
}

Gfx Command(uint32_t w0, const void* w1) {
    Gfx g;

    g.words.w0 = w0;
    g.words.w1 = reinterpret_cast<uintptr_t>(w1);
    return g;
}

// The commands as the game writes them: G_SETCIMG and G_SETTIMG of a 480-wide RGBA16 image, G_DL (a call) and
// G_ENDDL.
constexpr uint32_t kSetColourImage = 0xFF1001DF;
constexpr uint32_t kSetTextureImage = 0xFD1001DF;
constexpr uint32_t kCall = 0xDE000000;
constexpr uint32_t kEnd = 0xDF000000;

} // namespace

int main() {
    std::vector<uint8_t> memory(3 * td::kFramebufferBytes + 4096);
    std::vector<uint8_t> other(4096);
    td::Framebuffers framebuffers = { { memory.data(), memory.data() + td::kFramebufferBytes,
                                        memory.data() + 2 * td::kFramebufferBytes } };

    // A monitor's piece: the texture 20 rows into the first framebuffer, in a list the frame calls.
    Gfx monitor[] = { Command(kSetTextureImage, framebuffers.at[0] + 480 * 2 * 20), Command(kEnd, nullptr) };
    Gfx frame[] = { Command(kSetColourImage, framebuffers.at[1]), Command(kCall, monitor), Command(kEnd, nullptr) };
    td::FramebufferUse use = td::FindFramebufferUse(frame, framebuffers);
    Expect(use.drawnInto == framebuffers.at[1], "the frame is drawn into the second framebuffer");
    Expect(use.readsOne, "the frame's called list loads a texture inside the first framebuffer");

    Gfx past[] = { Command(kSetColourImage, framebuffers.at[2]),
                   Command(kSetTextureImage, framebuffers.at[2] + td::kFramebufferBytes),
                   Command(kSetTextureImage, other.data()), Command(kEnd, nullptr) };
    use = td::FindFramebufferUse(past, framebuffers);
    Expect(use.drawnInto == framebuffers.at[2], "the second frame is drawn into the third framebuffer");
    Expect(!use.readsOne, "textures past a framebuffer's end and in other memory are not reads of one");

    Gfx elsewhere[] = { Command(kSetColourImage, other.data()), Command(kEnd, nullptr) };
    use = td::FindFramebufferUse(elsewhere, framebuffers);
    Expect(use.drawnInto == nullptr, "a colour image in other memory is no framebuffer");

    if (gFailures == 0) {
        std::printf("screen_copy: all checks passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
