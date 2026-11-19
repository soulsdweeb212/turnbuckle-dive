// Reading the game's archive with LibUltraShip (src/archive.h says what it holds). A ResourceManager of its own mounts
// the archive (LibUltraShip's archives read their files through the ResourceManager that owns them), so that this works
// in every program that links LibUltraShip, with or without a window; files are found by their path, as LibUltraShip
// finds every file of an .o2r archive.
//
// The ResourceManager is made once and kept for the rest of the run. Its ArchiveManager holds it and it holds the
// ArchiveManager, so once made, the two can never be freed (LibUltraShip's own Context has the same pair); kept in
// gResources, they stay reachable rather than lost. Closing the archive drops it from the ArchiveManager, which closes
// its file, and the next archive_open mounts the next archive on the same ResourceManager.
#include "archive.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <stdexcept>

#include "ship/resource/File.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"

extern "C" {
#include "game.h"
}

namespace {

std::shared_ptr<Ship::ResourceManager> gResources;
std::vector<uint8_t> gIndex; // TD_ARCHIVE_INDEX, GAME_RES_COUNT entries of TD_ARCHIVE_INDEX_ENTRY bytes

std::shared_ptr<std::vector<char>> Load(const std::string& path) {
    auto file = gResources->GetArchiveManager()->LoadFile(path);

    if (file == nullptr || !file->IsLoaded || file->Buffer == nullptr) {
        return nullptr;
    }
    return file->Buffer;
}

void Fail(char* why, size_t whySize, const std::string& text) {
    std::snprintf(why, whySize, "%s", text.c_str());
}

// port_res_read's source while the archive is open: the bytes of the resource's file, which must have exactly the
// size the resource table gives.
int FromArchive(uint32_t id, void* buf, int32_t size) {
    if (id < 1 || id > GAME_RES_COUNT) {
        port_fatal("archive: resource id 0x%X is outside 1..0x%X", id, GAME_RES_COUNT);
    }
    const uint8_t* entry = gIndex.data() + (id - 1) * TD_ARCHIVE_INDEX_ENTRY;
    uint32_t owner = static_cast<uint32_t>(entry[0] << 8 | entry[1]);
    char name[64];

    std::snprintf(name, sizeof(name), TD_ARCHIVE_RES_FILE, owner);
    auto bytes = Load(name);
    if (bytes == nullptr) {
        port_fatal("archive: resource 0x%X's file %s is not in the game's archive", id, name);
    }
    if (size < 0 || bytes->size() != static_cast<size_t>(size)) {
        port_fatal("archive: resource 0x%X's file %s has 0x%zX bytes, but the resource table gives 0x%X", id, name,
                   bytes->size(), static_cast<unsigned>(size));
    }
    if (size > 0) {
        std::memcpy(buf, bytes->data(), static_cast<size_t>(size));
    }
    return 1;
}

uint32_t Be32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 |
           p[3];
}

} // namespace

extern "C" int archive_open(const char* path, Rom* rom, char* why, size_t whySize) {
    std::error_code ec;

    archive_close();
    std::memset(rom, 0, sizeof(*rom));
    if (!std::filesystem::is_regular_file(path, ec)) {
        Fail(why, whySize, std::string("no archive at ") + path);
        return -1;
    }
    try {
        if (gResources == nullptr) {
            gResources = std::make_shared<Ship::ResourceManager>();
            gResources->Init({ { "archivePaths", std::vector<std::string>{ path } } });
        } else if (gResources->GetArchiveManager()->AddArchive(std::string(path)) == nullptr) {
            throw std::runtime_error("the archive did not mount");
        }
    } catch (const std::exception& e) {
        archive_close();
        Fail(why, whySize, std::string("LibUltraShip could not open ") + path + ": " + e.what());
        return -1;
    }

    auto stamp = Load(TD_ARCHIVE_STAMP);
    auto low = Load(TD_ARCHIVE_ROM_LOW);
    auto high = Load(TD_ARCHIVE_ROM_HIGH);
    auto index = Load(TD_ARCHIVE_INDEX);
    if (stamp == nullptr || low == nullptr || high == nullptr || index == nullptr) {
        archive_close();
        Fail(why, whySize, std::string(path) + " is not the game's archive");
        return -1;
    }
    int format = 0;
    char sha1[41] = {};
    std::string text(stamp->begin(), stamp->end());
    if (std::sscanf(text.c_str(), "format %d rom %40s", &format, sha1) != 2 || format != TD_ARCHIVE_FORMAT) {
        archive_close();
        Fail(why, whySize, std::string(path) + " was written in another format");
        return -1;
    }
    if (std::strcmp(sha1, TD_ROM_SHA1) != 0) {
        archive_close();
        Fail(why, whySize, std::string(path) + " was written from another ROM");
        return -1;
    }
    if (low->size() != TD_ROM_RES_DATA || high->size() < 0x77A8u ||
        index->size() != static_cast<size_t>(GAME_RES_COUNT) * TD_ARCHIVE_INDEX_ENTRY) {
        archive_close();
        Fail(why, whySize, std::string(path) + " has ROM ranges or an index of the wrong size");
        return -1;
    }

    // The ROM image: the two ranges, each resource's first 4 bytes at its start, written in the table's order (a
    // resource of fewer than 4 bytes has the next one's first bytes after its own, as in the ROM), and then the whole of
    // every resource stored unpacked, whose file holds exactly its ROM bytes: the game streams the animations from the
    // ROM a piece at a time (initializeResourceStream), not through the loader.
    size_t size = TD_ROM_RES_TABLE + high->size();
    uint8_t* image = static_cast<uint8_t*>(std::calloc(size, 1));
    if (image == nullptr) {
        archive_close();
        Fail(why, whySize, "out of memory");
        return -1;
    }
    std::memcpy(image, low->data(), low->size());
    std::memcpy(image + TD_ROM_RES_TABLE, high->data(), high->size());
    gIndex.assign(index->begin(), index->end());
    for (uint32_t id = 1; id <= GAME_RES_COUNT; id++) {
        uint32_t start = TD_ROM_RES_DATA + (Be32(image + TD_ROM_RES_TABLE + (id - 1) * 4) & ~1u);

        if (start + 4 > size) {
            std::free(image);
            archive_close();
            Fail(why, whySize, std::string(path) + " has a resource table that points past the ROM");
            return -1;
        }
        std::memcpy(image + start, gIndex.data() + (id - 1) * TD_ARCHIVE_INDEX_ENTRY + 2, 4);
    }
    for (uint32_t id = 1; id <= GAME_RES_COUNT; id++) {
        uint32_t word = Be32(image + TD_ROM_RES_TABLE + (id - 1) * 4);
        uint32_t start = TD_ROM_RES_DATA + (word & ~1u);
        uint32_t end = TD_ROM_RES_DATA + (Be32(image + TD_ROM_RES_TABLE + id * 4) & ~1u);
        const uint8_t* entry = gIndex.data() + (id - 1) * TD_ARCHIVE_INDEX_ENTRY;
        char name[64];

        if (word & 1u) {
            continue;
        }
        std::snprintf(name, sizeof(name), TD_ARCHIVE_RES_FILE, static_cast<unsigned>(entry[0] << 8 | entry[1]));
        auto bytes = Load(name);
        if (end < start || end > TD_ROM_RES_TABLE || bytes == nullptr || bytes->size() != end - start) {
            std::free(image);
            archive_close();
            Fail(why, whySize, std::string(path) + ": " + name + " is missing or has the wrong size");
            return -1;
        }
        std::memcpy(image + start, bytes->data(), bytes->size());
    }

    rom->data = image;
    rom->size = size;
    rom->order = ROM_ORDER_Z64;
    std::memcpy(rom->sha1, sha1, sizeof(rom->sha1));
    for (int i = 0; i < 20; i++) {
        rom->name[i] = static_cast<char>(image[0x20 + i]);
    }
    for (int i = 19; i >= 0 && (rom->name[i] == ' ' || rom->name[i] == '\0'); i--) {
        rom->name[i] = '\0';
    }
    rom->sparse = 1;
    port_res_source(FromArchive);
    return 0;
}

// Drops the archive from the ArchiveManager (as LibUltraShip's ArchiveManager does when it is destroyed), which closes
// its file; the ResourceManager stays (above).
extern "C" void archive_close(void) {
    port_res_source(nullptr);
    gIndex.clear();
    if (gResources != nullptr && gResources->GetArchiveManager() != nullptr) {
        gResources->GetArchiveManager()->SetArchives(nullptr);
    }
}
