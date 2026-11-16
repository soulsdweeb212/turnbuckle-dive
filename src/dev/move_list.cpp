// The Move List window (src/dev/move_list.h).
//
// Where the moves are: not in the game code's read-only data, but in the ROM's resource archive. Each wrestler has
// three resources in a row, from 0x12B to 0x20E (src/res_convert.c): a profile, a move table and an animation list. The
// move table is 279 entries of 0x18 bytes, six 32-bit words each, stored LZSS-compressed. The game loads it (for each
// wrestler in a match, into D_80075468 + 0x10 + owner * 0x1A30) and, when a wrestler starts a move, unpacks one entry
// into the wrestler's WrMove (func_800A7C40 and unpackAnimationParameters, in the decompilation). Only the small tables
// that say which entry is which are in the game code's data: D_80030570 (the entry's first animation in the animation
// list) and D_800E3C20 (the entries of the twelve grapple moves). This window reads the resources with the game's own
// loader (res_load), in the PC's byte order, and those two tables from the ROM.
#include "move_list.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

#include "ship/config/ConsoleVariable.h"
#include "ship/core/Context.h"
#include "ship/debug/Console.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include "ship/window/gui/GuiWindow.h"

extern "C" {
#include "game.h"
#include "resources.h"
}

namespace td {

namespace {

constexpr uint32_t kProfileFirst = 0x12B; // the wrestlers' resources: profile, move table, animation list, ...
constexpr uint32_t kProfileLast = 0x20C;
constexpr uint32_t kSharedMoves = 0x126;  // a move table of 60 entries that is not any one wrestler's
constexpr uint32_t kEntryBytes = 0x18;
constexpr uint32_t kSlots = 279; // entries of a wrestler's move table
constexpr uint16_t kAnimationBase = 0x110B; // func_800C2E50 and func_800D5438 add it to the list's value

// Game data in the ROM, by ROM address (the main segment's RAM address minus 0x7FFFF400, overlay 2's from splat.yaml).
constexpr uint32_t kRomSlotAnimations = 0x31170; // D_80030570: u16 [279], each entry's first index in the list
constexpr uint32_t kRomGrappleSlots = 0xD70C0;   // D_800E3C20: u16 [2][2][3], the grapple moves' entries
constexpr uint32_t kRomBanks = 0x39664;          // D_80038A64: 10 addresses of u16 lists of profiles
constexpr uint32_t kBanks = 10;
constexpr uint32_t kMainRamToRom = 0x7FFFF400;

// One field of an entry, as unpackAnimationParameters takes it: `bits` bits of word `word` from bit `shift`. A signed
// field is a 6-bit value whose bit 5 the game extends into the byte's top bits. Names and order are WrMove's
// (include/wrestler.h in the decompilation).
struct Field {
    const char* name;
    uint8_t word;
    uint8_t shift;
    uint8_t bits;
    bool sign;
};

constexpr Field kFields[] = {
    { "unk38C", 0, 0, 11, false },          { "nameId", 5, 5, 12, false },
    { "power", 0, 11, 6, false },           { "spiritGainSelf", 0, 17, 6, true },
    { "spiritDeltaTarget", 0, 23, 6, true }, { "stunBits", 1, 0, 3, false },
    { "koBits", 1, 3, 3, false },           { "attackPart", 1, 6, 3, false },
    { "targetPart", 1, 9, 3, false },       { "activeJoint[0]", 2, 0, 5, false },
    { "activeJoint[1]", 2, 5, 5, false },   { "activeJoint[2]", 2, 10, 5, false },
    { "targetJoint", 5, 0, 5, false },      { "unk39B", 3, 22, 3, false },
    { "partDamage[0]", 3, 28, 3, false },   { "partDamage[1]", 4, 0, 3, false },
    { "partDamage[2]", 4, 3, 3, false },    { "partDamage[3]", 1, 28, 4, false },
    { "partDamage[4]", 1, 12, 4, false },   { "unk3A1", 3, 25, 3, false },
    { "spiritOnFinish", 3, 16, 6, true },   { "moveCode", 3, 0, 8, false },
    { "unk3A4", 2, 20, 6, false },          { "unk3A5", 2, 15, 5, false },
    { "range", 3, 8, 8, false },            { "unk3A7", 2, 26, 6, false },
    { "unk3A8[0]", 1, 16, 6, true },        { "unk3A8[1]", 1, 22, 6, true },
    { "flags", 4, 6, 26, false },
};

int32_t FieldValue(const Field& f, const uint32_t* words) {
    uint32_t v = (words[f.word] >> f.shift) & ((f.bits == 32 ? 0u : 1u << f.bits) - 1u);

    return f.sign && (v & 0x20) ? static_cast<int32_t>(v) - 0x40 : static_cast<int32_t>(v);
}

uint16_t RomU16(uint32_t at) {
    uint8_t b[2];

    port_rom_read(b, at, 2);
    return static_cast<uint16_t>(b[0] << 8 | b[1]);
}

uint32_t RomU32(uint32_t at) {
    uint8_t b[4];

    port_rom_read(b, at, 4);
    return static_cast<uint32_t>(b[0]) << 24 | b[1] << 16 | b[2] << 8 | b[3];
}

std::string Hex(uint32_t v, int digits) {
    char s[16];

    std::snprintf(s, sizeof s, "0x%0*" PRIX32, digits, v);
    return s;
}

// One resource as loaded, and where it is in the ROM.
struct Resource {
    uint32_t id = 0;
    GameResDesc desc{};
    std::vector<uint8_t> bytes;
};

Resource Load(uint32_t id) {
    Resource r;
    uint8_t* buf = res_load(id, &r.desc);

    r.id = id;
    r.bytes.assign(buf, buf + r.desc.size);
    std::free(buf);
    return r;
}

// A move table, with its animation list when it is a wrestler's.
struct Table {
    uint32_t profile = 0; // 0 for the shared table
    Resource moves;
    Resource animations;

    uint32_t Entries() const {
        return static_cast<uint32_t>(moves.bytes.size() / kEntryBytes);
    }
    void Words(uint32_t slot, uint32_t* out) const {
        std::memcpy(out, moves.bytes.data() + slot * kEntryBytes, kEntryBytes);
    }
};

Table LoadTable(uint32_t profile) {
    Table t;

    t.profile = profile;
    if (profile == 0) {
        t.moves = Load(kSharedMoves);
    } else {
        t.moves = Load(profile + 1);
        t.animations = Load(profile + 2);
    }
    return t;
}

// What the game's own tables say about the entries: each entry's first animation index, and the grapple entries.
struct SlotInfo {
    uint16_t firstAnimation[kSlots];
    std::string label[kSlots];
};

SlotInfo ReadSlotInfo() {
    SlotInfo s;
    static const char* const kStrength[2] = { "weak", "strong" };
    static const char* const kButton[2] = { "A", "B" };
    static const char* const kDirection[3] = { "", " + 0x800 (up)", " + 0x400 (down)" };

    for (uint32_t i = 0; i < kSlots; i++) {
        s.firstAnimation[i] = RomU16(kRomSlotAnimations + i * 2);
    }
    // D_800E3C20[i][j][k] (func_800D00FC): i is the grapple's strength (grappleCtx & 2, set by holding the button,
    // runGrappleAttempt), j 0 with button A (pad bit 0x8000) and 1 without (B), k the pad's direction bits.
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            for (int k = 0; k < 3; k++) {
                uint16_t slot = RomU16(kRomGrappleSlots + ((i * 2 + j) * 3 + k) * 2);

                if (slot < kSlots) {
                    s.label[slot] = std::string(kStrength[i]) + " grapple, " + kButton[j] + kDirection[k];
                }
            }
        }
    }
    return s;
}

// The animations the game plays for an entry: the list's values from the entry's first index up to the next entry's,
// plus kAnimationBase.
std::vector<uint16_t> Animations(const Table& t, const SlotInfo& info, uint32_t slot) {
    std::vector<uint16_t> out;
    size_t count = t.animations.bytes.size() / 2;

    if (t.profile == 0 || slot >= kSlots) {
        return out;
    }
    size_t end = slot + 1 < kSlots ? info.firstAnimation[slot + 1] : count;
    for (size_t i = info.firstAnimation[slot]; i < end && i < count; i++) {
        uint16_t v;

        std::memcpy(&v, t.animations.bytes.data() + i * 2, 2);
        out.push_back(static_cast<uint16_t>(v + kAnimationBase));
    }
    return out;
}

// The character ids that use each profile, from D_80038A64: bank b's list, entry e is character (b << 8) | (e + 1)
// (func_800C33F4). A bank's list runs up to the next bank's, the last one up to the table itself.
std::vector<std::vector<uint16_t>> ReadCharacters() {
    std::vector<std::vector<uint16_t>> users(kProfileLast - kProfileFirst + 1);
    uint32_t start[kBanks + 1];

    for (uint32_t b = 0; b < kBanks; b++) {
        start[b] = RomU32(kRomBanks + b * 4) - kMainRamToRom;
    }
    start[kBanks] = kRomBanks;
    for (uint32_t b = 0; b < kBanks; b++) {
        for (uint32_t at = start[b], e = 0; at + 2 <= start[b + 1]; at += 2, e++) {
            uint16_t profile = RomU16(at);

            if (profile >= kProfileFirst && profile <= kProfileLast && (profile - kProfileFirst) % 3 == 0) {
                users[profile - kProfileFirst].push_back(static_cast<uint16_t>(b << 8 | (e + 1)));
            }
        }
    }
    return users;
}

void WriteResource(FILE* f, const char* key, const Resource& r) {
    std::fprintf(f,
                 "    \"%s\": { \"resource\": \"%s\", \"romStart\": \"%s\", \"romEnd\": \"%s\", \"compressed\": %s, "
                 "\"bytes\": %d },\n",
                 key, Hex(r.id, 3).c_str(), Hex(r.desc.start, 7).c_str(), Hex(r.desc.end, 7).c_str(),
                 r.desc.packed > 0 ? "true" : "false", static_cast<int>(r.desc.size));
}

void WriteTable(FILE* f, const Table& t, const SlotInfo& info, const std::vector<uint16_t>* characters) {
    std::fprintf(f, "  {\n");
    if (t.profile != 0) {
        std::fprintf(f, "    \"profile\": \"%s\",\n    \"characterIds\": [", Hex(t.profile, 3).c_str());
        for (size_t i = 0; characters != nullptr && i < characters->size(); i++) {
            std::fprintf(f, "%s\"%s\"", i ? ", " : "", Hex((*characters)[i], 4).c_str());
        }
        std::fprintf(f, "],\n");
        WriteResource(f, "animationList", t.animations);
    } else {
        std::fprintf(f, "    \"profile\": null,\n    \"shared\": true,\n");
    }
    WriteResource(f, "moveTable", t.moves);
    std::fprintf(f, "    \"moves\": [\n");
    for (uint32_t slot = 0; slot < t.Entries(); slot++) {
        uint32_t w[6];

        t.Words(slot, w);
        std::fprintf(f, "      { \"slot\": \"%s\", \"offset\": \"%s\", \"words\": [", Hex(slot, 3).c_str(),
                     Hex(slot * kEntryBytes, 4).c_str());
        for (int i = 0; i < 6; i++) {
            std::fprintf(f, "%s\"%s\"", i ? ", " : "", Hex(w[i], 8).c_str());
        }
        std::fprintf(f, "], ");
        if (t.profile != 0 && slot < kSlots && !info.label[slot].empty()) {
            std::fprintf(f, "\"label\": \"%s\", ", info.label[slot].c_str());
        }
        std::fprintf(f, "\"animations\": [");
        auto anims = Animations(t, info, slot);
        for (size_t i = 0; i < anims.size(); i++) {
            std::fprintf(f, "%s\"%s\"", i ? ", " : "", Hex(anims[i], 4).c_str());
        }
        std::fprintf(f, "], \"fields\": { ");
        for (size_t i = 0; i < sizeof kFields / sizeof kFields[0]; i++) {
            std::fprintf(f, "%s\"%s\": %d", i ? ", " : "", kFields[i].name,
                         static_cast<int>(FieldValue(kFields[i], w)));
        }
        std::fprintf(f, " } }%s\n", slot + 1 < t.Entries() ? "," : "");
    }
    std::fprintf(f, "    ]\n  }");
}

// Writes tables to path as JSON. Returns false when the file cannot be written.
bool SaveJson(const std::string& path, const std::vector<const Table*>& tables, const SlotInfo& info,
              const std::vector<std::vector<uint16_t>>& characters) {
    FILE* f = std::fopen(path.c_str(), "w");

    if (f == nullptr) {
        return false;
    }
    std::fprintf(f, "{\n  \"entryBytes\": %u,\n  \"animationBase\": \"%s\",\n  \"fieldLayout\": [\n", kEntryBytes,
                 Hex(kAnimationBase, 4).c_str());
    for (size_t i = 0; i < sizeof kFields / sizeof kFields[0]; i++) {
        const Field& fl = kFields[i];
        std::fprintf(f, "    { \"name\": \"%s\", \"word\": %d, \"firstBit\": %d, \"bits\": %d, \"signed\": %s }%s\n",
                     fl.name, fl.word, fl.shift, fl.bits, fl.sign ? "true" : "false",
                     i + 1 < sizeof kFields / sizeof kFields[0] ? "," : "");
    }
    std::fprintf(f, "  ],\n  \"tables\": [\n");
    for (size_t i = 0; i < tables.size(); i++) {
        const Table& t = *tables[i];
        WriteTable(f, t, info, t.profile != 0 ? &characters[t.profile - kProfileFirst] : nullptr);
        std::fprintf(f, "%s\n", i + 1 < tables.size() ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");
    return std::fclose(f) == 0;
}

// The game's own tables about the entries, read from the ROM the first time they are needed.
const SlotInfo& Info() {
    static SlotInfo info = ReadSlotInfo();
    return info;
}

const std::vector<std::vector<uint16_t>>& Characters() {
    static std::vector<std::vector<uint16_t>> characters = ReadCharacters();
    return characters;
}

// Saves one table (a profile, or 0 for the shared table) or, with all, every table, as JSON beside the program, and
// says what it did.
std::string Save(uint32_t profile, bool all) {
    std::vector<Table> tables;
    std::vector<const Table*> list;
    std::string name = all ? "moves_all" : profile != 0 ? "moves_" + Hex(profile, 3) : "moves_shared";
    std::string path = Ship::Context::GetPathRelativeToAppDirectory(name + ".json");

    if (all) {
        for (uint32_t p = kProfileFirst; p <= kProfileLast; p += 3) {
            tables.push_back(LoadTable(p));
        }
        tables.push_back(LoadTable(0));
    } else {
        tables.push_back(LoadTable(profile));
    }
    for (const Table& t : tables) {
        list.push_back(&t);
    }
    return SaveJson(path, list, Info(), Characters()) ? "Saved " + path : "Could not write " + path;
}

class MoveListWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;

  protected:
    void UpdateElement() override {
    }

    void DrawElement() override {
        // The game reads its resource table at boot (initializeAssetSizeTable); until then nothing can be loaded.
        if (D_800482E4 == nullptr) {
            ImGui::TextWrapped("The game has not read its resource table yet.");
            return;
        }
        DrawChoice();
        if (mTable.moves.id == 0 || mTable.profile != mChosen) {
            mTable = LoadTable(mChosen);
        }
        DrawResources();
        DrawSave();
        mFilter.Draw("Filter", 200.0f);
        ImGui::SameLine();
        ImGui::Checkbox("Hide empty entries", &mHideEmpty);
        ImGui::Separator();
        DrawEntries();
    }

  private:
    std::string ProfileName(uint32_t profile) const {
        if (profile == 0) {
            return "Shared table " + Hex(kSharedMoves, 3);
        }
        std::string s = "Profile " + Hex(profile, 3);
        const auto& users = Characters()[profile - kProfileFirst];
        if (!users.empty()) {
            s += "  (character";
            s += users.size() > 1 ? "s " : " ";
            for (size_t i = 0; i < users.size() && i < 4; i++) {
                s += (i ? ", " : "") + Hex(users[i], 4);
            }
            s += users.size() > 4 ? ", ...)" : ")";
        }
        return s;
    }

    void DrawChoice() {
        if (!ImGui::BeginCombo("Wrestler", ProfileName(mChosen).c_str())) {
            return;
        }
        for (uint32_t p = kProfileFirst; p <= kProfileLast + 3; p += 3) {
            uint32_t profile = p <= kProfileLast ? p : 0; // the shared table last
            if (ImGui::Selectable(ProfileName(profile).c_str(), profile == mChosen)) {
                mChosen = profile;
            }
        }
        ImGui::EndCombo();
    }

    static void DrawResource(const char* what, const Resource& r, const char* unit) {
        if (r.desc.packed > 0) {
            ImGui::TextWrapped("%s: resource 0x%03X, ROM 0x%07X..0x%07X, compressed, %d bytes unpacked%s", what, r.id,
                        r.desc.start, r.desc.end, static_cast<int>(r.desc.size), unit);
        } else {
            ImGui::TextWrapped("%s: resource 0x%03X, ROM 0x%07X..0x%07X, %d bytes%s", what, r.id, r.desc.start,
                               r.desc.end, static_cast<int>(r.desc.size), unit);
        }
    }

    void DrawResources() {
        char unit[64];

        std::snprintf(unit, sizeof unit, " (%u entries of 0x%X bytes)", mTable.Entries(), kEntryBytes);
        DrawResource("Move table", mTable.moves, unit);
        if (mTable.profile != 0) {
            DrawResource("Animation list", mTable.animations, "");
        }
    }

    void DrawSave() {
        if (ImGui::Button("Save as JSON")) {
            mSaved = Save(mTable.profile, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save all as JSON")) {
            mSaved = Save(0, true);
        }
        if (!mSaved.empty()) {
            ImGui::TextWrapped("%s", mSaved.c_str());
        }
    }

    void DrawEntries() {
        if (!ImGui::BeginChild("entries")) {
            ImGui::EndChild();
            return;
        }
        for (uint32_t slot = 0; slot < mTable.Entries(); slot++) {
            uint32_t w[6];
            char label[160];

            mTable.Words(slot, w);
            if (mHideEmpty && (w[0] | w[1] | w[2] | w[3] | w[4] | w[5]) == 0) {
                continue;
            }
            auto anims = Animations(mTable, Info(), slot);
            const std::string& name = mTable.profile != 0 && slot < kSlots ? Info().label[slot] : std::string();
            std::snprintf(label, sizeof label, "0x%03X  +0x%04X  %s%s  %s", slot, slot * kEntryBytes,
                          anims.empty() ? "" : "anim ", anims.empty() ? "" : Hex(anims[0], 4).c_str(), name.c_str());
            if (!mFilter.PassFilter(label)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(slot));
            if (ImGui::TreeNode(label)) {
                DrawEntry(w, anims);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    static void DrawEntry(const uint32_t* w, const std::vector<uint16_t>& anims) {
        ImGui::Text("words  %08X %08X %08X %08X %08X %08X", w[0], w[1], w[2], w[3], w[4], w[5]);
        if (!anims.empty()) {
            std::string s = "animations ";
            for (uint16_t a : anims) {
                s += " " + Hex(a, 4);
            }
            ImGui::TextUnformatted(s.c_str());
        }
        if (!ImGui::BeginTable("fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                ImGuiTableFlags_SizingFixedFit)) {
            return;
        }
        ImGui::TableSetupColumn("field");
        ImGui::TableSetupColumn("bits");
        ImGui::TableSetupColumn("value");
        ImGui::TableHeadersRow();
        for (const Field& f : kFields) {
            int32_t v = FieldValue(f, w);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(f.name);
            ImGui::TableNextColumn();
            ImGui::Text("w%d %d..%d%s", f.word, f.shift, f.shift + f.bits - 1, f.sign ? " signed" : "");
            ImGui::TableNextColumn();
            ImGui::Text("%d (0x%X)", static_cast<int>(v), static_cast<unsigned>(v) & ((1u << f.bits) - 1u));
        }
        ImGui::EndTable();
    }

    uint32_t mChosen = kProfileFirst;
    Table mTable;
    ImGuiTextFilter mFilter;
    bool mHideEmpty = false;
    std::string mSaved; // what the last save did
};

} // namespace

void AddMoveListWindow(const std::shared_ptr<Ship::ConsoleVariable>& cvars,
                       const std::shared_ptr<Ship::Window>& window) {
    auto console = window->GetContext()->GetChildren().GetFirst<Ship::Console>();

    if (console != nullptr) {
        console->AddCommand(
            "moves", { [](std::shared_ptr<Ship::Console>, std::vector<std::string> args, std::string* output) {
                          std::string which = args.size() > 1 ? args[1] : "all";
                          uint32_t profile = static_cast<uint32_t>(std::strtoul(which.c_str(), nullptr, 0));

                          if (D_800482E4 == nullptr) {
                              *output = "The game has not read its resource table yet.";
                              return 1;
                          }
                          if (which == "all" || which == "shared") {
                              *output = Save(0, which == "all");
                              return 0;
                          }
                          if (profile < kProfileFirst || profile > kProfileLast || (profile - kProfileFirst) % 3 != 0) {
                              *output = "Not a profile: give 0x12B, 0x12E, ... 0x20C, or shared, or all.";
                              return 1;
                          }
                          *output = Save(profile, false);
                          return 0;
                      },
                       "Save move tables as JSON beside the program: one profile (such as 0x137), shared, or all",
                       { { "profile", Ship::ArgumentType::TEXT, true } } });
    }
    window->GetGui()->AddGuiWindow(std::make_shared<MoveListWindow>(cvars, window, "gMoveListShown", false,
                                                                    kMoveListWindow, ImVec2(640, 560),
                                                                    ImGuiWindowFlags_None));
}

} // namespace td
