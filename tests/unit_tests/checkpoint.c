//
// Created by Vincent Bode on 11/05/2019.
//



#include <laik.h>
#include <laik-fault-tolerance.h>
#include <stdio.h>
#include <string.h>
#include <laik-internal.h>
#include <assert.h>
#include "../fault-tolerance/fault_tolerance_test.h"
#include "../fault-tolerance/fault_tolerance_test_hash.h"



int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    Laik_Unit_Test_Data testData;

    test_init_laik(&argc, &argv, &testData);

    test_create_sample_data(&testData);
    assert(test_verify_sample_data(testData.data));

    // distribute originalData equally among all
    test_create_partitioners_and_partitionings(&testData);
    laik_switchto_partitioning(testData.data, testData.blockPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    assert(test_verify_sample_data(testData.data));

    Laik_Checkpoint* checkpoint = laik_checkpoint_create(testData.inst,
            testData.space, testData.data, NULL,
            0, 0, testData.world,LAIK_RO_None);
    assert(test_verify_sample_data(checkpoint->data));
    assert(laik_partitioning_isEqual(testData.blockPartitioning, laik_data_get_partitioning(checkpoint->data)));

    // Write garbage over the original data and then restore
    double* base;
    uint64_t count;
    laik_map_def1(testData.data, (void **) &base, &count);
    for (uint64_t i = 0; i < count; i++) base[i] = (double) i + 1;
    assert(!test_verify_sample_data(testData.data));

    // Restore useful data from checkpoint over the garbage data
    laik_checkpoint_restore(testData.inst, checkpoint, testData.space, testData.data);
    assert(test_verify_sample_data(testData.data));

    laik_checkpoint_free(checkpoint);

    laik_log(LAIK_LL_Info, "Test passed");
    laik_finalize(testData.inst);
    return 0;
}
