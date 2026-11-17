/* The Sound Player's calls into the game (src/dev/sound_player_game.c): it plays the game's songs and sound effects
 * with the game's own functions, the ones its menus and matches call, so they go through the game's sound code and
 * then the port's music player as the game's own do. C and C++. Call them only on the game's thread while the game
 * runs (the window draws inside the game's hand-off). */
#ifndef TD_SOUND_PLAYER_GAME_H
#define TD_SOUND_PLAYER_GAME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's song numbers: 0 (none) to 21, each a resource in the game's song table. */
#define SP_SONGS 22
/* The game's sound-effect numbers (the music player's effect table). */
#define SP_EFFECTS 210
/* The menus' sound ids, which the game turns into effect numbers through a table of its own. */
#define SP_MENU_FIRST 0x401
#define SP_MENU_COUNT 0x2B

/* The resource that holds song `song` (0: none). */
int sp_song_resource(int song);
/* The song the game last started or asked for (0: none), and whether a song is playing. */
int sp_current_song(void);
int sp_song_playing(void);

/* The player's songs. sp_take_songs takes the songs from the game (game_sdk_hold_songs in src/game_sdk.h: the game's
 * own song calls wait) and fades out the song playing over `fade` frames, the player's or the game's (the game's with
 * its own func_800033C0 and func_8000E55C). sp_request_song then asks for song `song` with the game's func_8000E55C, as
 * the game's screens ask for theirs: call it each frame until it returns 1, once the song has started (that is, once
 * the song before it has faded out). sp_held_song_playing says whether the player's song still plays; sp_give_songs
 * fades it out and gives the songs back to the game, whose screens then start their own again. */
void sp_take_songs(int fade);
int sp_request_song(int song, int fade);
int sp_held_song_playing(void);
void sp_give_songs(int fade);

/* Starts effect `number` at `volume` (128: as written) as the game's matches do; returns its handle (0: not played). */
uint32_t sp_play_effect(int number, int volume);
/* Plays a menu sound (SP_MENU_FIRST onwards) as the menus do; returns its handle. */
uint32_t sp_play_menu_sound(int id);
/* The effect number a menu sound plays (negative: none). */
int sp_menu_sound_effect(int id);
/* Whether the sound of `handle` still plays; stop it (fading over 2 frames, as the game does); stop every effect. */
int sp_effect_playing(uint32_t handle);
void sp_stop_effect(uint32_t handle);
void sp_stop_effects(void);

/* The game's music volume, 0 to 127 (the game sets 127 at boot); setting it is the game's own setting. */
int sp_music_volume(void);
void sp_set_music_volume(int volume);

#ifdef __cplusplus
}
#endif

#endif
