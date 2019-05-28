//
// Created by Vincent Bode on 26/05/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_HASH_H
#define LAIK_FAULT_TOLERANCE_TEST_HASH_H

#include <openssl/sha.h>
#include <stdio.h>

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
