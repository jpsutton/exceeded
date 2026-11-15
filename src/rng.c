#include "rng.h"

static uint32_t sa = 1;
static uint32_t pb = 1;

void rnga_seed(uint16_t seed) { sa = seed; }
int rnga_rand(void) {
    sa = sa * 0x015A4E35u + 1u;
    return (int)((sa >> 16) & 0x7FFF);
}
int rnga_scaled(int n) { return (int)(((int64_t)rnga_rand() * n) / 32768); }
int rnga_range(int lo, int hi) { return lo + rnga_rand() % (hi - lo + 1); }
uint32_t rnga_state(void) { return sa; }
void rnga_set_state(uint32_t s) { sa = s; }

void rngb_seed(uint16_t v) { pb = v; }
int rngb_next(void) {
    pb = (uint32_t)(((uint64_t)16807 * pb) % 2147483647u);
    int r = (int16_t)(pb & 0xFFFF);
    if (r == -32768) r = 0;
    return r;
}
int rngb_range(int lo, int hi) {
    if (hi < lo) { int t = lo; lo = hi; hi = t; }
    int u = rngb_next() + 32768;
    return lo + (int)((((int64_t)(hi - lo)) * u + 0x7FFF) >> 16);
}
