//
// Created by Vincent Bode on 19/05/2019.
//

#include <laik.h>
#include <laik-fault-tolerance.h>
#include <stdio.h>
#include <string.h>
#include <laik-internal.h>
#include "fault_tolerance_test.h"

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
//    test_init_laik(&argc, &argv);
////    laik_set_loglevel(LAIK_LL_Info);
//
//    if(world->size <= 1) {
//        printf("World size %i too small. Please use at least size 2 for this test.\n", world->size);
//        return -1;
//    }
//
//    printf("My id: %i\n", laik_myid(world));
//
//    test_create_sample_data();
//    uint64_t length = count * originalData->elemsize;
//
//    unsigned char hashSample[HASH_DIGEST_LENGTH];
//    test_hexHash("Sample data", base, length, hashSample);
//
//    // distribute originalData equally among all
//    originalPartitioner = laik_new_block_partitioner1();
//    test_apply_original_partitioner();
//
//    length = count * originalData->elemsize;
//    unsigned char hashOriginal[HASH_DIGEST_LENGTH];
//    test_hexHash("Original data", base, length, hashOriginal);
//
//    // This should automatically create a backup partitioner that stores data on another node
//    Laik_Checkpoint* checkpoint = laik_checkpoint_create(inst, space, originalData, NULL, 1, 1, NULL, LAIK_RO_None);
//
//    laik_map_def1(checkpoint->data, (void**) &backupBase, &backupCount);
//    unsigned char hashBackup[HASH_DIGEST_LENGTH];
//    test_hexHash("Checkpoint data using automatic backup partitioner", backupBase, length, hashBackup);
//
//    if(memcmp(hashOriginal, hashBackup, HASH_DIGEST_LENGTH) == 0) {
//        printf("The original and automatic backup partitioning should not store the same data on one node\n");
//        return -1;
//    }
//
//    backupPartitioner = checkpoint->data->activePartitioning->partitioner;
//
//    //Switch back to the original setup (i.e bring the data back onto the node)
//    printf("Reverting from partitioner %s to partitioner %s\n", checkpoint->data->activePartitioning->partitioner->name, originalPartitioning->partitioner->name);
//    laik_switchto_partitioning(checkpoint->data, originalPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
//    laik_map_def1(checkpoint->data, (void**) &backupBase, &backupCount);
//
//    unsigned char hashBackupOriginalPartitioning[HASH_DIGEST_LENGTH];
//    test_hexHash("Checkpoint data using restored partitioning", backupBase, length, hashBackupOriginalPartitioning);
//    if(memcmp(hashOriginal, hashBackupOriginalPartitioning, HASH_DIGEST_LENGTH) != 0) {
//        printf("The restored partitioning should be identical to the original\n");
//        return -1;
//    }
//
//    printf("Test passed\n");
//    return 0;
}
