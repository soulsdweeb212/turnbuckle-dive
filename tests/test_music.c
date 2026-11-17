/* The music player (src/audio/music.c), with the game's banks and effect table from the ROM. It checks:
 * - the pitch series against 2 to the power x, and the random numbers against the N64's first seven (range 100);
 * - a small song made here: its one line starts on the frame after it is started (songs start together through the
 *   queue), sounds, plays its note for as long as the tempo says, and lets its channel go at its stop command;
 * - a song stopped with a fade of 10 frames is let go 11 frames later, and its handle then answers 0;
 * - the game's effect 1, which starts effect 151 as its second half: both channels answer to the first's handle, and
 *   both end by themselves;
 * - effects take free channels, and a busy player takes the lowest-priority effect's channel for a higher one.
 * Without the ROM it reports the test as skipped (77). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio/sound.h"
#include "rom.h"

static int g_failed;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL: " __VA_ARGS__);                                                                              \
            printf("\n");                                                                                              \
            g_failed = 1;                                                                                              \
        }                                                                                                              \
    } while (0)

/* One frame of the player and its sound (480 samples at 28800 Hz). */
static void frame(void) {
    static int16_t out[480 * 2];

    synth_render(sound_synth(), out, 480);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* A song of one line: an envelope (speed 1, start 127, attack 1 to 127, decay 1 to 127, release 1), wave 0, tempo
 * 120, one note of 96 ticks (a second at 96 ticks a second), stop. */
static size_t make_song(uint8_t *song) {
    static const uint8_t line[] = { 0x84, 1, 127, 1, 127, 1, 127, 1, 0x81, 0x00, 0x85, 120, 48, 96, 0x80 };

    memset(song, 0, 0x100);
    put32(song + 0x00, 1);    /* one line */
    put32(song + 0x04, 0x18); /* its stream's offset is at 0x18 */
    put32(song + 0x08, 0x1C); /* no volume stream */
    put32(song + 0x0C, 0x20); /* no bend stream */
    put32(song + 0x10, 0x24); /* no envelope table, no drums */
    put32(song + 0x14, 0x24);
    put32(song + 0x18, 0x30);
    memcpy(song + 0x30, line, sizeof(line));
    return 0x30 + sizeof(line);
}

static void check_maths(Music *music) {
    static const int expected[7] = { 20, 33, 46, 13, 27, 40, -49 };
    float worst = 0.0f;
    int i;

    for (i = -120; i <= 120; i++) {
        float x = i / 120.0f;
        float e = fabsf(music_power_of_2(x) / exp2f(x) - 1.0f);

        worst = e > worst ? e : worst;
    }
    CHECK(worst < 2e-5f, "2^x within an octave is off by %g", worst);
    /* The first seven numbers of a new player, read through the random-volume command of a channel made here. */
    for (i = 0; i < 7; i++) {
        static const uint8_t command[] = { 0xA4, 100, 0, 0x60, 1, 0x80 };
        MusicChannel *c = &music->channel[0];

        memset(c, 0, sizeof(*c));
        c->data = command;
        c->end = command + sizeof(command);
        c->bank = music->songs_bank;
        c->tempo = 409;
        c->stop_countdown = -1;
        c->length = 1;
        music_frame(music);
        CHECK((int8_t)c->volume == expected[i], "random number %d is %d, not %d", i + 1, (int8_t)c->volume,
              expected[i]);
        c->data = NULL;
    }
}

static void check_song(Music *music) {
    static uint8_t song[0x100];
    size_t size = make_song(song);
    uint32_t handle = music_start_song(music, song, size);
    int frames = 0;

    CHECK(handle != 0, "the song got no handle");
    CHECK(music_ask(music, MUSIC_SONGS_CLASS) == 1, "the song has %d lines playing, not 1",
          music_ask(music, MUSIC_SONGS_CLASS));
    CHECK(music->channel[0].paused, "the song's line did not wait for the next frame");
    frame();
    CHECK(music->channel[0].sounding, "the song's note did not start");
    CHECK(music->channel[0].last_volume > 20000, "the note's volume is %u", music->channel[0].last_volume);
    /* Tempo 120 gains 409 a frame, 256 a tick: 96 ticks are 60.1 frames. */
    while (music_handle_ask(music, handle) != 0 && frames < 200) {
        frame();
        frames++;
    }
    CHECK(frames >= 59 && frames <= 62, "the one-second note lasted %d frames", frames);
    CHECK(!music->channel[0].sounding, "the voice still sounds after the song's stop");

    handle = music_start_song(music, song, size);
    frame();
    frame();
    CHECK(music_handle_stop(music, handle, 10) == 1, "the stop did not take the song's line");
    for (frames = 0; music_handle_ask(music, handle) != 0 && frames < 100; frames++) {
        frame();
    }
    CHECK(frames == 11, "a 10-frame fade let the line go after %d frames", frames);
}

static void check_effects(Music *music) {
    uint32_t handle = music_start_effect(music, 1, 128, 128, 0, -1);
    int frames;
    int i, busy = 0;

    CHECK(handle != 0, "effect 1 got no handle");
    frame();
    frame();
    frame();
    CHECK(music_handle_ask(music, handle) == 2, "effect 1 and its second half: %d channels, not 2",
          music_handle_ask(music, handle));
    for (frames = 0; music_handle_ask(music, handle) != 0 && frames < 1000; frames++) {
        frame();
    }
    CHECK(frames < 1000, "effect 1 did not end");

    /* Fill every channel with effects of priority 40, then ask for one of 100. */
    for (i = 0; i < MUSIC_CHANNELS; i++) {
        busy += music_start_effect(music, 2, 128, 128, 0, 40) != 0;
    }
    CHECK(busy == MUSIC_CHANNELS, "only %d effects found a free channel", busy);
    CHECK(music_start_effect(music, 2, 128, 128, 0, 40) == 0, "an effect took a channel of equal priority");
    CHECK(music_start_effect(music, 2, 128, 128, 0, 100) != 0, "a higher-priority effect found no channel");
    /* A stop without a fade lets the channels go at the next frame, in channel order; effects that start their second
     * halves in that frame would take the channels let go before them, so they are given their first frame first. */
    frame();
    music_stop(music, MUSIC_EFFECTS_CLASS, 0);
    frame();
    CHECK(music_ask(music, MUSIC_EFFECTS_CLASS | MUSIC_SONGS_CLASS) == 0, "%d channels still play after stopping",
          music_ask(music, MUSIC_EFFECTS_CLASS | MUSIC_SONGS_CLASS));
}

int main(void) {
    Rom rom;
    RomStatus status = rom_load(rom_find(NULL), &rom);

    if (status != ROM_OK) {
        printf(status == ROM_NOT_FOUND ? "no ROM: skipped\n" : "the ROM is not the supported one: skipped\n");
        return 77;
    }
    if (sound_start(rom.data, rom.size) != 0) {
        printf("FAIL: sound_start\n");
        return 1;
    }
    check_maths(sound_music());
    sound_end();
    sound_start(rom.data, rom.size);
    check_song(sound_music());
    check_effects(sound_music());
    sound_end();
    rom_free(&rom);
    if (!g_failed) {
        printf("all music player checks passed\n");
    }
    return g_failed;
}
