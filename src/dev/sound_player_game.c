/* The Sound Player's calls into the game (src/dev/sound_player_game.h). It is compiled with the game's settings and
 * calls the game's functions by their decompiled names:
 * - func_8000E55C: asks for a song by number (the menus and the match call it each frame with the song they want),
 *   and func_800033C0 fades out the song the game started;
 * - func_800032A4: starts a sound effect at a volume (playEntitySound's way, in a match);
 * - func_8000DEE4: plays a menu sound by its id;
 * - func_8000335C: whether a sound still plays; func_8000DF78 stops one; MusStop(1, 4) stops every effect (what
 *   func_8000DFB0 does; the port leaves that function out, as nothing in the game reaches it);
 * - MusHandleStop and MusHandleAsk, the music library's, for the player's own song, whose handle the game no longer
 *   keeps once its screens have asked for theirs;
 * - func_80003418: the music volume. */
#include "common.h"

#include "sound_player_game.h"

void func_8000E55C(s32 arg0, s16 arg1, s8 arg2);
s32 func_8000DEE4(s32 arg0);
s32 func_8000335C(s32 handle);
void func_8000DF78(s32 arg0);
s32 func_800033F8(void);
void func_80003418(s8 arg0);
void func_800033C0(s16 arg0);
void MusStop(s32 flags, s32 speed);
s32 MusHandleStop(u32 handle, s32 speed);
s32 MusHandleAsk(u32 handle);

/* The port's hold on the songs (src/game_sdk.h). */
void game_sdk_hold_songs(int hold);
void game_sdk_player_calls(int on);
u32 game_sdk_held_song(void);
int game_sdk_scene_changed(void);

extern s16 D_80030D0C[]; /* the song table: a resource id for each song number */
extern s16 D_80036748;   /* the song last asked for */
extern s16 D_800482D8;   /* the music volume, as func_80003418 keeps it (volume << 8, | 0xFF when not 0) */
extern s16 D_80030EA2[]; /* the menus' sound ids' effect numbers (func_8000DEE4) */

int sp_song_resource(int song) {
    return song > 0 && song < SP_SONGS && D_80030D0C[song] > 0 ? D_80030D0C[song] : 0;
}

int sp_current_song(void) {
    return D_80036748;
}

int sp_song_playing(void) {
    return func_800033F8() != 0;
}

/* func_8000E55C's third argument switches the song on (any value but 0; the game passes 0x50). */
void sp_take_songs(int fade) {
    game_sdk_player_calls(1);
    if (game_sdk_held_song() != 0) {
        MusHandleStop(game_sdk_held_song(), fade);
    }
    game_sdk_hold_songs(1);
    func_800033C0((s16)fade);
    func_8000E55C(0, (s16)fade, 0x50);
    game_sdk_player_calls(0);
}

int sp_request_song(int song, int fade) {
    game_sdk_player_calls(1);
    func_8000E55C(song, (s16)fade, 0x50);
    game_sdk_player_calls(0);
    return D_80036748 == song && game_sdk_held_song() != 0;
}

int sp_scene_changed(void) {
    return game_sdk_scene_changed();
}

int sp_held_song_playing(void) {
    return game_sdk_held_song() != 0 && MusHandleAsk(game_sdk_held_song()) != 0;
}

void sp_give_songs(int fade) {
    game_sdk_player_calls(1);
    if (game_sdk_held_song() != 0) {
        MusHandleStop(game_sdk_held_song(), fade);
    }
    game_sdk_player_calls(0);
    game_sdk_hold_songs(0);
}

uint32_t sp_play_effect(int number, int volume) {
    if (number < 0 || number >= SP_EFFECTS) {
        return 0;
    }
    return (uint32_t)func_800032A4((s16)number, (s16)volume, 128, 0, -1);
}

uint32_t sp_play_menu_sound(int id) {
    return (uint32_t)func_8000DEE4(id);
}

int sp_menu_sound_effect(int id) {
    if (id < SP_MENU_FIRST || id >= SP_MENU_FIRST + SP_MENU_COUNT) {
        return -1;
    }
    return D_80030EA2[id - SP_MENU_FIRST];
}

int sp_effect_playing(uint32_t handle) {
    return handle != 0 && func_8000335C((s32)handle) != 0;
}

void sp_stop_effect(uint32_t handle) {
    func_8000DF78((s32)handle);
}

void sp_stop_effects(void) {
    MusStop(1, 4); /* class 1: the effects */
}

int sp_music_volume(void) {
    return (u16)D_800482D8 >> 8;
}

void sp_set_music_volume(int volume) {
    func_80003418((s8)(volume < 0 ? 0 : volume > 127 ? 127 : volume));
}
