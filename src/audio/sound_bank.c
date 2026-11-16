/* The game's sound banks (src/audio/sound_bank.h).
 *
 * The table's layout, from the N64's libmus (__MusIntRemapPtrBank in the decompilation), all big-endian:
 * - +0x00: "N64 PtrTablesV2"; +0x10: a word whose top bit libmus sets once it has turned the offsets into addresses;
 * - +0x20: the number of samples; +0x24, +0x28, +0x2C: offsets (from the table's start) of three arrays, one entry a
 *   sample: the detune bytes, the fine tunes (4 bytes each, of which libmus reads the first as a signed number of
 *   hundredths) and the offsets of the samples' records;
 * - a sample's record, the N64's ALWaveTable: +0x0 the data's offset from the samples' start (an address of the 64DD
 *   drive when its top byte is 0xFF), +0x4 its size, +0x8 its type, +0xC the offset of its loop (0: none), +0x10 the
 *   offset of its codebook (ADPCM only);
 * - a loop, ALADPCMloop: start, end and count (32 bits each), then the decoder's state, 16 samples of 16 bits;
 * - a codebook, ALADPCMBook: order and number of predictors (32 bits each), then the coefficients, 16 bits each. */
#include "sound_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vadpcm.h"

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int16_t be16s(const uint8_t *p) {
    return (int16_t)(uint16_t)((unsigned)p[0] << 8 | p[1]);
}

/* Whether size bytes at offset lie inside a table of `length` bytes. */
static int inside(uint32_t offset, uint32_t size, uint32_t length) {
    return offset <= length && size <= length - offset;
}

int sound_bank_load(SoundBank *bank, const uint8_t *rom, size_t rom_size, uint32_t start, uint32_t end,
                    uint32_t waves) {
    const uint8_t *t;
    uint32_t length;
    uint32_t count, detunes, fines, records;
    uint32_t books = 0;
    uint32_t i;
    int16_t *book;

    memset(bank, 0, sizeof(*bank));
    if (end <= start || end > rom_size || memcmp(rom + start, "N64 PtrTablesV2", 16) != 0) {
        fprintf(stderr, "sound: no sound bank at ROM 0x%X\n", (unsigned)start);
        return -1;
    }
    t = rom + start;
    length = end - start;
    count = be32(t + 0x20);
    detunes = be32(t + 0x24);
    fines = be32(t + 0x28);
    records = be32(t + 0x2C);
    if (count == 0 || count > 4096 || !inside(detunes, count, length) || !inside(fines, count * 4, length) ||
        !inside(records, count * 4, length)) {
        fprintf(stderr, "sound: the bank at ROM 0x%X has a broken header\n", (unsigned)start);
        return -1;
    }
    /* First pass: check each record and count the codebooks' coefficients. */
    for (i = 0; i < count; i++) {
        uint32_t r = be32(t + records + 4 * i);
        uint32_t book_at;

        if (!inside(r, 0x14, length)) {
            goto broken;
        }
        if (t[r + 8] == SOUND_WAVE_ADPCM) {
            uint32_t order, predictors;

            book_at = be32(t + r + 0x10);
            if (!inside(book_at, 8, length)) {
                goto broken;
            }
            order = be32(t + book_at);
            predictors = be32(t + book_at + 4);
            if (order == 0 || order > 8 || predictors == 0 || predictors > 16 ||
                !inside(book_at + 8, order * predictors * 16, length)) {
                goto broken;
            }
            books += order * predictors * 8;
        }
    }
    bank->waves = calloc(count, sizeof(SoundWave));
    bank->books = calloc(books != 0 ? books : 1, sizeof(int16_t));
    if (bank->waves == NULL || bank->books == NULL) {
        sound_bank_free(bank);
        return -1;
    }
    bank->count = count;
    book = bank->books;
    for (i = 0; i < count; i++) {
        uint32_t r = be32(t + records + 4 * i);
        SoundWave *w = &bank->waves[i];
        uint32_t base = be32(t + r);
        uint32_t loop = be32(t + r + 0xC);
        int8_t detune = (int8_t)(uint8_t)(t[detunes + i] - 48);
        int8_t fine = (int8_t)t[fines + 4 * i];

        w->rom = (base & 0xFF000000u) == 0xFF000000u ? 0 : waves + base; /* 64DD samples: none in this game */
        w->length = be32(t + r + 4);
        w->type = t[r + 8];
        w->tune = (float)detune + (float)fine / 100.0f;
        if (w->rom == 0 || w->rom > rom_size || w->length > rom_size - w->rom) {
            fprintf(stderr, "sound: sample %u of the bank at ROM 0x%X lies outside the ROM\n", (unsigned)i,
                    (unsigned)start);
            sound_bank_free(bank);
            return -1;
        }
        if (loop != 0) {
            int k;

            if (!inside(loop, 12 + 32, length)) {
                goto broken;
            }
            w->has_loop = 1;
            w->loop_start = be32(t + loop);
            w->loop_end = be32(t + loop + 4);
            w->loop_count = be32(t + loop + 8);
            for (k = 0; k < 16; k++) {
                w->loop_state[k] = be16s(t + loop + 12 + 2 * k);
            }
        }
        if (w->type == SOUND_WAVE_ADPCM) {
            uint32_t book_at = be32(t + r + 0x10);
            uint32_t n;
            uint32_t k;

            w->order = (int32_t)be32(t + book_at);
            w->predictors = (int32_t)be32(t + book_at + 4);
            n = (uint32_t)(w->order * w->predictors * 8);
            for (k = 0; k < n; k++) {
                book[k] = be16s(t + book_at + 8 + 2 * k);
            }
            w->book = book;
            book += n;
        }
    }
    return 0;

broken:
    fprintf(stderr, "sound: the bank at ROM 0x%X has a broken sample record\n", (unsigned)start);
    sound_bank_free(bank);
    return -1;
}

void sound_bank_free(SoundBank *bank) {
    free(bank->waves);
    free(bank->books);
    memset(bank, 0, sizeof(*bank));
}

uint32_t sound_wave_samples(const SoundWave *wave) {
    return wave->type == SOUND_WAVE_ADPCM ? wave->length / 9 * 16 : wave->length / 2;
}

uint32_t sound_wave_decode(const SoundWave *wave, const uint8_t *rom, size_t rom_size, int16_t *out) {
    uint32_t n = sound_wave_samples(wave);
    const uint8_t *data = rom + wave->rom;
    uint32_t i;

    if (wave->rom > rom_size || wave->length > rom_size - wave->rom) {
        return 0;
    }
    if (wave->type == SOUND_WAVE_ADPCM) {
        int16_t state[16] = { 0 };

        for (i = 0; i < n / 16; i++) {
            vadpcm_decode_frame(data + 9 * i, wave->book, wave->order, wave->predictors, state, out + 16 * i);
        }
    } else {
        for (i = 0; i < n; i++) {
            out[i] = be16s(data + 2 * i);
        }
    }
    return n;
}
