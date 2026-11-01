// Decodes a packed movie (scripts/pack_intro_video.py) with LibSaturn's own decoder on the host and
// writes chosen frames as raw RGB24, so scripts/check_intro_decoder.py can compare them with ffmpeg.
// Usage: movie_dump MOVIE OUTDIR FRAME...   (every frame is decoded in order; the listed ones are written)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "saturn/movie.h"

static uint32_t be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
static uint32_t be16(const uint8_t* p) { return (uint32_t(p[0]) << 8) | p[1]; }

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: movie_dump MOVIE OUTDIR FRAME...\n"); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::perror(argv[1]); return 1; }
    std::vector<uint8_t> file;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) file.insert(file.end(), buf, buf + n);
    std::fclose(f);
    if (file.size() < 2048 || std::memcmp(file.data(), "SATMOV01", 8) != 0) { std::fprintf(stderr, "not a movie\n"); return 1; }
    const uint32_t w = be16(&file[8]), h = be16(&file[10]), frames = be32(&file[12]);
    std::set<uint32_t> want;
    for (int i = 3; i < argc; ++i) want.insert(uint32_t(std::atoi(argv[i])));

    static sat_movie_decoder_t decoder;
    if (sat_movie_decoder_init(&decoder, uint16_t(w), uint16_t(h)) != SAT_OK) return 1;
    const uint32_t stride = 512;
    std::vector<uint16_t> canvas(stride * h + 2, 0);
    uint16_t* pixels = canvas.data() + ((reinterpret_cast<uintptr_t>(canvas.data()) & 3u) ? 1 : 0);
    size_t pos = 2048;
    for (uint32_t n = 0; n < frames; ++n) {
        const uint32_t video = be32(&file[pos]), audio = be32(&file[pos + 4]);
        const uint8_t* frame = &file[pos + 8 + audio];
        const sat_result_t rc = sat_movie_decode_frame(&decoder, frame, video, pixels, stride);
        if (rc != SAT_OK) { std::fprintf(stderr, "frame %u: %d\n", n, int(rc)); return 1; }
        if (want.count(n)) {
            char name[512];
            std::snprintf(name, sizeof name, "%s/frame-%05u.rgb", argv[2], n);
            FILE* o = std::fopen(name, "wb");
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const uint16_t d = pixels[y * stride + x];
                    const uint8_t rgb[3] = {uint8_t((d & 31) << 3), uint8_t(((d >> 5) & 31) << 3), uint8_t(((d >> 10) & 31) << 3)};
                    std::fwrite(rgb, 1, 3, o);
                }
            }
            std::fclose(o);
        }
        pos += 8 + audio + video + ((4 - (video & 3)) & 3);
    }
    std::printf("decoded %u frames\n", frames);
    return 0;
}
