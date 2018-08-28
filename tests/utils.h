#ifndef ZIDX_TESTS_UTILS_H
#define ZIDX_TESTS_UTILS_H

#include<stdio.h>
#include<stdint.h>

void     initialize_srandom(uint64_t seed);
void     initialize_srandom2(uint64_t seed, uint64_t skip);
uint32_t get_random_int();
uint8_t  get_random_byte();
FILE*    get_random_compressed_file(uint64_t seed, long length,
                                    uint8_t *data);

#endif
