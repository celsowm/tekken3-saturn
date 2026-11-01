// Decodes the sound of a packed movie (scripts/pack_intro_video.py) with LibSaturn's own IMA block decoder
// and writes it as interleaved S16LE stereo, so scripts/check_intro_decoder.py can compare it with ffmpeg's.
// Usage: movie_audio_dump MOVIE OUT.pcm
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "saturn/audio.h"

static uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
static uint32_t be16(const uint8_t* p) { return (uint32_t(p[0]) << 8) | p[1]; }

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: movie_audio_dump MOVIE OUT.pcm\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> file;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    if (file.size() < 2048 || std::memcmp(file.data(), "SATMOV01", 8) != 0) { std::fprintf(stderr, "not a movie\n"); return 1; }
    const uint32_t frames = be32(&file[12]);
    if (be32(&file[20]) != 22050 || file[24] != 2 || file[25] != 1 || be16(&file[26]) != SAT_AUDIO_IMA_BLOCK_FRAMES) {
        std::fprintf(stderr, "no 22.05 kHz stereo IMA sound\n");
        return 1;
    }
    FILE* o = std::fopen(argv[2], "wb");
    size_t pos = 2048;
    uint8_t planar[2 * 2 * SAT_AUDIO_IMA_BLOCK_FRAMES];
    for (uint32_t n = 0; n < frames; ++n) {
        const uint32_t video = be32(&file[pos]), audio = be32(&file[pos + 4]);
        for (uint32_t at = 0; at < audio; at += SAT_AUDIO_IMA_BLOCK_BYTES(2)) {
            if (sat_audio_ima_decode_block(&file[pos + 8 + at], 2, planar) != SAT_OK) { std::fprintf(stderr, "bad block\n"); return 1; }
            for (uint32_t i = 0; i < SAT_AUDIO_IMA_BLOCK_FRAMES; ++i) {
                for (uint32_t c = 0; c < 2; ++c) {
                    const uint8_t* s = &planar[c * 2 * SAT_AUDIO_IMA_BLOCK_FRAMES + 2 * i];
                    const uint8_t le[2] = {s[1], s[0]};
                    std::fwrite(le, 1, 2, o);
                }
            }
        }
        pos += 8 + audio + video + ((4 - (video & 3)) & 3);
    }
    std::fclose(o);
    return 0;
}
