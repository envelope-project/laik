//
// Created by Vincent Bode on 26/05/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_HASH_H
#define LAIK_FAULT_TOLERANCE_TEST_HASH_H

#include <stdio.h>

#define SHA_DIGEST_LENGTH 1

void SHA1(const unsigned char* base, uint64_t length, unsigned char* hash) {
    (void)base; (void)length;
    hash[0] = -1;
}

void test_hexHash(char *msg, void *baseAddress, uint64_t length, unsigned char *hash) {
    SHA1((const unsigned char *) baseAddress, length, hash);
    printf("%s ", msg);
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        printf("%02x", hash[i]);
    }
    printf(" (%lu bytes)\n", length);
}

void test_hexHash_noKeep(char *msg, void* baseAddress, uint64_t length) {
    unsigned char hash[SHA_DIGEST_LENGTH] ;
    test_hexHash(msg, baseAddress, length, hash);
}


#endif //LAIK_FAULT_TOLERANCE_TEST_HASH_H
