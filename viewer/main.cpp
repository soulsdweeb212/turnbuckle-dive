// viewer: the asset viewer. It loads a model, an effect sprite or a picture of the game from your ROM with the game's
// own loader, turns it into display lists with the game's own functions (compiled from the decompilation, see
// viewer.h), and draws them with LibUltraShip in the game's 480x240 picture, shown at 4:3 like hello. docs/VIEWER.md
// explains it in full.
//
//   viewer [ROM]                  a wrestler's head: mesh 0xA63 with face texture 0xA6A and palette 0xA68, turning
//   viewer --head MESH TEX PAL    another model: a mesh, a texture for it and the texture's palette
//   viewer --angle DEG            the model at a fixed angle, in degrees, instead of turning
//   viewer --sprite [TEX PAL]     an effect sprite, whose 16-colour palette the game reads as IA16 (texture 0x3EC with
//                                 palette 0x3DF if none are given)
//   viewer --picture [ID]         a TEX picture, drawn the way the game draws its pictures (ID 0x3 if none is given)
//   viewer --frames N             draw N frames, then exit
//   viewer --dump                 draw one frame, then print its display lists command by command, in the order
//                                 Fast3D runs them, and exit
//   viewer --screenshot FILE      draw three frames, save the third as a PNG file, and exit
//   viewer --check [--reference FILE]
//                                 draw the subjects named in FILE (viewer_ref.json in the current folder if none is
//                                 given), which tools/viewer_ref.py writes: the head from two sides, the sprite and
//                                 the picture. Compare the vertices and triangles the game built for the head with
//                                 FILE, then read each picture back and compare it with FILE, pixel by pixel
//
// Ids are written in decimal or in hex with 0x. F1 shows a menu bar whose Debug menu opens LibUltraShip's Console,
// Stats and Gfx Debugger windows.
// Exit code: 0 done (or every check passed), 1 wrong options or a resource or ROM that cannot be used, 2 a check
// failed, 77 --check found no ROM.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "context.h"
#include "debug.h"
#include "fast/Fast3dWindow.h"
#include "fast/interpreter.h"
#include "frame.h"
#include "libultraship/libultraship.h"
#include "viewer.h"

namespace {

constexpr uint32_t kWidth = td::kGameWidth;
constexpr uint32_t kHeight = td::kGameHeight;

// The subjects shown when no ids are given.
constexpr uint32_t kHeadMesh = 0xA63;
constexpr uint32_t kHeadTexture = 0xA6A;
constexpr uint32_t kHeadPalette = 0xA68;
constexpr uint32_t kSpriteTexture = 0x3EC;
constexpr uint32_t kSpritePalette = 0x3DF;
constexpr uint32_t kPicture = 0x3;

// How far the head turns each frame when no --angle is given, in degrees: a turn every 12 seconds at 60 frames a
// second.
constexpr float kTurnPerFrame = 0.5f;

// The camera puts the model's middle at the picture's middle and makes the ball around it 2 * kFitPixels picture
// pixels high, so the model fits the picture at every angle. tools/viewer_ref.py places it the same way.
constexpr float kFitPixels = 100.0f;

// Distinct colour and depth image addresses. Fast3D treats a fill rectangle as a depth clear when the two are equal,
// and both start out null.
uint16_t sColorImage[1];
uint16_t sDepthImage[1];

// The viewport covers the whole 480x240 picture (in quarter pixels: half its size, and its middle).
Vp sViewport = { { { kWidth * 2, kHeight * 2, G_MAXZ / 2, 0 }, { kWidth * 2, kHeight * 2, G_MAXZ / 2, 0 } } };
Mtx sProjection;
Mtx sModelview;

// The frame's display list, and the commands the game writes for a picture, which the frame calls.
Gfx sFrame[256];
std::vector<Gfx> sPictureCommands;

enum class Subject { Head, Sprite, Picture };

struct Options {
    const char* rom = nullptr;
    Subject subject = Subject::Head;
    uint32_t mesh = kHeadMesh, texture = kHeadTexture, palette = kHeadPalette;
    uint32_t spriteTexture = kSpriteTexture, spritePalette = kSpritePalette;
    uint32_t pictureId = kPicture;
    bool fixedAngle = false;
    float angle = 0.0f;
    long frames = 0;
    bool dump = false;
    const char* screenshot = nullptr;
    bool check = false;
    const char* reference = "viewer_ref.json";
};

bool StartsWithDigit(const char* text) {
    return text != nullptr && text[0] >= '0' && text[0] <= '9';
}

// Reads count ids that follow option on the command line, from argv[at] on, into ids. They are optional: when the
// argument after the option starts with a digit, it and the ones after it are the ids. (A ROM whose name starts with a
// digit can be given as ./name.) Returns false after printing why if they cannot be read.
bool ParseIds(int argc, char** argv, int* at, const char* option, uint32_t* const ids[], int count) {
    if (!StartsWithDigit(*at + 1 < argc ? argv[*at + 1] : nullptr)) {
        return true;
    }
    for (int i = 0; i < count; i++) {
        if (*at + 1 + i >= argc || !viewer_parse_id(option, argv[*at + 1 + i], ids[i])) {
            std::fprintf(stderr, "%s takes no ids, or %d\n", option, count);
            return false;
        }
    }
    *at += count;
    return true;
}

// Reads the command line into options. Returns false after printing why it cannot.
bool ParseOptions(int argc, char** argv, Options* options) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;

        if (std::strcmp(arg, "--head") == 0) {
            uint32_t* const ids[] = { &options->mesh, &options->texture, &options->palette };
            options->subject = Subject::Head;
            if (!ParseIds(argc, argv, &i, "--head", ids, 3)) {
                return false;
            }
        } else if (std::strcmp(arg, "--sprite") == 0) {
            uint32_t* const ids[] = { &options->spriteTexture, &options->spritePalette };
            options->subject = Subject::Sprite;
            if (!ParseIds(argc, argv, &i, "--sprite", ids, 2)) {
                return false;
            }
        } else if (std::strcmp(arg, "--picture") == 0) {
            uint32_t* const ids[] = { &options->pictureId };
            options->subject = Subject::Picture;
            if (!ParseIds(argc, argv, &i, "--picture", ids, 1)) {
                return false;
            }
        } else if (std::strcmp(arg, "--angle") == 0 && next != nullptr) {
            char* end;
            options->angle = std::strtof(next, &end);
            if (end == next || *end != '\0' || !std::isfinite(options->angle)) {
                std::fprintf(stderr, "--angle needs a number of degrees, such as 30 or -45.5\n");
                return false;
            }
            options->fixedAngle = true;
            i++;
        } else if (std::strcmp(arg, "--frames") == 0 && next != nullptr) {
            char* end;
            options->frames = std::strtol(next, &end, 10);
            if (end == next || *end != '\0' || options->frames <= 0) {
                std::fprintf(stderr, "--frames needs a whole number above 0\n");
                return false;
            }
            i++;
        } else if (std::strcmp(arg, "--dump") == 0) {
            options->dump = true;
        } else if (std::strcmp(arg, "--screenshot") == 0 && next != nullptr) {
            options->screenshot = next;
            i++;
        } else if (std::strcmp(arg, "--check") == 0) {
            options->check = true;
        } else if (std::strcmp(arg, "--reference") == 0 && next != nullptr) {
            options->reference = next;
            i++;
        } else if (std::strncmp(arg, "--", 2) == 0 || options->rom != nullptr) {
            std::fprintf(stderr, "usage: viewer [--head [MESH TEX PAL]] [--sprite [TEX PAL]] [--picture [ID]] "
                                 "[--angle DEG] [--frames N] [--dump] [--screenshot FILE] "
                                 "[--check [--reference FILE]] [ROM]\n");
            return false;
        } else {
            options->rom = arg;
        }
    }
    return true;
}

// Checks that the subject's resources are of the kinds the game's functions expect, before they see them.
bool CheckHeadIds(uint32_t mesh, uint32_t texture, uint32_t palette) {
    uint32_t entries = 0;

    return viewer_check_mesh(mesh) && viewer_check_texture(texture, &entries) &&
           viewer_check_palette(palette, entries);
}

bool CheckSpriteIds(uint32_t texture, uint32_t palette) {
    return viewer_check_sprite_texture(texture) && viewer_check_palette(palette, 16);
}

// ---- The frame ----

// The start of every frame: the colour image (the address only matters to Fast3D's fill-rectangle rule), the scissor
// and the background, which fills the whole picture in fill mode.
Gfx* StartFrame(Gfx* g) {
    gDPPipeSync(g++);
    gDPSetColorImage(g++, G_IM_FMT_RGBA, G_IM_SIZ_16b, kWidth, sColorImage);
    gDPSetDepthImage(g++, sDepthImage);
    gDPSetScissor(g++, G_SC_NON_INTERLACE, 0, 0, kWidth, kHeight);
    gDPSetCycleType(g++, G_CYC_FILL);
    gDPSetRenderMode(g++, G_RM_NOOP, G_RM_NOOP2);
    gDPSetFillColor(g++, td::Fill(td::kBackgroundR, td::kBackgroundG, td::kBackgroundB));
    gDPFillRectangle(g++, 0, 0, kWidth - 1, kHeight - 1);
    gDPPipeSync(g++);
    return g;
}

// The end of every frame. A check reads the corner markers to find which way up the picture reads back.
void EndFrame(Gfx* g, bool markers) {
    if (markers) {
        g = td::AddCornerMarkers(g);
    }
    gDPFullSync(g++);
    gSPEndDisplayList(g++);
}

// Where the camera puts a model: the middle of its box, the angle it is turned by about the vertical axis, and the
// scale, in picture pixels per model unit, that makes the ball around the model 2 * kFitPixels high.
struct Camera {
    float middle[3];
    float scale;
};

Camera FitCamera(const void* vertexData, uint32_t count) {
    const Vtx* vertices = static_cast<const Vtx*>(vertexData);
    float low[3], high[3];
    float radius = 0.0f;
    Camera camera;

    for (int axis = 0; axis < 3; axis++) {
        low[axis] = high[axis] = vertices[0].v.ob[axis];
        for (uint32_t i = 1; i < count; i++) {
            low[axis] = std::min(low[axis], (float)vertices[i].v.ob[axis]);
            high[axis] = std::max(high[axis], (float)vertices[i].v.ob[axis]);
        }
        camera.middle[axis] = (low[axis] + high[axis]) / 2.0f;
    }
    for (uint32_t i = 0; i < count; i++) {
        float dx = vertices[i].v.ob[0] - camera.middle[0];
        float dy = vertices[i].v.ob[1] - camera.middle[1];
        float dz = vertices[i].v.ob[2] - camera.middle[2];
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    camera.scale = kFitPixels / std::max(radius, 1.0f);
    return camera;
}

// The matrices of the view. The modelview moves the model's middle to the origin, turns it by angle degrees about
// the vertical axis and scales it to picture pixels. The projection is orthographic: it maps a point (x, y, z) in
// pixels from the picture's middle to the screen. The game's picture is 480x240 shown at 4:3, so its pixels are 1.5
// times as tall as they are wide; x is divided by 160 rather than 240, so that the model keeps its shape on screen.
// Depths up to 256 pixels either way stay inside the view. tools/viewer_ref.py computes the same place for every
// vertex.
void SetCamera(const Camera& camera, float angle) {
    float radians = angle * 3.14159265358979f / 180.0f;
    float c = std::cos(radians), s = std::sin(radians);
    float move[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 },
                         { -camera.middle[0], -camera.middle[1], -camera.middle[2], 1 } };
    float turn[4][4] = { { c, 0, -s, 0 }, { 0, 1, 0, 0 }, { s, 0, c, 0 }, { 0, 0, 0, 1 } };
    float size[4][4] = { { camera.scale, 0, 0, 0 }, { 0, camera.scale, 0, 0 }, { 0, 0, camera.scale, 0 },
                         { 0, 0, 0, 1 } };
    float projection[4][4] = { { 1.0f / 160.0f, 0, 0, 0 }, { 0, 1.0f / 120.0f, 0, 0 }, { 0, 0, -1.0f / 256.0f, 0 },
                               { 0, 0, 0, 1 } };
    float moved[4][4], modelview[4][4];

    td::MultiplyMatrices(move, turn, moved);
    td::MultiplyMatrices(moved, size, modelview);
    td::MatrixFromFloat(&sModelview, modelview);
    td::MatrixFromFloat(&sProjection, projection);
}

// The settings the game's frame starts with for its models and sprites, which come from its segment-1 block and
// buildSceneSetupDisplayList (not compiled yet: they write N64 addresses into 32-bit words). One cycle; textures on at
// full scale; shading, no lighting; palettes read as RGBA16; perspective-correct texture coordinates. Then the
// viewport and the camera's two matrices. One setting is the viewer's own: the game filters its textures
// (G_TF_BILERP), and the viewer samples them without filtering, so every texel shows as a block of one colour, which
// --check can then compare pixel by pixel.
Gfx* StartModels(Gfx* g) {
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPClearGeometryMode(g++, 0xFFFFFFFF);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetTextureLUT(g++, G_TT_RGBA16);
    gDPSetTexturePersp(g++, G_TP_PERSP);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gSPViewport(g++, &sViewport);
    gSPMatrix(g++, &sProjection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, &sModelview, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    return g;
}

// The head's frame. The game's own lists (the material list and the triangle list that the game's functions wrote) do
// the drawing; around them, the viewer sets what the game's frame would have set.
void BuildHeadFrame(const ViewerHead& head, bool markers) {
    Gfx* g = StartModels(StartFrame(sFrame));

    // What the game's func_80007020 does for its models (not compiled yet: it keeps list addresses in 32-bit fields).
    // First the render state it sets: the texel's colour and alpha as they are (G_CC_DECALRGBA), no blending with what
    // is behind, and texels whose alpha is 0 left out (CVG_X_ALPHA and ALPHA_CVG_SEL). Then, for the model, a G_VTX of
    // its vertices into the vertex slots from its first slot on, and a G_DL to its material list. That list loads the
    // palette and the texels and branches to the triangle list, whose G_ENDDL returns here.
    gDPPipeSync(g++);
    gDPSetRenderMode(g++,
                     AA_EN | CVG_X_ALPHA | ALPHA_CVG_SEL | FORCE_BL | GBL_c1(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1),
                     GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_IN, G_BL_1));
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    __gSPVertex(g++, const_cast<void*>(head.vertices), head.vertex_count, head.first_vertex);
    __gSPDisplayList(g++, const_cast<void*>(head.material_list));
    EndFrame(g, markers);
}

// The effect sprite's frame. The game's func_80011764 draws a sprite with buildCi4TextureDisplayList, whose list sets
// the sprites' render state (the texel blended over what is behind it by its alpha) and colour combiner (the texel
// times the primitive colour), and loads the palette and the texels. func_80011764 is not compiled yet (it keeps the
// sprites in arrays sized for the N64's pointers), so the viewer writes its other commands here, in its order and with
// its words: the primitive colour before the list (the game takes it from a table for each kind of sprite; the viewer
// uses white, so every texel shows as it is), and after it shading off, the palette read as IA16, the four vertices
// and their two triangles, and the settings put back (shading on, no alpha test, palettes read as RGBA16).
void BuildSpriteFrame(const ViewerSprite& sprite, bool markers) {
    Gfx* g = StartModels(StartFrame(sFrame));

    gDPSetPrimColor(g++, 0, 0, 255, 255, 255, 255);
    __gSPDisplayList(g++, const_cast<void*>(sprite.material_list));
    gSPClearGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetTextureLUT(g++, G_TT_IA16);
    __gSPVertex(g++, const_cast<void*>(sprite.vertices), 4, 0);
    gSP2Triangles(g++, 0, 1, 2, 0, 2, 1, 3, 0);
    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    gDPSetTextureLUT(g++, G_TT_RGBA16);
    EndFrame(g, markers);
}

// The picture's top left corner: in the middle of the 480x240 frame, or at its edge for a picture taller or wider
// than the frame. The game's func_80096D8C draws from a negative x correctly but not from a negative y, so a picture
// taller than the frame shows its top 240 rows. tools/viewer_ref.py puts a picture in the same place.
int32_t PictureX(const ViewerPicture& picture) {
    return std::max<int32_t>(0, ((int32_t)kWidth - (int32_t)picture.width) / 2);
}

int32_t PictureY(const ViewerPicture& picture) {
    return std::max<int32_t>(0, ((int32_t)kHeight - (int32_t)picture.height) / 2);
}

// The picture's frame. The game's 2D code (func_8009EC58 in overlay 1, for example) sets the render state and loads the
// picture's palette with commands of its own, then calls func_80096D8C for the rest. That code is not compiled yet (it
// calls static lists in the overlay's data, which the port does not have yet), so the viewer writes those commands
// itself: the palette load with the game's words (gDPLoadTLUT_pal16 and _pal256 emit them), and the render state
// below. Then the frame calls the commands the game's func_80096D8C wrote.
void BuildPictureFrame(const ViewerPicture& picture, bool markers) {
    Gfx* g = StartFrame(sFrame);

    // Its render state: one cycle, the texel blended over what is behind it by its alpha (G_RM_AA_XLU_SURF, so palette
    // entries with alpha 0 let the background through), palettes read as RGBA16, no perspective correction (texture
    // rectangles need none). The game's combiner fades the texel towards the environment colour by the primitive
    // colour; at full strength, as here, that is the texel itself (G_CC_DECALRGBA). As for the models, the viewer does
    // not filter the texels.
    gDPSetCycleType(g++, G_CYC_1CYCLE);
    gDPSetRenderMode(g++, G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureLUT(g++, G_TT_RGBA16);
    gDPSetTexturePersp(g++, G_TP_NONE);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gDPSetAlphaCompare(g++, G_AC_NONE);
    if (picture.palette_entries == 16) {
        gDPLoadTLUT_pal16(g++, 0, picture.palette);
    } else {
        gDPLoadTLUT_pal256(g++, picture.palette);
    }
    __gSPDisplayList(g++, sPictureCommands.data());
    EndFrame(g, markers);
}

// Has the game's func_80096D8C write its commands for the picture, and ends them with G_ENDDL.
void BuildPictureCommands(const ViewerPicture& picture) {
    uint32_t capacity = viewer_picture_max_commands(&picture);

    sPictureCommands.assign(capacity + 1, Gfx{});
    uint32_t written =
        viewer_picture_draw(&picture, PictureX(picture), PictureY(picture), sPictureCommands.data(), capacity);
    gSPEndDisplayList(&sPictureCommands[written]);
}

// Draws the frame until count frames have been drawn. A backend that paces frames itself (DXGI on Windows) skips
// drawing a frame that is already late; only the frames it drew count. Returns false if the window was closed first.
bool DrawFrames(const std::shared_ptr<Fast::Fast3dWindow>& window, int count) {
    int drawn = 0;

    while (drawn < count) {
        if (!WindowIsRunning()) {
            return false;
        }
        window->HandleEvents();
        if (window->DrawAndRunGraphicsCommands(sFrame, {})) {
            drawn++;
        }
    }
    return true;
}

// ---- The reference ----

// A frame as the reference describes it (tools/viewer_ref.py, pixel_table): for each picture pixel, the set of colours
// it may have, each (red << 10) | (green << 5) | blue in 5 bits per channel, or -1 where the background shows. A pixel
// whose set is empty is not compared.
struct PixelTable {
    std::vector<std::vector<int>> sets;
    std::vector<uint32_t> pixels; // kWidth x kHeight places in sets, row by row from the top
};

// The head seen from one side: the angle it is turned to, the triangles the camera sees from the front, and the frame.
struct HeadView {
    float angle = 0.0f;
    std::vector<uint32_t> front;
    PixelTable table;
};

struct Reference {
    uint32_t mesh = 0, texture = 0, palette = 0;
    std::vector<std::array<int, 10>> vertices;
    std::vector<std::array<int, 3>> triangles;
    std::vector<HeadView> views;
    uint32_t spriteTexture = 0, spritePalette = 0;
    PixelTable sprite;
    uint32_t pictureId = 0;
    int32_t pictureX = 0, pictureY = 0;
    PixelTable picture;
};

// Reading the reference. Each value's type and range, and each list's length, are checked as it is read, so that a
// reference written by an older or newer tools/viewer_ref.py, or changed by hand, stops the check with a message
// instead of being read past its end.
struct BadReference : std::runtime_error {
    using std::runtime_error::runtime_error;
};

const nlohmann::json& Member(const nlohmann::json& object, const char* where, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        throw BadReference(std::string(where) + " has no \"" + key + "\"");
    }
    return object.at(key);
}

const nlohmann::json& List(const nlohmann::json& list, const char* where, size_t size = 0) {
    if (!list.is_array() || (size != 0 && list.size() != size)) {
        throw BadReference(std::string(where) + " is not a list" +
                           (size != 0 ? " of " + std::to_string(size) : std::string()));
    }
    return list;
}

int64_t Integer(const nlohmann::json& value, const char* where, int64_t low, int64_t high) {
    bool fits = false;

    if (value.is_number_unsigned()) {
        fits = value.get<uint64_t>() <= (uint64_t)high;
    } else if (value.is_number_integer()) {
        fits = value.get<int64_t>() >= low && value.get<int64_t>() <= high;
    }
    if (!fits) {
        throw BadReference(std::string(where) + " is not a whole number from " + std::to_string(low) + " to " +
                           std::to_string(high));
    }
    return value.get<int64_t>();
}

uint32_t Id(const nlohmann::json& object, const char* where, const char* key) {
    std::string name = std::string(where) + "." + key;

    return (uint32_t)Integer(Member(object, where, key), name.c_str(), 1, 0xFFFF);
}

PixelTable ReadTable(const nlohmann::json& object, const char* where) {
    PixelTable table;
    std::string name = where;
    const nlohmann::json& sets = List(Member(object, where, "colour_sets"), (name + ".colour_sets").c_str());
    const nlohmann::json& rows = List(Member(object, where, "pixels"), (name + ".pixels").c_str(), kHeight);

    for (size_t i = 0; i < sets.size(); i++) {
        std::string at = name + ".colour_sets[" + std::to_string(i) + "]";
        std::vector<int> set;
        for (const nlohmann::json& colour : List(sets[i], at.c_str())) {
            set.push_back((int)Integer(colour, at.c_str(), -1, 0x7FFF));
        }
        table.sets.push_back(set);
    }
    for (uint32_t y = 0; y < kHeight; y++) {
        std::string at = name + ".pixels[" + std::to_string(y) + "]";
        for (const nlohmann::json& place : List(rows[y], at.c_str(), kWidth)) {
            table.pixels.push_back((uint32_t)Integer(place, at.c_str(), 0, (int64_t)table.sets.size() - 1));
        }
    }
    return table;
}

Reference ReadReference(const nlohmann::json& file) {
    Reference ref;
    const nlohmann::json& head = Member(file, "the reference", "head");
    const nlohmann::json& sprite = Member(file, "the reference", "sprite");
    const nlohmann::json& picture = Member(file, "the reference", "picture");

    ref.mesh = Id(head, "head", "mesh");
    ref.texture = Id(head, "head", "texture");
    ref.palette = Id(head, "head", "palette");
    for (const nlohmann::json& vertex : List(Member(head, "head", "vertices"), "head.vertices")) {
        std::array<int, 10> fields;
        const nlohmann::json& values = List(vertex, "a vertex of head.vertices", fields.size());
        for (size_t f = 0; f < fields.size(); f++) {
            fields[f] = (int)Integer(values[f], "a vertex field", -32768, 65535);
        }
        ref.vertices.push_back(fields);
    }
    for (const nlohmann::json& triangle : List(Member(head, "head", "triangles"), "head.triangles")) {
        std::array<int, 3> corners;
        const nlohmann::json& values = List(triangle, "a triangle of head.triangles", corners.size());
        for (size_t c = 0; c < corners.size(); c++) {
            corners[c] = (int)Integer(values[c], "a triangle's vertex", 0, 255);
        }
        ref.triangles.push_back(corners);
    }
    for (const nlohmann::json& view : List(Member(head, "head", "views"), "head.views")) {
        HeadView seen;
        const nlohmann::json& angle = Member(view, "a head view", "angle");
        if (!angle.is_number() || !std::isfinite(angle.get<double>())) {
            throw BadReference("a head view's angle is not a number");
        }
        seen.angle = angle.get<float>();
        for (const nlohmann::json& number : List(Member(view, "a head view", "front"), "a head view's front")) {
            seen.front.push_back(
                (uint32_t)Integer(number, "a head view's front", 0, (int64_t)ref.triangles.size() - 1));
        }
        seen.table = ReadTable(view, "a head view");
        ref.views.push_back(seen);
    }
    if (ref.views.empty()) {
        throw BadReference("head.views is empty");
    }

    ref.spriteTexture = Id(sprite, "sprite", "texture");
    ref.spritePalette = Id(sprite, "sprite", "palette");
    ref.sprite = ReadTable(sprite, "sprite");

    ref.pictureId = Id(picture, "picture", "id");
    ref.pictureX = (int32_t)Integer(Member(picture, "picture", "x"), "picture.x", 0, kWidth);
    ref.pictureY = (int32_t)Integer(Member(picture, "picture", "y"), "picture.y", 0, kHeight);
    ref.picture = ReadTable(picture, "picture");
    return ref;
}

bool LoadReference(const char* path, Reference* reference) {
    std::ifstream in(path);

    if (!in) {
        std::printf("check: FAIL, no reference %s: tools/viewer_ref.py writes it (docs/VIEWER.md says how)\n", path);
        return false;
    }
    try {
        *reference = ReadReference(nlohmann::json::parse(in));
    } catch (const std::exception& e) {
        std::printf("check: FAIL, the reference %s cannot be read: %s\n", path, e.what());
        return false;
    }
    std::printf("check: reference %s\n", path);
    return true;
}

// ---- --check ----

// A 5-bit-per-channel colour as it reads back.
struct Rgb {
    int r, g, b;
};

Rgb Channels(uint16_t px) {
    return { px >> 11, (px >> 6) & 0x1F, (px >> 1) & 0x1F };
}

// The colour-scaling tolerance: a 5-bit channel that Fast3D scaled to 8 bits for the GPU and that was read back may be
// 1 off.
bool Near(Rgb a, Rgb b) {
    return std::abs(a.r - b.r) <= 1 && std::abs(a.g - b.g) <= 1 && std::abs(a.b - b.b) <= 1;
}

const Rgb kBackground = { (int)td::kBackgroundR >> 3, (int)td::kBackgroundG >> 3, (int)td::kBackgroundB >> 3 };

// A colour of the reference: -1 is the background.
Rgb ReferenceColour(int colour) {
    return colour < 0 ? kBackground : Rgb{ (colour >> 10) & 0x1F, (colour >> 5) & 0x1F, colour & 0x1F };
}

// The two pixels where a checked frame has its corner markers (td::AddCornerMarkers), which the checks leave out.
bool IsMarker(uint32_t x, uint32_t y) {
    return (x == 0 && y == 0) || (x == kWidth - 1 && y == kHeight - 1);
}

// Compares the vertices that the game's func_8000486C decoded, and the triangles in the list that the game's
// buildVertexDisplayList wrote, with the reference's own decode of the mesh.
bool CheckHeadData(const ViewerHead& head, const Reference& ref) {
    static const char* const kFields[10] = { "x", "y", "z", "flag", "s", "t", "red", "green", "blue", "alpha" };
    const Vtx* v = static_cast<const Vtx*>(head.vertices);
    bool pass = true;

    if (ref.vertices.size() != head.vertex_count) {
        std::printf("check: FAIL, the game decoded %u vertices from mesh 0x%X, the reference %zu\n", head.vertex_count,
                    head.mesh, ref.vertices.size());
        pass = false;
    } else {
        for (uint32_t i = 0; i < head.vertex_count && pass; i++) {
            const Vtx_t& t = v[i].v;
            int fields[10] = { t.ob[0], t.ob[1], t.ob[2], t.flag, t.tc[0],
                               t.tc[1], t.cn[0], t.cn[1], t.cn[2], t.cn[3] };

            for (int f = 0; f < 10; f++) {
                if (fields[f] != ref.vertices[i][f]) {
                    std::printf("check: FAIL, vertex %u of mesh 0x%X: the game's func_8000486C wrote %s %d, the "
                                "reference has %d\n",
                                i, head.mesh, kFields[f], fields[f], ref.vertices[i][f]);
                    pass = false;
                    break;
                }
            }
        }
        if (pass) {
            std::printf("check: PASS, the %u vertices the game's func_8000486C decoded from mesh 0x%X equal the "
                        "reference's, field by field\n",
                        head.vertex_count, head.mesh);
        }
    }

    // The triangle list: two geometry-mode commands, then G_TRI2 (two triangles) and G_TRI1 (one), each vertex number
    // doubled in a byte, then a pipe sync and G_ENDDL. The walk stops at the end of the list's buffer.
    std::vector<std::array<int, 3>> built;
    const Gfx* g = static_cast<const Gfx*>(head.triangle_list);
    for (uint32_t step = 0; step < head.triangle_list_size; step++, g++) {
        uint8_t opcode = (uint8_t)(g->words.w0 >> 24);
        auto triangle = [](uintptr_t word) {
            return std::array<int, 3>{ (int)((word >> 16) & 0xFF) / 2, (int)((word >> 8) & 0xFF) / 2,
                                       (int)(word & 0xFF) / 2 };
        };

        if (opcode == G_TRI2) {
            built.push_back(triangle(g->words.w0));
            built.push_back(triangle(g->words.w1));
        } else if (opcode == G_TRI1) {
            built.push_back(triangle(g->words.w0));
        } else if (opcode == G_ENDDL) {
            break;
        }
    }
    bool same = built.size() == ref.triangles.size();
    for (size_t i = 0; same && i < built.size(); i++) {
        same = built[i] == ref.triangles[i];
        if (!same) {
            std::printf("check: FAIL, triangle %zu of mesh 0x%X: the game's list has %d %d %d, the reference "
                        "%d %d %d\n",
                        i, head.mesh, built[i][0], built[i][1], built[i][2], ref.triangles[i][0], ref.triangles[i][1],
                        ref.triangles[i][2]);
        }
    }
    if (same) {
        std::printf("check: PASS, the %zu triangles in the game's triangle list (buildVertexDisplayList) equal the "
                    "reference's\n",
                    built.size());
    } else if (built.size() != ref.triangles.size()) {
        std::printf("check: FAIL, the game's triangle list for mesh 0x%X has %zu triangles, the reference %zu\n",
                    head.mesh, built.size(), ref.triangles.size());
    }
    return pass && same;
}

// Compares every pixel of the frame read back with the reference's table: each must have one of the colours its set
// allows, each channel 1 off at most (Near). A pixel whose set holds only the background must not be drawn; one whose
// set holds no background must be. what names the subject in the messages. The check also needs at least 100 pixels
// that must show the subject, so that it cannot pass on a reference without one.
bool CheckPixels(const td::Picture& pic, const PixelTable& table, const std::string& what) {
    uint32_t compared = 0, skipped = 0, subject = 0, background = 0;
    uint32_t missing = 0, extra = 0, wrong = 0;

    for (uint32_t y = 0; y < kHeight; y++) {
        for (uint32_t x = 0; x < kWidth; x++) {
            const std::vector<int>& allowed = table.sets[table.pixels[y * kWidth + x]];

            if (IsMarker(x, y)) {
                continue;
            }
            if (allowed.empty()) {
                skipped++;
                continue;
            }
            compared++;
            bool onlyBackground = allowed.size() == 1 && allowed[0] < 0;
            bool mayBeBackground = std::any_of(allowed.begin(), allowed.end(), [](int c) { return c < 0; });
            background += onlyBackground ? 1 : 0;
            subject += mayBeBackground ? 0 : 1;

            Rgb got = Channels(pic.At(x, y));
            if (std::any_of(allowed.begin(), allowed.end(), [&](int c) { return Near(got, ReferenceColour(c)); })) {
                continue;
            }
            if (onlyBackground) {
                extra++;
            } else if (!mayBeBackground && Near(got, kBackground)) {
                missing++;
            } else {
                wrong++;
            }
            if (missing + extra + wrong <= 5) {
                std::string colours;
                for (int c : allowed) {
                    Rgb want = ReferenceColour(c);
                    char text[32];
                    std::snprintf(text, sizeof(text), "%s(%d,%d,%d)", colours.empty() ? "" : " or ", want.r, want.g,
                                  want.b);
                    colours += text;
                }
                std::printf("check: %s, pixel (%u,%u) is (%d,%d,%d), the reference has %s\n", what.c_str(), x, y,
                            got.r, got.g, got.b, colours.c_str());
            }
        }
    }
    std::printf("check: %s, %u pixels compared: %u must show it, %u the background, and the rest, on its edges, may "
                "show either; %u not compared. %u show the background where it must be drawn, %u are drawn where the "
                "background must show, %u have another colour (colours as read back, 5 bits per channel; each may be "
                "1 off)\n",
                what.c_str(), compared, subject, background, skipped, missing, extra, wrong);
    bool pass = missing == 0 && extra == 0 && wrong == 0 && subject >= 100;
    if (pass) {
        std::printf("check: PASS, %s is drawn as the reference shows it, pixel for pixel\n", what.c_str());
    } else if (subject < 100) {
        std::printf("check: FAIL, the reference has only %u pixels that must show %s\n", subject, what.c_str());
    } else {
        std::printf("check: FAIL, %s is not drawn as the reference shows it\n", what.c_str());
    }
    return pass;
}

// Reads the picture of the frame just drawn and finds which way up it is. The reference allows for the framebuffer
// pixel that is read back for a picture pixel (td::Picture::At) to have its middle up to a quarter of a picture pixel
// from the picture pixel's middle, which holds when the framebuffer has at least two pixels for each picture pixel
// across and down.
bool ReadFrame(const std::shared_ptr<Fast::Fast3dWindow>& window, td::Picture* pic) {
    if (!td::ReadPicture(window, pic)) {
        return false;
    }
    if (pic->w < 2 * kWidth || pic->h < 2 * kHeight) {
        std::printf("check: FAIL, the framebuffer is %ux%u; the check needs at least %ux%u\n", pic->w, pic->h,
                    2 * kWidth, 2 * kHeight);
        return false;
    }
    return td::CheckCorners(pic);
}

// Fast3D keeps every texture it has decoded, filed under the addresses of its texels and palette, not under what they
// hold. Once a subject's buffers are freed, the next subject's may get the same addresses, so the textures Fast3D keeps
// are dropped with them.
void ForgetTextures(const std::shared_ptr<Fast::Fast3dWindow>& window) {
    if (auto interpreter = window->GetInterpreterWeak().lock()) {
        interpreter->TextureCacheClear();
    }
}

// --check: the subjects the reference names, each drawn for three frames and then read back.
int RunCheck(const std::shared_ptr<Fast::Fast3dWindow>& window, const Reference& ref) {
    bool pass = true;
    td::Picture pic;
    char what[96];

    // The ids come from a file, so they are checked like ids typed on the command line.
    for (uint32_t id : { ref.mesh, ref.texture, ref.palette, ref.spriteTexture, ref.spritePalette, ref.pictureId }) {
        if (!viewer_check_id("the reference", id)) {
            return 2;
        }
    }
    if (!CheckHeadIds(ref.mesh, ref.texture, ref.palette) || !CheckSpriteIds(ref.spriteTexture, ref.spritePalette) ||
        !viewer_check_picture(ref.pictureId)) {
        return 2;
    }

    // The head, from each side the reference shows it.
    ViewerHead head;
    viewer_head_load(ref.mesh, ref.texture, ref.palette, &head);
    // Lists that do not match the reference may hold anything, which Fast3D could crash on, so they are not drawn.
    if (!CheckHeadData(head, ref)) {
        std::printf("check: FAIL, the head's lists are not drawn, because they do not match the reference\n");
        viewer_head_free(&head);
        return 2;
    }
    Camera camera = FitCamera(head.vertices, head.vertex_count);
    std::vector<bool> seen(ref.triangles.size(), false);
    for (const HeadView& view : ref.views) {
        SetCamera(camera, view.angle);
        BuildHeadFrame(head, true);
        if (!DrawFrames(window, 3) || !ReadFrame(window, &pic)) {
            viewer_head_free(&head);
            return 2;
        }
        std::snprintf(what, sizeof(what), "the head at %.1f degrees", view.angle);
        pass = CheckPixels(pic, view.table, what) && pass;
        for (uint32_t t : view.front) {
            seen[t] = true;
        }
    }
    std::printf("check: the pictures compared show %zu of the %zu triangles of mesh 0x%X from the front\n",
                (size_t)std::count(seen.begin(), seen.end(), true), seen.size(), ref.mesh);
    viewer_head_free(&head);
    ForgetTextures(window);

    // The effect sprite, facing the camera.
    ViewerSprite sprite;
    viewer_sprite_load(ref.spriteTexture, ref.spritePalette, &sprite);
    SetCamera(FitCamera(sprite.vertices, 4), 0.0f);
    BuildSpriteFrame(sprite, true);
    if (!DrawFrames(window, 3) || !ReadFrame(window, &pic)) {
        viewer_sprite_free(&sprite);
        return 2;
    }
    std::snprintf(what, sizeof(what), "sprite 0x%X with IA16 palette 0x%X", ref.spriteTexture, ref.spritePalette);
    pass = CheckPixels(pic, ref.sprite, what) && pass;
    viewer_sprite_free(&sprite);
    ForgetTextures(window);

    // The picture.
    ViewerPicture picture;
    viewer_picture_load(ref.pictureId, &picture);
    if (ref.pictureX != PictureX(picture) || ref.pictureY != PictureY(picture)) {
        std::printf("check: FAIL, the reference puts picture 0x%X at (%d,%d), the viewer at (%d,%d)\n", picture.id,
                    ref.pictureX, ref.pictureY, PictureX(picture), PictureY(picture));
        viewer_picture_free(&picture);
        return 2;
    }
    BuildPictureCommands(picture);
    BuildPictureFrame(picture, true);
    if (!DrawFrames(window, 3) || !ReadFrame(window, &pic)) {
        viewer_picture_free(&picture);
        return 2;
    }
    std::snprintf(what, sizeof(what), "picture 0x%X, %ux%u at (%d,%d)", picture.id, picture.width, picture.height,
                  PictureX(picture), PictureY(picture));
    pass = CheckPixels(pic, ref.picture, what) && pass;
    viewer_picture_free(&picture);
    return pass ? 0 : 2;
}

// The viewer's own window: the head turning (or at a fixed angle), the sprite, or the picture, until the window is
// closed or options.frames frames are drawn. With --dump it draws one frame and prints its display lists; with
// --screenshot it draws three and saves the last.
int RunViewer(const std::shared_ptr<Fast::Fast3dWindow>& window, const Options& options) {
    ViewerHead head = {};
    ViewerSprite sprite = {};
    ViewerPicture picture = {};
    Camera camera = {};
    long frame = 0;
    // A screenshot is read back like a --check picture, so its frames carry the corner markers that tell which way up
    // it reads back.
    bool markers = options.screenshot != nullptr;

    if (options.subject == Subject::Picture) {
        viewer_picture_load(options.pictureId, &picture);
        BuildPictureCommands(picture);
        BuildPictureFrame(picture, markers);
        std::printf("viewer: picture 0x%X, %ux%u, %u bits per texel\n", picture.id, picture.width, picture.height,
                    picture.bits);
    } else if (options.subject == Subject::Sprite) {
        viewer_sprite_load(options.spriteTexture, options.spritePalette, &sprite);
        SetCamera(FitCamera(sprite.vertices, 4), 0.0f);
        BuildSpriteFrame(sprite, markers);
        std::printf("viewer: effect sprite, texture 0x%X, IA16 palette 0x%X\n", sprite.texture, sprite.palette);
    } else {
        viewer_head_load(options.mesh, options.texture, options.palette, &head);
        camera = FitCamera(head.vertices, head.vertex_count);
        std::printf("viewer: mesh 0x%X (%u vertices, %u triangles), texture 0x%X, palette 0x%X\n", head.mesh,
                    head.vertex_count, head.triangle_count, head.texture, head.palette);
    }

    long limit = options.dump ? 1 : options.screenshot != nullptr ? 3 : options.frames;
    while (WindowIsRunning() && (limit == 0 || frame < limit)) {
        window->HandleEvents();
        if (options.subject == Subject::Head) {
            SetCamera(camera, options.fixedAngle ? options.angle : (float)frame * kTurnPerFrame);
            BuildHeadFrame(head, markers);
        }
        if (!window->DrawAndRunGraphicsCommands(sFrame, {})) {
            continue;
        }
        frame++;
        td::HoldForGfxDebugger(window, sFrame);
    }
    if (options.dump) {
        std::printf("dump: the frame's display lists in the order Fast3D runs them: address, the two words, name\n");
        uint32_t count = td::DumpDisplayList(sFrame, stdout);
        std::printf("dump: %u commands\n", count);
    }
    int result = 0;
    if (options.screenshot != nullptr) {
        td::Picture pic;

        if (frame < limit || !td::ReadPicture(window, &pic) || !td::CheckCorners(&pic) ||
            !td::SavePicturePng(pic, options.screenshot)) {
            result = 1;
        } else {
            std::printf("viewer: saved %ux%u picture to %s\n", pic.w, pic.h, options.screenshot);
        }
    }

    viewer_picture_free(&picture);
    viewer_sprite_free(&sprite);
    viewer_head_free(&head);
    return result;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    Reference reference;

    if (!ParseOptions(argc, argv, &options)) {
        return 1;
    }

    int opened = viewer_open_rom(options.rom);
    if (opened != 0) {
        if (options.check && opened == VIEWER_EXIT_SKIP) {
            std::printf("SKIP: --check needs the ROM\n");
            return VIEWER_EXIT_SKIP;
        }
        return 1;
    }
    if (options.check && !LoadReference(options.reference, &reference)) {
        viewer_close_rom();
        return 2;
    }
    // The resources are checked before the window opens, so a wrong id ends the viewer with its message at once.
    if (!options.check) {
        bool idsOk = options.subject == Subject::Picture ? viewer_check_picture(options.pictureId)
                     : options.subject == Subject::Sprite
                         ? CheckSpriteIds(options.spriteTexture, options.spritePalette)
                         : CheckHeadIds(options.mesh, options.texture, options.palette);
        if (!idsOk) {
            viewer_close_rom();
            return 1;
        }
    }

    td::PreferWslGpu();
    // A --check run, as CTest starts it, may have nobody watching to close the crash handler's message box, so it has
    // no crash handler, and a crash ends the viewer at once.
    td::LibUltraShip lus = td::StartLibUltraShip("viewer", !options.check);
    if (lus.window == nullptr) {
        viewer_close_rom();
        return 1;
    }
    td::ShowGamePicture(lus.window, options.check || options.screenshot != nullptr);
    td::AddDebugMenu(lus.window);

    int result = options.check ? RunCheck(lus.window, reference) : RunViewer(lus.window, options);

    td::StopLibUltraShip(&lus);
    viewer_close_rom();
    return result;
}
