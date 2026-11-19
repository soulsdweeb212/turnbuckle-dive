// The player's launch: the game's archive, written once from the player's ROM (src/first_launch.h says what it does).
extern "C" {
#include "rom.h"
}
#include "archive.h"
#include "first_launch.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "ship/core/Context.h"

namespace fs = std::filesystem;

namespace {

// The supported ROM's size, 16 MiB; a file of any other size is never it, so the search reads no other file.
constexpr std::uintmax_t kRomSize = 0x1000000;

std::string gShown; // the file first_launch_open shows the player (its *shown)

bool HasRomName(const fs::path& path) {
    std::string ext = path.extension().string();

    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".z64" || ext == ".n64" || ext == ".v64";
}

// The files in dir that may be the ROM: named as a ROM and of its size, in name order, so that the same one is chosen
// each time.
std::vector<fs::path> RomsIn(const fs::path& dir) {
    std::vector<fs::path> found;
    std::error_code ec;

    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;

        if (it->is_regular_file(fileEc) && HasRomName(it->path()) && it->file_size(fileEc) == kRomSize && !fileEc) {
            found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

bool SameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;

    return fs::equivalent(a, b, ec) && !ec;
}

// Writes the archive from rom, the supported ROM read from romPath, and opens it into rom in place of the ROM. When the
// archive cannot be written or opened, rom stays the ROM, which the game then runs from this time.
RomStatus WriteAndOpen(Rom* rom, const std::string& romPath, const std::string& archive) {
    char why[512];
    Rom image;
    std::error_code ec;

    std::printf("Writing the game's archive %s from the ROM %s; the port starts from the archive from now on\n",
                archive.c_str(), romPath.c_str());
    std::fflush(stdout);
    fs::create_directories(fs::path(archive).parent_path(), ec);
    if (archive_write(rom, archive.c_str(), why, sizeof(why)) != 0 ||
        archive_open(archive.c_str(), &image, why, sizeof(why)) != 0) {
        std::fprintf(stderr, "The game's archive could not be written: %s\nThe game runs from the ROM this time.\n",
                     why);
        gShown = romPath;
        return ROM_OK;
    }
    rom_free(rom);
    *rom = image;
    gShown = archive;
    return ROM_OK;
}

} // namespace

extern "C" RomStatus first_launch_open(const char* rom_arg, Rom* rom, const char** shown) {
    const fs::path folder = Ship::Context::GetAppDirectoryPath();
    const std::string archive = (folder / TD_ARCHIVE_NAME).string();
    const char* env = std::getenv("TD_ROM");
    char why[512];

    *shown = nullptr;
    // 1. A ROM given: that one, and the archive written from it unless the folder has one that can be used.
    if (rom_arg != nullptr || (env != nullptr && env[0] != '\0')) {
        gShown = rom_find(rom_arg);
        *shown = gShown.c_str();
        RomStatus status = rom_load(gShown.c_str(), rom);
        if (status != ROM_OK) {
            return status;
        }
        Rom image;
        if (archive_open(archive.c_str(), &image, why, sizeof(why)) == 0) {
            rom_free(rom);
            *rom = image;
            gShown = archive;
        } else {
            WriteAndOpen(rom, gShown, archive);
        }
        *shown = gShown.c_str();
        return ROM_OK;
    }

    // 2. No ROM given: the archive, when it can be used.
    std::error_code ec;
    bool hadArchive = fs::exists(archive, ec);
    if (archive_open(archive.c_str(), rom, why, sizeof(why)) == 0) {
        gShown = archive;
        *shown = gShown.c_str();
        return ROM_OK;
    }
    if (hadArchive) {
        std::printf("The game's archive cannot be used (%s); it is written again from the ROM\n", why);
    }

    // The ROM: in the port's folder, the program's folder, the current folder, then where rom_find looks.
    std::vector<fs::path> folders = { folder };
    for (const fs::path& more : { fs::path(Ship::Context::GetAppBundlePath()), fs::current_path(ec) }) {
        if (!more.empty() && std::none_of(folders.begin(), folders.end(),
                                          [&](const fs::path& known) { return SameFolder(known, more); })) {
            folders.push_back(more);
        }
    }
    std::vector<fs::path> candidates;
    for (const fs::path& dir : folders) {
        for (const fs::path& file : RomsIn(dir)) {
            candidates.push_back(file);
        }
    }
    fs::path fallback = rom_find(nullptr);
    if (fs::is_regular_file(fallback, ec) &&
        std::none_of(candidates.begin(), candidates.end(),
                     [&](const fs::path& known) { std::error_code same; return fs::equivalent(known, fallback, same); })) {
        candidates.push_back(fallback);
    }
    std::vector<std::string> rejected;
    for (const fs::path& file : candidates) {
        std::string path = file.string();
        RomStatus status = rom_load(path.c_str(), rom);

        if (status == ROM_OK) {
            gShown = path;
            WriteAndOpen(rom, gShown, archive);
            *shown = gShown.c_str();
            return ROM_OK;
        }
        if (status == ROM_WRONG_HASH) {
            rejected.push_back(path + ": not the supported ROM (SHA-1 " + rom->sha1 + ")");
            rom_free(rom);
        } else {
            rejected.push_back(path + ": " + rom_status_text(status));
        }
    }

    std::fflush(stdout);
    std::fprintf(stderr,
                 "No ROM found. Turnbuckle Dive needs your ROM once, to write the game's archive %s; after that it "
                 "starts from the archive.\n"
                 "Put the ROM (a .z64, .n64 or .v64 file of 16 MiB, under any name) in %s and start again, or pass "
                 "its path as the first argument. The supported ROM's SHA-1 is %s.\n",
                 archive.c_str(), folder.string().c_str(), TD_ROM_SHA1);
    for (const std::string& line : rejected) {
        std::fprintf(stderr, "Looked at %s\n", line.c_str());
    }
    return ROM_NOT_FOUND;
}
