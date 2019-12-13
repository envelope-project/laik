//
// Created by Vincent Bode on 11/05/2019.
//



#include <laik.h>
#include <laik-fault-tolerance.h>
#include <stdio.h>
#include <string.h>
#include <laik-internal.h>
#include <assert.h>
#include "../../examples/fault-tolerance/fault_tolerance_test.h"
#include "../../examples/fault-tolerance/fault_tolerance_test_hash.h"


Laik_Unit_Test_Data runTestWithData(Laik_Unit_Test_Data *testData);

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    Laik_Unit_Test_Data testData;
    test_init_laik(&argc, &argv, &testData);

    test_create_sample_data(&testData, 1);
    test_assert(true, test_verify_sample_data(testData.data), "Original test data verification");
    testData = runTestWithData(&testData);

    test_create_sample_data(&testData, 2);
    test_assert(true, test_verify_sample_data(testData.data), "Original test data verification");
    testData = runTestWithData(&testData);

    test_create_sample_data(&testData, 3);
    test_assert(true, test_verify_sample_data(testData.data), "Original test data verification");
    testData = runTestWithData(&testData);

    laik_finalize(testData.inst);
    laik_log(LAIK_LL_Info, "Test passed");
    return 0;
}

Laik_Unit_Test_Data runTestWithData(Laik_Unit_Test_Data *testData) {// distribute originalData equally among all
    test_create_partitioners_and_partitionings(testData);
    laik_switchto_partitioning(testData->data, testData->blockPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    test_assert(true, test_verify_sample_data(testData->data), "Distributed test data verification");

    Laik_Checkpoint* checkpoint = laik_checkpoint_create(testData->data, NULL,
                                                         0, 0, testData->world, LAIK_RO_None);
    test_assert(true, test_verify_sample_data(checkpoint->data), "Checkpoint test data verification");
    test_assert(true,
                laik_partitioning_isEqual(testData->blockPartitioning, laik_data_get_partitioning(checkpoint->data)),
                "Non redundant checkpoint has equal partitionings to original data");

    // Write garbage over the original data and then restore
    double* base;
    uint64_t count;
    assert(laik_my_slicecount(laik_data_get_partitioning(testData->data)) == 1);
    laik_get_map_1d(testData->data, 0, (void **) &base, &count);
    for (uint64_t i = 0; i < count; i++) base[i] = (double) i + 1;
    test_assert(false, test_verify_sample_data(testData->data), "Test data scrambled verification");

    // Restore useful data from checkpoint over the garbage data
    laik_checkpoint_restore(checkpoint, testData->data);
    test_assert(true, test_verify_sample_data(testData->data), "Restored test data verification");

    laik_checkpoint_free(checkpoint);

    int failedList[] = {2};
    Laik_Group* smallWorld = laik_new_shrinked_group(testData->world, 1, failedList);
    int nodeStatusTest[] = {LAIK_FT_NODE_OK, LAIK_FT_NODE_OK, LAIK_FT_NODE_FAULT, LAIK_FT_NODE_OK};

    //Check that missing redundancy is detected correctly
    checkpoint = laik_checkpoint_create(testData->data, NULL, 0, 0, NULL, LAIK_RO_None);
    test_assert(false, laik_checkpoint_remove_failed_slices(checkpoint, testData->world, nodeStatusTest),
                "Failed slice on non-redundant checkpoint causes data loss");
    laik_checkpoint_free(checkpoint);

    //Check that bad rotation distance is detected correctly
    // TODO: This is disabled because 1D doesn't like it
//    checkpoint = laik_checkpoint_create(testData->inst, testData->space, testData->data, NULL, 1, 4, NULL, LAIK_RO_None);
//    test_assert(false, laik_checkpoint_remove_failed_slices(checkpoint, testData->world, nodeStatusTest),
//                "Incorrect rotation distance on redundant checkpoint causes data loss");
//    laik_checkpoint_free(checkpoint);

    //Check that correct rotation distance is detected correctly
    checkpoint = laik_checkpoint_create(testData->data, NULL, 1, 1, NULL, LAIK_RO_None);

    // Check that slices are assigned into different mappings, instead of allocating a large mapping.
    test_assert(testData->data->activeMappings->count * 2, checkpoint->data->activeMappings->count,
                "Have twice as many mappings in checkpoint as in original data");

    test_assert(true, laik_checkpoint_remove_failed_slices(checkpoint, testData->world, nodeStatusTest),
                "Correct rotation distance on redundant checkpoint causes no data loss");
    laik_checkpoint_free(checkpoint);

    // Simulate a failed node and do the restore
    checkpoint = laik_checkpoint_create(testData->data, NULL, 1, 1, NULL, LAIK_RO_None);
    laik_checkpoint_remove_failed_slices(checkpoint, testData->world, nodeStatusTest);
    Laik_Partitioning* smallBlock = laik_new_partitioning(testData->blockPartitioner, smallWorld, testData->space, 0);
    laik_switchto_partitioning(testData->data, smallBlock, LAIK_DF_None, LAIK_RO_None);
    laik_checkpoint_restore(checkpoint, testData->data);
    test_assert(true, test_verify_sample_data(testData->data), "Restored data successfully");
    laik_checkpoint_free(checkpoint);

    // Check that no node is detected as failed
    int allNodesUp[] = { LAIK_FT_NODE_OK, LAIK_FT_NODE_OK, LAIK_FT_NODE_OK, LAIK_FT_NODE_OK};
    int nodeStatusCheck[4];
    int failed = laik_failure_check_nodes(testData->inst, testData->world, nodeStatusCheck);
    test_assert(0, failed, "No nodes incorrectly detected as failed");
    test_assert(0, memcmp(allNodesUp, nodeStatusCheck, sizeof(nodeStatusCheck)), "No nodes incorrectly detected as failed");

    return (*testData);
}
