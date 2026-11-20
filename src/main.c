#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "audio/sound.h"
#include "audio/sound_dump.h"
#include "audio/sound_replay.h"
#ifdef TD_GAME_CODE
#include "audio/music_dump.h"
#endif
#include "rom.h"
#ifdef TD_ARCHIVE
#include "archive.h"
#include "first_launch.h"
#endif
#ifdef TD_GAME_CODE
#include "resources.h"
#endif
#ifdef TD_DEV_MATCH
#include "dev/dev_boot.h"
#include "dev/dev_match.h"
#endif
#ifdef TD_INSPECTOR
#include "dev/inspector.h"
#include "splash_faces.h"
#endif

/* The first boot frame after the game's legal screen and the THQ logo (frames 1 to 220: the legal screen to 92, its
 * cross-fade into the THQ logo to 124, the THQ logo to 188, its cross-fade into the Asmik Ace logo to 220). */
#define BOOT_LOGOS_END 221

#ifndef TD_VERSION
#define TD_VERSION "dev"
#endif
#include "td_commit.h" /* TD_COMMIT, the commit the program is built from (cmake/commit.cmake) */

/* Reads a resource id written in hex with 0x (0x1DE9) or in decimal (7657). Returns 0 if the text is not one. */
static uint32_t parse_res_id(const char *text) {
    int base = 10;
    char *end;
    unsigned long value;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    /* strtoul would also accept leading spaces and a minus sign. */
    if (!isxdigit((unsigned char)text[0])) {
        return 0;
    }
    value = strtoul(text, &end, base);
    if (*end != '\0' || value > GAME_RES_COUNT) {
        return 0;
    }
    return (uint32_t)value;
}

#ifdef TD_GAME_CODE
/* Runs the game's own ROM loader, compiled from the decompilation, on the checked ROM: with res_id 0 it loads every
 * resource and prints a summary, else only resource res_id, and prints its size and first bytes. A breakpoint in the
 * decompilation's src/main/decompressLzss.c (the game's LZSS decompressor) stops in here for each compressed one. */
static void run_game_loader(const Rom *rom, uint32_t res_id) {
    GameResDesc desc;
    uint8_t *buf;

    rom_attach(rom);
    res_init();
    if (res_id != 0) {
        int i, shown;

        buf = res_load(res_id, &desc);
        printf("Loaded: resource 0x%04X (%u), %d bytes", res_id, res_id, (int)desc.size);
        if (desc.packed > 0) {
            printf(", unpacked from %d bytes at ROM 0x%07X\n", (int)desc.packed, desc.start);
        } else {
            printf(", copied from ROM 0x%07X\n", desc.start);
        }
        shown = desc.size < 16 ? (int)desc.size : 16;
        printf("Bytes:  ");
        for (i = 0; i < shown; i++) {
            printf("%02X%s", buf[i], i + 1 < shown ? " " : "");
        }
        printf("%s\n", desc.size > 16 ? " ..." : "");
        free(buf);
    } else {
        uint32_t id, loaded = 0, compressed = 0;
        unsigned long long bytes = 0;

        for (id = 1; id <= GAME_RES_COUNT; id++) {
            buf = res_load(id, &desc);
            loaded++;
            if (desc.packed > 0) {
                compressed++;
            }
            bytes += (unsigned long long)desc.size;
            free(buf);
        }
        printf("Loaded: %u resources with the game's loader, %u of them compressed, %llu bytes\n", loaded, compressed,
               bytes);
    }
    res_shutdown();
}
#endif

int main(int argc, char **argv) {
    const char *rom_arg = NULL;
    const char *path;
    const char *archive_arg = NULL; /* --archive FILE: run from the game's archive instead of the ROM */
    const char *extract_arg = NULL; /* --extract FILE: write the game's archive from the ROM and stop */
    uint32_t res_id = 0;
    int dev_match = 0;
    unsigned dev_boot = 0; /* --dev-boot FRAMES: frames to run from the boot */
    int play = 0;          /* the player's launch, with no developer mode: the game, from its archive */
    unsigned play_frames = 0; /* --frames N: the player's game stops after N frames (the tests); 0 for no limit */
    int headless = 0;
    int no_splash = 0;
    const char *screenshot = NULL;
    unsigned screenshot_frame = 300;
    unsigned screenshot_every = 0;
    const char *dump = NULL;
    unsigned dump_frame = 0;
    const char *capture_dir = NULL;
    const char *sound_dump_dir = NULL; /* --sound-dump DIR: write the sound banks' samples as WAV files and stop */
    const char *music_dump_dir = NULL; /* --music-dump DIR: write the songs, played by the music player, and stop */
    const char *sound_replay_dir = NULL; /* --sound-replay DIR: replay a recording of the original's sound calls */
    const char *sound_lockstep_dir = NULL; /* --sound-lockstep DIR: the developer match's sound, in step with a recording */
    unsigned capture_frames[64];
    unsigned capture_count = 0;
#ifdef TD_DEV_MATCH
    DevMatchOptions match_options = { NULL, NULL, 0, 0, 0, NULL, NULL, 0, 0, 0, NULL, NULL, 0, 0, NULL, 0, 0 };
#endif
    RomStatus status;
    Rom rom;
    int i;

#ifdef TD_INSPECTOR
    dev_inspector_set_command(argc, argv); /* the F1 menu's Reset starts the program again with these */
#endif
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--archive") == 0 && i + 1 < argc) {
            archive_arg = argv[++i];
        } else if (strcmp(argv[i], "--extract") == 0 && i + 1 < argc) {
            extract_arg = argv[++i];
        } else if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
            res_id = parse_res_id(argv[++i]);
            if (res_id == 0) {
                fprintf(stderr, "--res %s: a resource id is 1 to %u, in decimal or in hex such as 0x1DE9\n", argv[i],
                        GAME_RES_COUNT);
                return 1;
            }
#ifndef TD_GAME_CODE
            fprintf(stderr, "--res: this build has no game code (README.md, Building, says how to build it)\n");
            return 1;
#endif
        } else if (strcmp(argv[i], "--sound-dump") == 0 && i + 1 < argc) {
            sound_dump_dir = argv[++i];
        } else if (strcmp(argv[i], "--sound-replay") == 0 && i + 1 < argc) {
            sound_replay_dir = argv[++i];
        } else if (strcmp(argv[i], "--sound-lockstep") == 0 && i + 1 < argc) {
            sound_lockstep_dir = argv[++i];
        } else if (strcmp(argv[i], "--music-dump") == 0 && i + 1 < argc) {
            music_dump_dir = argv[++i];
#ifndef TD_GAME_CODE
            fprintf(stderr, "--music-dump: this build has no game code (README.md, Building, says how to build it)\n");
            return 1;
#endif
        } else if (strcmp(argv[i], "--dev-boot") == 0 && i + 1 < argc) {
            dev_boot = (unsigned)strtoul(argv[++i], NULL, 0);
#ifndef TD_DEV_MATCH
            fprintf(stderr, "--dev-boot: this build does not link the game's files "
                            "(docs/GAME_CODE.md, \"Every file of the developer program\")\n");
            return 1;
#endif
        } else if (strcmp(argv[i], "--dev-match") == 0) {
            dev_match = 1;
#ifndef TD_DEV_MATCH
            fprintf(stderr, "--dev-match: this build does not link the developer program's game files "
                            "(docs/GAME_CODE.md, \"Every file of the developer program\")\n");
            return 1;
#endif
#ifdef TD_DEV_MATCH
        } else if (strcmp(argv[i], "--wr-log") == 0 && i + 1 < argc) {
            match_options.wr_log = argv[++i];
        } else if (strcmp(argv[i], "--res-log") == 0 && i + 1 < argc) {
            match_options.res_log = argv[++i];
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            play_frames = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--no-splash") == 0) {
            no_splash = 1;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--screenshot-frame") == 0 && i + 1 < argc) {
            screenshot_frame = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            match_options.players = atoi(argv[++i]) > 0 ? 1 : 0;
        } else if (strcmp(argv[i], "--time-limit") == 0 && i + 1 < argc) {
            int minutes = atoi(argv[++i]);
            match_options.time_limit = minutes == 0 ? -1 : minutes;
        } else if (strcmp(argv[i], "--matches") == 0 && i + 1 < argc) {
            match_options.matches = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--record-pads") == 0 && i + 1 < argc) {
            match_options.record_pads = argv[++i];
        } else if (strcmp(argv[i], "--replay-pads") == 0 && i + 1 < argc) {
            match_options.replay_pads = argv[++i];
        } else if (strcmp(argv[i], "--pad-script") == 0 && i + 1 < argc) {
            match_options.pad_script = argv[++i];
        } else if (strcmp(argv[i], "--pad-bot") == 0 && i + 1 < argc) {
            match_options.has_pad_bot = 1;
            match_options.pad_bot_seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--no-idle-randoms") == 0) {
            match_options.no_idle_randoms = 1;
        } else if (strcmp(argv[i], "--capture-match-frames") == 0 && i + 2 < argc) {
            char *list = argv[++i];
            capture_dir = argv[++i];
            while (*list != '\0' && capture_count < sizeof(capture_frames) / sizeof(capture_frames[0])) {
                char *end;
                capture_frames[capture_count++] = (unsigned)strtoul(list, &end, 0);
                list = *end == ',' ? end + 1 : end + strlen(end);
            }
        } else if (strcmp(argv[i], "--hide-group") == 0 && i + 1 < argc) {
            unsigned long group = strtoul(argv[++i], NULL, 0);
            if (group < 32) {
                match_options.hide_groups |= 1u << group;
            }
        } else if (strcmp(argv[i], "--screenshot-every") == 0 && i + 1 < argc) {
            screenshot_every = (unsigned)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--dump-frame") == 0 && i + 2 < argc) {
            dump_frame = (unsigned)strtoul(argv[++i], NULL, 0);
            dump = argv[++i];
        } else if (strcmp(argv[i], "--no-watchdog") == 0) {
            match_options.no_watchdog = 1;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            match_options.has_seed = 1;
            match_options.seed = (uint32_t)strtoul(argv[++i], NULL, 0);
#endif
        } else if (strncmp(argv[i], "--", 2) == 0 || rom_arg != NULL) {
            fprintf(stderr, "Usage: turnbuckle-dive [--archive FILE] [--extract FILE | --res ID | --sound-dump DIR | --music-dump DIR | --sound-replay DIR | --dev-boot FRAMES [--no-splash] [--wr-log FILE] [--sound-lockstep DIR] [--pad-script FILE] [--record-pads FILE] [--replay-pads FILE] [--no-idle-randoms] | --dev-match [--headless] [--sound-lockstep DIR] [--wr-log FILE] [--res-log FILE] [--seed N] "
                            "[--no-watchdog] "
                            "[--hide-group N]... [--screenshot FILE [--screenshot-frame N | --screenshot-every N]] [--dump-frame N FILE] [--no-idle-randoms] [--capture-match-frames N,N,... DIR] [--players 0|1] [--time-limit MINUTES] [--matches N] [--record-pads FILE] [--replay-pads FILE] [--pad-script FILE] [--pad-bot SEED]] [ROM]\n"
                            "With no mode, the game starts in its window, from its archive, which the first launch writes from the ROM;\n"
                            "--frames N stops it after N frames and --headless runs it without the window (for checks).\n");
            return 1;
        } else {
            rom_arg = argv[i];
        }
    }
    printf("Turnbuckle Dive %s, commit %s\n", TD_VERSION, TD_COMMIT);
#ifdef TD_ARCHIVE
    /* --archive: the ROM image comes from the game's archive (src/archive.h), and the game's loader takes each resource
     * from the archive's files. */
    if (archive_arg != NULL) {
        char why[512];

        if (archive_open(archive_arg, &rom, why, sizeof(why)) != 0) {
            fflush(stdout);
            fprintf(stderr, "Archive: %s\n", why);
            return 1;
        }
        path = archive_arg;
        status = ROM_OK;
    } else if (extract_arg == NULL && !dev_match && dev_boot == 0 && res_id == 0 && sound_dump_dir == NULL &&
               music_dump_dir == NULL && sound_replay_dir == NULL) {
        /* The player's launch, with no developer mode: the game's archive, written from the ROM on the first launch
         * (src/first_launch.h). The developer modes read the ROM itself, unless given --archive. */
        status = first_launch_open(rom_arg, &rom, &path);
        if (path == NULL) {
            return 1; /* no ROM given and none found; first_launch_open said where to put it */
        }
        play = 1;
    } else
#endif
    {
        path = rom_find(rom_arg);
        status = rom_load(path, &rom);
    }
    if (status != ROM_OK && status != ROM_WRONG_HASH) {
        /* stdout is buffered when it is not a terminal; flush it so messages keep their order. */
        fflush(stdout);
        fprintf(stderr, "ROM %s: %s\n", path, rom_status_text(status));
        fprintf(stderr, "Pass the path to your ROM as the first argument, or set TD_ROM.\n");
        /* 77: CTest reports the developer match's tests as skipped on a machine with no ROM, such as CI. */
        return dev_match && status == ROM_NOT_FOUND ? 77 : 1;
    }
    printf("%s %s\n", rom.sparse ? "Archive:" : "ROM:    ", path);
    printf("Size:   %zu bytes (%zu MiB)\n", rom.size, rom.size >> 20);
    printf("Order:  %s\n", rom_order_text(rom.order));
    printf("Name:   %s\n", rom.name);
    printf("SHA-1:  %s\n", rom.sha1);
    if (status == ROM_WRONG_HASH) {
        fflush(stdout);
        fprintf(stderr, "This ROM is not supported. Expected SHA-1 %s.\n", TD_ROM_SHA1);
        rom_free(&rom);
        return 2;
    }
    printf("Status: supported ROM%s\n", rom.sparse ? ", from the game's archive" : "");
#ifdef TD_ARCHIVE
    if (extract_arg != NULL) {
        char why[512];
        int result = archive_write(&rom, extract_arg, why, sizeof(why));

        if (result != 0) {
            fflush(stdout);
            fprintf(stderr, "Archive %s: %s\n", extract_arg, why);
        } else {
            printf("Archive: wrote %s\n", extract_arg);
        }
        rom_free(&rom);
        return result != 0;
    }
#endif
#ifdef TD_GAME_CODE
    if (music_dump_dir != NULL) {
        int result = music_dump(&rom, music_dump_dir, 90);

        rom_free(&rom);
        return result;
    }
#endif
    if (sound_replay_dir != NULL) {
        int result = sound_replay(rom.data, rom.size, sound_replay_dir);

        rom_free(&rom);
        return result;
    }
    if (sound_dump_dir != NULL) {
        int result = sound_dump(rom.data, rom.size, sound_dump_dir);

        rom_free(&rom);
        return result;
    }
#ifdef TD_DEV_MATCH
    /* The developer boot, and the player's game: the same boot in the same window, with no frame limit and no
     * progress lines for the player (--frames and --headless are for the tests). */
    if (play_frames != 0 && !play) {
        fprintf(stderr, "--frames is for the game started with no developer mode; --dev-boot takes its own frames\n");
        rom_free(&rom);
        return 1;
    }
    if (dev_boot != 0 || play) {
        int result;
        void (*on_frame)(void *) = NULL;

        if (play) {
            dev_boot_play();
        }
#ifdef TD_INSPECTOR
        /* The Match Inspector's window draws the frames, unless --headless (with no screen, the developer boot runs
         * without; the player's game cannot). */
        if (play && !headless && dev_inspector_start() != 0) {
            fprintf(stderr, "The game cannot start without a window.\n");
            rom_free(&rom);
            return 1;
        }
        if (!headless && (play || dev_inspector_start() == 0)) {
            on_frame = dev_inspector_frame;
            dev_inspector_frames_from(dev_boot_frames);
            /* The sound: the music player and the synthesizer, heard through the window's sound device. Not in a
             * scripted or replayed run: the game asks whether its music still plays before it starts the next song,
             * and the answer would hang on the sound device's timing, so such a run would not go the same each time. */
            if (match_options.pad_script == NULL && match_options.replay_pads == NULL && sound_lockstep_dir == NULL &&
                sound_start(rom.data, rom.size) == 0) {
                dev_inspector_open_sound();
            }
            /* The game's saves in a file beside the program, as a cartridge keeps them; a scripted or replayed run
             * starts with no save, so that it goes the same on every run. */
            if (match_options.pad_script == NULL && match_options.replay_pads == NULL) {
                dev_inspector_use_save_file();
            }
            if (screenshot != NULL && screenshot_every != 0) {
                dev_inspector_screenshots(screenshot, screenshot_every);
            } else if (screenshot != NULL) {
                dev_inspector_screenshot(screenshot, screenshot_frame);
            }
            if (dump != NULL) {
                dev_inspector_dump(dump, dump_frame);
            }
            /* The Turnbuckle Dive boot (F1, Settings, Boot): the splash screen in place of the game's legal screen and
             * THQ logo, unless the run is scripted or saves pictures. The game's first 220 frames are those two
             * screens, the same on every run; the window leaves them out, so the Asmik Ace logo follows the splash. The faces are made from the ROM; when they cannot be, the boot is the original. */
            if (!no_splash && dev_inspector_boot_screen() == DEV_BOOT_SCREEN_TURNBUCKLE_DIVE &&
                match_options.pad_script == NULL && match_options.replay_pads == NULL && screenshot == NULL &&
                dump == NULL) {
                static uint8_t faces[SPLASH_FACES * SPLASH_FACE_BYTES];

                if (splash_faces_load(&rom, faces) == 0) {
                    dev_inspector_splash(faces, TD_COMMIT);
                    dev_inspector_skip_frames(BOOT_LOGOS_END);
                }
            }
        }
#endif
        dev_boot_pads(match_options.replay_pads, match_options.record_pads, match_options.pad_script);
        dev_boot_idle_randoms(match_options.no_idle_randoms);
        if (match_options.wr_log != NULL) {
            dev_boot_wr_log(match_options.wr_log);
        }
        /* The sound in step with a recording of the original's boot (src/audio/sound_replay.h); its files are finished
         * when the boot ends at its last frame. */
        if (sound_lockstep_dir != NULL) {
            if (sound_lockstep_start(rom.data, rom.size, sound_lockstep_dir) != 0) {
                rom_free(&rom);
                return 1;
            }
            dev_boot_at_end(sound_lockstep_end);
        }
        result = dev_boot_run(&rom, play ? play_frames : dev_boot, on_frame, NULL);
        sound_end();
        rom_free(&rom);
        return result;
    }
    if (dev_match) {
        int result;

#ifdef TD_INSPECTOR
        /* The Match Inspector's window, unless --headless; with no screen to open it on, the match runs without it. */
        if (!headless) {
            if (dev_inspector_start() == 0) {
                match_options.on_frame = dev_inspector_frame;
                if (screenshot != NULL && screenshot_every != 0) {
                    dev_inspector_screenshots(screenshot, screenshot_every);
                } else if (screenshot != NULL) {
                    dev_inspector_screenshot(screenshot, screenshot_frame);
                }
                if (dump != NULL) {
                    dev_inspector_dump(dump, dump_frame);
                }
                if (capture_dir != NULL) {
                    dev_inspector_capture(capture_dir, capture_frames, capture_count);
                }
            } else {
                printf("dev-match: no window; the match runs without the Match Inspector\n");
            }
        }
#endif
        /* The match's sound in step with a recording of the original's (src/audio/sound_replay.h). */
        if (sound_lockstep_dir != NULL && sound_lockstep_start(rom.data, rom.size, sound_lockstep_dir) != 0) {
            rom_free(&rom);
            return 1;
        }
        result = dev_match_run(&rom, &match_options);
        if (sound_lockstep_dir != NULL) {
            sound_lockstep_end();
        }
#ifdef TD_INSPECTOR
        dev_inspector_stop();
#endif
        rom_free(&rom);
        return result;
    }
#endif
    (void)dev_match;
    (void)play;
    (void)play_frames;
    (void)sound_lockstep_dir;
    (void)dev_boot;
    (void)headless;
    (void)no_splash;
    (void)screenshot;
    (void)screenshot_frame;
    (void)screenshot_every;
    (void)dump;
    (void)dump_frame;
    (void)capture_dir;
    (void)capture_frames;
    (void)capture_count;
#ifdef TD_GAME_CODE
    run_game_loader(&rom, res_id);
#endif
    rom_free(&rom);
    return 0;
}
