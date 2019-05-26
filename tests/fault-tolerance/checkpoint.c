//
// Created by Vincent Bode on 11/05/2019.
//



#include <laik.h>
#include <laik-fault-tolerance.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <laik-internal.h>
#include "fault_tolerance_test.h"



int main(int argc, char *argv[]) {
    test_init_laik(&argc, &argv);

    test_create_sample_data();

    // distribute originalData equally among all
    originalPartitioner = laik_new_block_partitioner1();
    test_apply_original_partitioner();
    uint64_t length = count * originalData->elemsize;

    unsigned char hash1[SHA_DIGEST_LENGTH];
    test_hexHash("Memory hash before checkpoint creation", base, length, hash1);

    Laik_Checkpoint checkpoint = laik_checkpoint_create(inst, space, originalData, originalPartitioner, world, LAIK_RO_None);

    backupPartitioning = laik_new_partitioning(originalPartitioner, world, checkpoint.space, NULL);
    laik_switchto_partitioning(checkpoint.data, backupPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    laik_map_def1(checkpoint.data, (void **) &backupBase, &backupCount);

    unsigned char hash2[SHA_DIGEST_LENGTH];
    test_hexHash("Memory hash of checkpoint", backupBase, length, hash2);

    if (memcmp(hash1, hash2, SHA_DIGEST_LENGTH) != 0) {
        printf("Hashes different, checkpoint failed\n");
        return -1;
    }

    // Write garbage over the original data and then restore
    for (uint64_t i = 0; i < count; i++) base[i] = (double) i + 1;

    unsigned char hash3[SHA_DIGEST_LENGTH];
    test_hexHash("Memory hash of garbage data", base, length, hash3);

    if (memcmp(hash2, hash3, SHA_DIGEST_LENGTH) == 0) {
        printf("Checkpoint hash equal to garbage hash, error.\n");
        return -1;
    }

    // Restore useful data from checkpoint over the garbage data
    laik_checkpoint_restore(inst, &checkpoint, space, originalData);

    unsigned char hash4[SHA_DIGEST_LENGTH];
    test_hexHash("Memory hash of restored data", base, length, hash4);

    if (memcmp(hash1, hash4, SHA_DIGEST_LENGTH) != 0) {
        printf("Original hash not equal to restored hash, error.\n");
        return -1;
    }

    printf("Test passed\n");
    return 0;
}
