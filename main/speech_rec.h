#pragma once
#include <stdbool.h>

// Command IDs returned by speech_listen().
//  1..26  -> letters A..Z (spoken with the NATO alphabet: alpha..zulu)
//  then control / shortcut words:
enum {
    CMD_SEARCH = 27,   // "search" / "look up"  -> run the lookup
    CMD_DELETE = 28,   // "delete"              -> remove last letter
    CMD_CLEAR  = 29,   // "clear"               -> erase the whole word
    CMD_SPACE  = 30,   // "space"               -> add a space
    CMD_RANDOM = 31,   // "random article"      -> random page
    CMD_BACK   = 32,   // "go back"             -> previous article
    CMD_MAZE   = 33,   // "play maze"           -> tilt-a-ball maze game
    CMD_2048   = 34,   // "play puzzle"         -> 2048 game
    CMD_EXIT_GAME = 35,// "exit game"           -> back to Wikipedia
};

// Returns the lowercase letter 'a'..'z' for a letter command id (1..26),
// or 0 if the id is not a letter.
static inline char speech_id_to_letter(int id)
{
    return (id >= 1 && id <= 26) ? (char)('a' + id - 1) : 0;
}

// Load the on-device MultiNet model from flash and register the spelling
// vocabulary. Call once at boot (after mic_init). False if model missing.
bool speech_init(void);

// Listen for up to max_ms and return a command id (see enum / 1..26), or -1.
int speech_listen(int max_ms);

// Look up the command id for an exact registered phrase, e.g. "november" -> 14,
// "search" -> CMD_SEARCH. Returns -1 if not found. Used by the no-mic test mode
// so typed NATO words map to the same actions as spoken ones.
int speech_phrase_to_id(const char *phrase);
