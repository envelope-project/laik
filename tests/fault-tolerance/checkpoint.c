//
// Created by Vincent Bode on 11/05/2019.
//



#include <laik.h>
#include <laik-fault-tolerance.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>

void hexHash(char *msg, uint64_t *base, uint64_t count, unsigned char *hash);


int main(int argc, char *argv[]) {
    laik_set_loglevel(LAIK_LL_Debug);
    Laik_Instance *inst = laik_init(&argc, &argv);
    Laik_Group *world = laik_world(inst);

    int size = 4096;

    // provides meta-information for logging
    laik_set_phase(inst, 0, "init", 0);

    uint64_t *base, *backupBase;
    uint64_t count, backupCount;

    Laik_Space *space;
    Laik_Data *originalData;
    Laik_Partitioner *blockPartitioner;
    Laik_Partitioning *masterPartitioning, *blockPartitioning, *checkpointPartitioning;

    // define global 1d double originalData with <size> entries
    space = laik_new_space_1d(inst, size);
    originalData = laik_new_data(space, laik_Double);

    // Create some sample originalData to checkpoint
    masterPartitioning = laik_new_partitioning(laik_Master, world, space, 0);
    laik_switchto_partitioning(originalData, masterPartitioning, LAIK_DF_None, LAIK_RO_None);
    if (laik_myid(world) == 0) {
        // it is ensured this is exactly one slice
        laik_map_def1(originalData, (void **) &base, &count);
        for (uint64_t i = 0; i < count; i++) base[i] = (uint64_t) i;
    }

    // distribute originalData equally among all
    blockPartitioner = laik_new_block_partitioner1();
    blockPartitioning = laik_new_partitioning(blockPartitioner, world, space, 0);
    laik_switchto_partitioning(originalData, blockPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    laik_map_def1(originalData, (void **) &base, &count);

    unsigned char hash1[SHA_DIGEST_LENGTH];
    hexHash("Memory hash before checkpoint creation", base, count * sizeof(uint64_t), hash1);


    Laik_Checkpoint checkpoint = laik_checkpoint_create(inst, space, originalData);

    checkpointPartitioning = laik_new_partitioning(blockPartitioner, world, checkpoint.space, NULL);
    laik_switchto_partitioning(checkpoint.data, checkpointPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    laik_map_def1(checkpoint.data, (void **) &backupBase, &backupCount);

    unsigned char hash2[SHA_DIGEST_LENGTH];
    hexHash("Memory hash of checkpoint", backupBase, backupCount, hash2);

    if (memcmp(hash1, hash2, SHA_DIGEST_LENGTH) != 0) {
        printf("Hashes different, checkpoint failed\n");
        return -1;
    }

    // Write garbage over the original data and then restore
    for (uint64_t i = 0; i < count; i++) base[i] = (uint64_t) i + 1;

    unsigned char hash3[SHA_DIGEST_LENGTH];
    hexHash("Memory hash of garbage data", base, count * sizeof(uint64_t), hash3);

    if (memcmp(hash2, hash3, SHA_DIGEST_LENGTH) == 0) {
        printf("Checkpoint hash equal to garbage hash, error.\n");
        return -1;
    }

    // Restore useful data from checkpoint over the garbage data
    laik_checkpoint_restore(inst, &checkpoint, space, originalData);

    unsigned char hash4[SHA_DIGEST_LENGTH];
    hexHash("Memory hash of restored data", base, count * sizeof(uint64_t), hash4);

    if (memcmp(hash1, hash4, SHA_DIGEST_LENGTH) != 0) {
        printf("Original hash not equal to restored hash, error.\n");
        return -1;
    }

    printf("Test passed\n");
    return 0;
}

void hexHash(char *msg, uint64_t *base, uint64_t count, unsigned char *hash) {
    SHA1((const unsigned char *) base, count, hash);
    printf("%s ", msg);
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        printf("%02x", hash[i]);
    }
    printf("\n");

}
