/* Native stub — the board id the setup AP's WPA2 key is derived from (V-05).
 * A fixed value, so a host run is deterministic. */
#pragma once
#include <stdint.h>
#include <string.h>
typedef struct { uint8_t id[8]; } pico_unique_board_id_t;
static inline void pico_get_unique_board_id(pico_unique_board_id_t* out) {
    static const uint8_t fixed[8] = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04};
    memcpy(out->id, fixed, sizeof(fixed));
}
