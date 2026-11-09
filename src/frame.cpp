#include "frame.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"

namespace td {

void MatrixFromFloat(Mtx* m, const float f[4][4]) {
    int32_t* intWords = &m->m[0][0];
    int32_t* fracWords = &m->m[2][0];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t a = (int32_t)(f[i][j] * 65536.0f);
            int32_t b = (int32_t)(f[i][j + 1] * 65536.0f);

            *intWords++ = (int32_t)(((uint32_t)a & 0xFFFF0000u) | ((uint32_t)b >> 16));
            *fracWords++ = (int32_t)(((uint32_t)a << 16) | ((uint32_t)b & 0xFFFFu));
        }
    }
}

void MultiplyMatrices(const float a[4][4], const float b[4][4], float out[4][4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
}

uint32_t Fill(uint32_t r, uint32_t g, uint32_t b) {
    uint32_t c = GPACK_RGBA5551(r, g, b, 1);

    return (c << 16) | c;
}

Gfx* AddCornerMarkers(Gfx* g) {
    gDPPipeSync(g++);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(g++, Fill(255, 255, 255));
    gDPFillRectangle(g++, 0, 0, 0, 0);
    gDPFillRectangle(g++, kGameWidth - 1, kGameHeight - 1, kGameWidth - 1, kGameHeight - 1);
    return g;
}

uint16_t Picture::At(uint32_t x, uint32_t y) const {
    // The centre of picture pixel (x, y), in framebuffer pixels. Each picture pixel covers several framebuffer pixels
    // (2.67 x 4 in a 1280x960 framebuffer), so the centre lies well inside one of them.
    uint32_t fx = (uint32_t)(((float)x + 0.5f) * (float)w / (float)kGameWidth);
    uint32_t fy = (uint32_t)(((float)y + 0.5f) * (float)h / (float)kGameHeight);
    uint32_t row = topDown ? fy : h - 1 - fy;

    return px[row * w + fx];
}

bool ReadPicture(const std::shared_ptr<Fast::Fast3dWindow>& window, Picture* pic) {
    auto interp = window->GetInterpreterWeak().lock();

    if (interp == nullptr || !interp->mRendersToFb) {
        std::printf("check: the picture is drawn straight to the window, so it cannot be read back\n");
        return false;
    }

    pic->w = interp->mCurDimensions.width;
    pic->h = interp->mCurDimensions.height;
    pic->px.resize(pic->w * pic->h);
    interp->mRapi->ReadFramebufferToCPU(interp->mGameFb, pic->w, pic->h, pic->px.data());
    std::printf("check: framebuffer %ux%u for a %ux%u picture (%.2fx)\n", pic->w, pic->h, kGameWidth, kGameHeight,
                (double)pic->h / kGameHeight);
    return true;
}

namespace {

const char* Describe(uint16_t px) {
    uint32_t r = px >> 11, g = (px >> 6) & 0x1F, b = (px >> 1) & 0x1F;

    if (r == 31 && g == 31 && b == 31) {
        return "white";
    }
    return (r < 8 && g < 8 && b > 4) ? "background" : "other";
}

} // namespace

bool CheckCorners(Picture* pic) {
    uint32_t w = pic->w;
    uint32_t h = pic->h;
    const std::vector<uint16_t>& px = pic->px;

    // Sample the middle of each corner pixel of the 480x240 picture as it lands in the w x h framebuffer.
    uint32_t dx = (uint32_t)(0.5f * w / kGameWidth);
    uint32_t dy = (uint32_t)(0.5f * h / kGameHeight);
    uint16_t lowLeft = px[dy * w + dx];
    uint16_t lowRight = px[dy * w + (w - 1 - dx)];
    uint16_t highLeft = px[(h - 1 - dy) * w + dx];
    uint16_t highRight = px[(h - 1 - dy) * w + (w - 1 - dx)];

    std::printf("check: corners, first row read: left %s, right %s; last row read: left %s, right %s\n",
                Describe(lowLeft), Describe(lowRight), Describe(highLeft), Describe(highRight));

    auto is = [](uint16_t p, const char* what) { return std::strcmp(Describe(p), what) == 0; };
    bool upright = is(highLeft, "white") && is(lowRight, "white") && is(lowLeft, "background") &&
                   is(highRight, "background");
    bool flipped = is(lowLeft, "white") && is(highRight, "white") && is(highLeft, "background") &&
                   is(lowRight, "background");

    if (upright || flipped) {
        pic->topDown = flipped;
        std::printf("check: PASS, markers at (0,0) and (479,239) are in the picture's corners (%s rows)\n",
                    upright ? "bottom-up" : "top-down");
        return true;
    }
    std::printf("check: FAIL, the markers are not in two opposite corners\n");
    return false;
}

namespace {

// The CRC-32 that PNG puts after each chunk (the one zip uses too), computed bit by bit.
uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void PutBe32(std::vector<uint8_t>* out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out->push_back((uint8_t)(value >> shift));
    }
}

// Appends a PNG chunk: its length, its type, its data and the CRC of type and data.
void PutChunk(std::vector<uint8_t>* out, const char type[4], const std::vector<uint8_t>& data) {
    std::vector<uint8_t> body(type, type + 4);

    body.insert(body.end(), data.begin(), data.end());
    PutBe32(out, (uint32_t)data.size());
    out->insert(out->end(), body.begin(), body.end());
    PutBe32(out, Crc32(body.data(), body.size()));
}

} // namespace

// A PNG file: an 8-byte signature, then chunks. IHDR gives the size and the pixel format (8-bit RGB); IDAT holds the
// rows, each after a 0 byte (no filter), wrapped as zlib data; IEND ends the file. The zlib data is stored without
// compression, in blocks of at most 65535 bytes, so that no compression library is needed: each block has a byte
// that says whether it is the last, then its length and the length's complement, then its bytes; an Adler-32
// checksum of all the bytes ends it.
bool SavePicturePng(const Picture& pic, const char* path) {
    std::vector<uint8_t> rows;
    for (uint32_t y = 0; y < pic.h; y++) {
        uint32_t row = pic.topDown ? y : pic.h - 1 - y;

        rows.push_back(0);
        for (uint32_t x = 0; x < pic.w; x++) {
            uint16_t px = pic.px[row * pic.w + x];

            // 5 bits per channel to 8, the top bits repeated in the low ones, so 31 becomes 255.
            for (uint32_t shift : { 11u, 6u, 1u }) {
                uint32_t channel = (uint32_t)(px >> shift) & 0x1F;

                rows.push_back((uint8_t)((channel << 3) | (channel >> 2)));
            }
        }
    }

    std::vector<uint8_t> zlib = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (size_t at = 0;; at += 65535) {
        size_t size = std::min<size_t>(65535, rows.size() - at);
        bool last = at + size >= rows.size();

        zlib.push_back(last ? 1 : 0);
        zlib.push_back((uint8_t)size);
        zlib.push_back((uint8_t)(size >> 8));
        zlib.push_back((uint8_t)~size);
        zlib.push_back((uint8_t)(~size >> 8));
        zlib.insert(zlib.end(), rows.begin() + (std::ptrdiff_t)at, rows.begin() + (std::ptrdiff_t)(at + size));
        if (last) {
            break;
        }
    }
    for (uint8_t byte : rows) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    PutBe32(&zlib, (b << 16) | a);

    std::vector<uint8_t> header;
    PutBe32(&header, pic.w);
    PutBe32(&header, pic.h);
    header.insert(header.end(), { 8, 2, 0, 0, 0 }); // 8 bits per channel, RGB, the standard compression and filters,
                                                     // not interlaced

    std::vector<uint8_t> file = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    PutChunk(&file, "IHDR", header);
    PutChunk(&file, "IDAT", zlib);
    PutChunk(&file, "IEND", {});

    std::FILE* out = std::fopen(path, "wb");
    if (out == nullptr || std::fwrite(file.data(), 1, file.size(), out) != file.size()) {
        std::fprintf(stderr, "could not write %s\n", path);
        if (out != nullptr) {
            std::fclose(out);
        }
        return false;
    }
    std::fclose(out);
    return true;
}

void WalkDisplayList(const Gfx* list, const std::function<void(const DisplayListStep&)>& visit, uint32_t maxSteps) {
    // The lists being run, innermost last, each at the command it runs next: like Fast3D's g_exec_stack.
    std::vector<const Gfx*> stack = { list };
    uint32_t steps = 0;

    while (!stack.empty() && steps++ < maxSteps) {
        const Gfx* cmd = stack.back();
        uint32_t depth = (uint32_t)stack.size() - 1;
        uint8_t opcode = (uint8_t)(cmd->words.w0 >> 24);

        visit({ cmd, depth, false });
        if (opcode == G_DL) {
            uintptr_t address = cmd->words.w1;
            const Gfx* target = (address & 1) != 0 ? nullptr : (const Gfx*)address;
            bool branch = ((cmd->words.w0 >> 16) & 1) != 0;

            if (branch) {
                // The list goes on in the target and never comes back here.
                if (target == nullptr) {
                    stack.pop_back();
                } else {
                    stack.back() = target;
                }
            } else {
                stack.back() = cmd + 1;
                if (target != nullptr) {
                    stack.push_back(target);
                }
            }
        } else if (opcode == G_ENDDL) {
            stack.pop_back();
        } else if (opcode == G_TEXRECT || opcode == G_TEXRECTFLIP) {
            visit({ cmd + 1, depth, true });
            visit({ cmd + 2, depth, true });
            stack.back() = cmd + 3;
        } else {
            stack.back() = cmd + 1;
        }
    }
}

uint32_t DumpDisplayList(const Gfx* list, std::FILE* out) {
    uint32_t count = 0;

    WalkDisplayList(list, [&](const DisplayListStep& step) {
        uint8_t opcode = (uint8_t)(step.cmd->words.w0 >> 24);
        const char* name;
        char note[64] = "";

        if (step.partOfPrevious) {
            // G_TEXRECT reads these two itself; on their own they would be the RDP's half-word commands.
            name = opcode == G_RDPHALF_1 ? "G_RDPHALF_1" : opcode == G_RDPHALF_2 ? "G_RDPHALF_2" : "?";
            std::snprintf(note, sizeof(note), "  (read by the G_TEXRECT above)");
        } else {
            name = Fast::GfxGetOpcodeName((int8_t)opcode);
            count++;
            if (opcode == G_DL) {
                bool branch = ((step.cmd->words.w0 >> 16) & 1) != 0;
                bool segmented = (step.cmd->words.w1 & 1) != 0;
                std::snprintf(note, sizeof(note), "  (%s%s)", branch ? "branch" : "call",
                              segmented ? ", segmented address: not followed" : "");
            }
        }
        std::fprintf(out, "%*s%p  %08X %016llX  %s%s\n", (int)(step.depth * 4), "", (const void*)step.cmd,
                     (uint32_t)step.cmd->words.w0, (unsigned long long)step.cmd->words.w1,
                     name != nullptr ? name : "(unknown command)", note);
    });
    return count;
}

} // namespace td
