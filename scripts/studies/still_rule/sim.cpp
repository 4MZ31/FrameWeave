// Study: how much of a moving picture does the still rule overwrite? (not part of the repo)
#include "veyra/pipeline/PushHudStill.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace veyra::pipeline::hudstill;
// The header's decide() with the surround share as a parameter (4.0 had 0.5 and reach 2).
static std::vector<uint8_t> decideWith(const std::vector<uint8_t>& same, unsigned width, unsigned height, int r, float surround) {
    std::vector<uint8_t> keep(same.size(), 0);
    const unsigned bw = (width + kBlock - 1) / kBlock, bh = (height + kBlock - 1) / kBlock;
    std::vector<unsigned> stillIn(size_t(bw) * bh, 0), pixelsIn(size_t(bw) * bh, 0);
    for (unsigned by = 0; by < bh; ++by) for (unsigned bx = 0; bx < bw; ++bx) {
        unsigned still = 0, total = 0;
        for (unsigned y = by * kBlock; y < std::min(height, (by + 1) * kBlock); ++y)
            for (unsigned x = bx * kBlock; x < std::min(width, (bx + 1) * kBlock); ++x) { ++total; still += same[size_t(y) * width + x] ? 1u : 0u; }
        stillIn[size_t(by) * bw + bx] = still; pixelsIn[size_t(by) * bw + bx] = total;
    }
    std::vector<uint8_t> moves(size_t(bw) * bh, 0);
    for (int by = 0; by < int(bh); ++by) for (int bx = 0; bx < int(bw); ++bx) {
        unsigned still = 0, total = 0;
        for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
            const int qx = bx + dx, qy = by + dy;
            if (qx < 0 || qy < 0 || qx >= int(bw) || qy >= int(bh)) continue;
            still += stillIn[size_t(qy) * bw + qx]; total += pixelsIn[size_t(qy) * bw + qx];
        }
        moves[size_t(by) * bw + bx] = float(still) < surround * float(total) ? 1 : 0;
    }
    const auto at = [&](int x, int y) { return same[size_t(std::clamp(y, 0, int(height) - 1)) * width + size_t(std::clamp(x, 0, int(width) - 1))] != 0; };
    for (int y = 0; y < int(height); ++y) for (int x = 0; x < int(width); ++x) {
        if (!same[size_t(y) * width + x]) continue;
        if (!moves[size_t(unsigned(y) / kBlock) * bw + unsigned(x) / kBlock]) continue;
        unsigned beside = 0, around = 0;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            const bool s = at(x + dx, y + dy); around += s ? 1u : 0u;
            if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) beside += s ? 1u : 0u;
        }
        keep[size_t(y) * width + x] = beside >= kNear && around >= kAround ? 1 : 0;
    }
    return keep;
}
int main(int argc, char** argv) {
    // in: width height, then same mask bytes; out: keep masks for 4.0 and 4.1
    const unsigned w = unsigned(atoi(argv[2])), h = unsigned(atoi(argv[3]));
    std::vector<uint8_t> same(size_t(w) * h);
    FILE* f = fopen(argv[1], "rb"); if (!f || fread(same.data(), 1, same.size(), f) != same.size()) return 2; fclose(f);
    const auto k40 = decideWith(same, w, h, 2, 0.5f);
    const auto k41 = decide(same, w, h);                       // the header as shipped in the test build
    const auto chk = decideWith(same, w, h, int(reachFor(h)), kSurround);
    if (chk != k41) { std::puts("copy differs from header"); return 3; }
    FILE* o = fopen((std::string(argv[1]) + ".k40").c_str(), "wb"); fwrite(k40.data(), 1, k40.size(), o); fclose(o);
    o = fopen((std::string(argv[1]) + ".k41").c_str(), "wb"); fwrite(k41.data(), 1, k41.size(), o); fclose(o);
    return 0;
}
