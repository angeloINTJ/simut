/**
 * @file    tools/h5_parity/h5_parity.cpp
 * @brief   Parity harness: runs the firmware V5 codec over a corpus the
 *          Python reference generated, so the two can be diffed byte for byte.
 *
 * Not a test by itself — `tools/check_history_v5_parity.py` builds it, feeds
 * it, and does the comparing. Kept out of the PlatformIO test tree because it
 * is a filter (corpus in, chunks out), not a Unity suite.
 *
 * Corpus (little-endian):
 *   u32 nCases
 *   per case: u8 nCh, u16 nominalIntervalS, u8 count,
 *             nCh x { u8 id, u8 kind, i8 scaleExp, u8 flags },
 *             count x { u32 epoch, nCh x i16 }
 *
 * Output (little-endian):
 *   u32 nCases
 *   per case: u32 chunkLen, chunkLen bytes,
 *             u8 decodedCount, decodedCount x { u32 epoch, nCh x i16 }
 *
 * @license MIT License
 */

#include "HistoryV5.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace simut_native { uint32_t fake_millis_value = 0; }

static std::vector<uint8_t> g_in;
static size_t g_pos = 0;
static std::vector<uint8_t> g_out;

template <typename T> static T rd( ) {
    T v{};
    if (g_pos + sizeof(T) > g_in.size( )) { fprintf(stderr, "corpus truncated\n"); exit(2); }
    memcpy(&v, g_in.data( ) + g_pos, sizeof(T));
    g_pos += sizeof(T);
    return v;
}

template <typename T> static void wr(T v) {
    const uint8_t* p = (const uint8_t*)&v;
    g_out.insert(g_out.end( ), p, p + sizeof(T));
}

int main(int argc, char** argv) {
    if (argc != 3) { fprintf(stderr, "usage: h5_parity <corpus> <out>\n"); return 2; }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("corpus"); return 2; }
    fseek(f, 0, SEEK_END);
    g_in.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(g_in.data( ), 1, g_in.size( ), f) != g_in.size( )) { perror("read"); return 2; }
    fclose(f);

    const uint32_t nCases = rd<uint32_t>( );
    wr<uint32_t>(nCases);

    H5ChannelDesc schema[H5_MAX_CHANNELS];
    int16_t row[H5_MAX_CHANNELS];
    uint8_t chunk[H5_BLOCK_MAX_BYTES];

    for (uint32_t c = 0; c < nCases; c++) {
        const uint8_t  nCh     = rd<uint8_t>( );
        const uint16_t nominal = rd<uint16_t>( );
        const uint8_t  count   = rd<uint8_t>( );
        for (uint8_t i = 0; i < nCh; i++) {
            schema[i].id       = rd<uint8_t>( );
            schema[i].kind     = rd<uint8_t>( );
            schema[i].scaleExp = (int8_t)rd<uint8_t>( );
            schema[i].flags    = rd<uint8_t>( );
        }

        HistoryV5Encoder enc;
        enc.begin(schema, nCh, nominal);
        for (uint8_t r = 0; r < count; r++) {
            const uint32_t epoch = rd<uint32_t>( );
            for (uint8_t k = 0; k < nCh; k++) row[k] = (int16_t)rd<uint16_t>( );
            /* The reference stops at the first refusal too, so the corpus
             * never carries a record past that point. */
            if (r == 0) enc.reset(epoch, row);
            else if (!enc.add(epoch, row)) { fprintf(stderr, "case %u: add refused at %u\n", c, r); return 3; }
        }

        const size_t len = enc.seal(chunk, sizeof(chunk), 0);
        wr<uint32_t>((uint32_t)len);
        g_out.insert(g_out.end( ), chunk, chunk + len);

        /* Decode what we just produced, so the reference can check both
         * directions from one run. */
        HistoryV5Decoder dec;
        uint8_t produced = 0;
        std::vector<uint8_t> recs;
        if (dec.begin(chunk, len, schema, nCh, nominal)) {
            uint32_t epoch;
            while (dec.next(epoch, row)) {
                const uint8_t* p = (const uint8_t*)&epoch;
                recs.insert(recs.end( ), p, p + 4);
                for (uint8_t k = 0; k < nCh; k++) {
                    const uint8_t* q = (const uint8_t*)&row[k];
                    recs.insert(recs.end( ), q, q + 2);
                }
                produced++;
            }
        }
        wr<uint8_t>(produced);
        g_out.insert(g_out.end( ), recs.begin( ), recs.end( ));
    }

    FILE* o = fopen(argv[2], "wb");
    if (!o) { perror("out"); return 2; }
    fwrite(g_out.data( ), 1, g_out.size( ), o);
    fclose(o);
    return 0;
}
