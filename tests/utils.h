#ifndef ZIDX_TESTS_UTILS_H
#define ZIDX_TESTS_UTILS_H

#include<stdint.h>

void        initialize_srandom(uint64_t seed);
uint32_t    get_random_int();
uint8_t     get_random_byte();
const char* get_random_compressed_file(uint64_t seed, long length);

#endif