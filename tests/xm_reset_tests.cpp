#include "rev_xm.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    rev::xm::Player* fresh = rev::xm::CreatePlayer(bytes.data(), bytes.size());
    rev::xm::Player* restarted = rev::xm::CreatePlayer(bytes.data(), bytes.size());
    if (!fresh || !restarted) return 3;
    float scratch[2048 * 2] = {};
    for (int i = 0; i < 24; ++i) rev::xm::Update(restarted, scratch, 2048);
    rev::xm::SetPosition(restarted, 0, 0);
    float expected[2048 * 2] = {}, actual[2048 * 2] = {};
    bool equal = true;
    bool audible = false;
    for (int block = 0; block < 24 && equal; ++block) {
        rev::xm::Update(fresh, expected, 2048);
        rev::xm::Update(restarted, actual, 2048);
        for (int i = 0; i < 2048 * 2; ++i) {
            if (fabsf(actual[i]) > 0.0001f) audible = true;
            if (fabsf(expected[i] - actual[i]) > 0.000001f) { equal = false; break; }
        }
    }
    rev::xm::DestroyPlayer(fresh);
    rev::xm::DestroyPlayer(restarted);
    if (!equal || !audible) { std::fprintf(stderr, "XM reset did not reproduce audible fresh playback\n"); return 1; }
    std::printf("[xm_reset_tests] PASS\n");
    return 0;
}
