/* rng.h - RNG-A (Borland rand) and RNG-B (Park-Miller), spec 05 section 1 and 06 section 3 */
#ifndef RNG_H
#define RNG_H
#include <stdint.h>

void rnga_seed(uint16_t seed);
int rnga_rand(void);              /* 0..32767 */
int rnga_scaled(int n);           /* floor(rand()*n/32768) */
int rnga_range(int lo, int hi);   /* lo + rand() % (hi-lo+1) */
uint32_t rnga_state(void);
void rnga_set_state(uint32_t s);

void rngb_seed(uint16_t v);
int rngb_next(void);              /* signed 16-bit */
int rngb_range(int lo, int hi);
#endif
