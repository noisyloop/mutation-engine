#pragma once
#include <cstdint>

namespace me
{
// Tiny deterministic PRNG (xorshift32). Same seed -> same sequence on every
// platform, which is what makes mutations and glitch events reproducible.
struct Rng
{
    uint32_t s = 1;
    void seed (uint32_t v) noexcept { s = v ? v : 0x9E3779B9u; }
    uint32_t nextU32() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() noexcept { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); }       // [0,1)
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }                               // [-1,1)
    int   range (int n) noexcept { return n <= 1 ? 0 : (int) (nextU32() % (uint32_t) n); }     // [0,n)
};
}
