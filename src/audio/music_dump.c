/* --music-dump (src/audio/music_dump.h). The songs are the game's resources that its song table names (D_80030D0C in
 * the decompilation, one 16-bit resource id for each song number; func_8000E55C plays them), loaded with the game's
 * own loader as the game loads them, and started as the game starts them. They are played from a fresh start of the
 * sound each, so each begins on the same voices and random numbers as when it is the first song played. */
#include "music_dump.h"

#include <stdio.h>
#include <stdlib.h>

#include "game.h"
#include "resources.h"
#include "sound.h"

#define SONG_TABLE (0x80030D0Cu - 0x7FFFF400u) /* in the ROM */
#define SONGS 22

static void put16(FILE *f, unsigned v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}

static void put32(FILE *f, uint32_t v) {
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

static int write_wav(const char *path, const int16_t *frames, uint32_t count) {
    FILE *f = fopen(path, "wb");
    uint32_t i;
    int ok;

    if (f == NULL) {
        return -1;
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + count * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);                /* PCM */
    put16(f, 2);                /* stereo */
    put32(f, SYNTH_RATE);
    put32(f, SYNTH_RATE * 4);   /* bytes a second */
    put16(f, 4);                /* bytes a frame */
    put16(f, 16);               /* bits a sample */
    fwrite("data", 1, 4, f);
    put32(f, count * 4);
    for (i = 0; i < count * 2; i++) {
        put16(f, (uint16_t)frames[i]);
    }
    ok = ferror(f) == 0;
    return fclose(f) == 0 && ok ? 0 : -1;
}

int music_dump(const Rom *rom, const char *dir, int seconds) {
    uint32_t limit = (uint32_t)seconds * SYNTH_RATE;
    int16_t *frames = malloc((size_t)limit * 4);
    char path[1024];
    FILE *list;
    int song, result = 0;

    snprintf(path, sizeof(path), "%s/songs.txt", dir);
    list = fopen(path, "w");
    if (frames == NULL || list == NULL) {
        fprintf(stderr, "music-dump: cannot write to %s\n", dir);
        free(frames);
        if (list != NULL) {
            fclose(list);
        }
        return 1;
    }
    rom_attach(rom);
    res_init();
    for (song = 1; song < SONGS; song++) {
        uint32_t id = (uint32_t)(rom->data[SONG_TABLE + 2 * song] << 8 | rom->data[SONG_TABLE + 2 * song + 1]);
        GameResDesc desc;
        uint8_t *data;
        uint32_t n = 0;

        if (id == 0 || id > GAME_RES_COUNT || sound_start(rom->data, rom->size) != 0) {
            continue;
        }
        data = res_load(id, &desc);
        music_start_song(sound_music(), data, (size_t)desc.size);
        /* Until the time is up or the song has ended (a song that ends lets its channels go). */
        while (n < limit) {
            uint32_t step = limit - n < SYNTH_RATE ? limit - n : SYNTH_RATE;

            synth_render(sound_synth(), frames + 2 * n, (int)step);
            n += step;
            if (music_ask(sound_music(), MUSIC_SONGS_CLASS) == 0) {
                break;
            }
        }
        sound_end();
        free(data);
        snprintf(path, sizeof(path), "%s/song_%02d.wav", dir, song);
        if (write_wav(path, frames, n) != 0) {
            fprintf(stderr, "music-dump: cannot write %s\n", path);
            result = 1;
        }
        fprintf(list, "song_%02d.wav resource 0x%03X %.1f s%s\n", song, (unsigned)id, n / (double)SYNTH_RATE,
                n < limit ? " (ended)" : "");
    }
    res_shutdown();
    fclose(list);
    free(frames);
    printf("music-dump: songs written to %s\n", dir);
    return result;
}
