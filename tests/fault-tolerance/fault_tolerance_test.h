//
// Created by Vincent Bode on 19/05/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_H
#define LAIK_FAULT_TOLERANCE_TEST_H

#include <openssl/sha.h>

#define TEST_SIZE 4096

Laik_Instance *inst;
Laik_Group *world;

double *base, *backupBase;
uint64_t count, backupCount;

Laik_Space *space;
Laik_Data *originalData;

Laik_Partitioner *originalPartitioner, *backupPartitioner;
Laik_Partitioning *originalPartitioning, *backupPartitioning;


void test_apply_original_partitioner() {
    originalPartitioning = laik_new_partitioning(originalPartitioner, world, space, 0);
    originalPartitioning->name = "original-block-partitioning";
    laik_switchto_partitioning(originalData, originalPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    laik_map_def1(originalData, (void **) &base, &count);
}

void test_init_laik(int *argc, char ***argv) {
    laik_set_loglevel(LAIK_LL_Debug);
    inst = laik_init(argc, argv);
    world = laik_world(inst);

    // provides meta-information for logging
    laik_set_phase(inst, 0, "init", 0);
}

void test_create_sample_data() {
    // define global 1d double originalData with <size> entries
    space = laik_new_space_1d(inst, TEST_SIZE);
    originalData = laik_new_data(space, laik_Double);

    // Create some sample originalData to checkpoint
    Laik_Partitioning *masterPartitioning = laik_new_partitioning(laik_Master, world, space, 0);
    laik_switchto_partitioning(originalData, masterPartitioning, LAIK_DF_None, LAIK_RO_None);
    if (laik_myid(world) == 0) {
        // it is ensured this is exactly one slice
        laik_map_def1(originalData, (void**) &base, &count);
        for (uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }
}

void test_hexHash(char *msg, void *baseAddress, uint64_t length, unsigned char *hash) {
    SHA1((const unsigned char *) baseAddress, length, hash);
    printf("%i: %s ", laik_myid(world), msg);
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        printf("%02x", hash[i]);
    }
    printf(" (%lu bytes)\n", length);
}

void test_hexHash_noKeep(char *msg, void* baseAddress, uint64_t length) {
    unsigned char hash[SHA_DIGEST_LENGTH] ;
    test_hexHash(msg, baseAddress, length, hash);
}


#endif //LAIK_FAULT_TOLERANCE_TEST_H
