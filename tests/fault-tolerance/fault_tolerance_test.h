//
// Created by Vincent Bode on 19/05/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_H
#define LAIK_FAULT_TOLERANCE_TEST_H

#include "fault_tolerance_test_hash.h"
#include <assert.h>

#define TEST_SIZE 4096

struct _Laik_Unit_Test_Data {
    Laik_Instance* inst;
    Laik_Group* world;

    Laik_Space* space;
    Laik_Data* data;

    Laik_Partitioner* blockPartitioner;
    Laik_Partitioning* blockPartitioning;

    Laik_Partitioner* masterPartitioner;
    Laik_Partitioning* masterPartitioning;
};

typedef struct _Laik_Unit_Test_Data Laik_Unit_Test_Data;

////Laik_Instance *inst;
////Laik_Group *world;
////
////double *base, *backupBase;
////uint64_t count, backupCount;
////
////Laik_Space *space;
////Laik_Data *originalData;
////
////Laik_Partitioner *originalPartitioner, *backupPartitioner;
////Laik_Partitioning *originalPartitioning, *backupPartitioning;
//
//
//void test_apply_original_partitioner() {
//    originalPartitioning = laik_new_partitioning(originalPartitioner, world, space, 0);
//    originalPartitioning->name = "original-block-partitioning";
//    laik_switchto_partitioning(originalData, originalPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
//    laik_map_def1(originalData, (void **) &base, &count);
//}
//
void test_init_laik(int *argc, char ***argv, Laik_Unit_Test_Data *testData) {
    laik_set_loglevel(LAIK_LL_Info);
    testData->inst = laik_init(argc, argv);
    testData->world = laik_world(testData->inst);
    laik_log(LAIK_LL_Info, "Setting up test environment");

    // For testing purposes: world size 4
    assert(laik_size(testData->world) == 4);

    // provides meta-information for logging
    laik_set_phase(testData->inst, 0, "init", 0);
}
//
void test_create_sample_data(Laik_Unit_Test_Data* testData) {
    // define global 1d double originalData with <size> entries
    testData->space = laik_new_space_1d(testData->inst, TEST_SIZE);
    testData->data = laik_new_data(testData->space, laik_Double);

    // Create some sample originalData to checkpoint
    Laik_Partitioning *masterPartitioning = laik_new_partitioning(laik_Master, testData->world, testData->space, 0);
    laik_switchto_partitioning(testData->data, masterPartitioning, LAIK_DF_None, LAIK_RO_None);

    double *base;
    uint64_t count;
    if (laik_myid(testData->world) == 0) {
        // it is ensured this is exactly one slice
        laik_map_def1(testData->data, (void**) &base, &count);
        for (uint64_t i = 0; i < count; i++) base[i] = (double) i;
    }
}

bool test_verify_sample_data(Laik_Data* data) {
    double* base;
    uint64_t count;
    Laik_Partitioning *partitioning = laik_data_get_partitioning(data);
    for (int sliceIndex = 0; sliceIndex < laik_my_slicecount(partitioning); ++sliceIndex) {
        laik_map_def(data, sliceIndex, (void **) &base, &count);
        Laik_TaskSlice* taskSlice = laik_my_slice(partitioning, sliceIndex);
        const Laik_Slice* slice = laik_taskslice_get_slice(taskSlice);
        int64_t sZ = slice->from.i[2];
        int64_t sY = slice->from.i[1];
        int64_t sX = slice->from.i[0];
        if(slice->space->dims < 3) { sZ = 0; }
        if(slice->space->dims < 2) { sY = 0; }
//        laik_log(LAIK_LL_Warning, "Checking slice %i: %zu, %zu, %zu", sliceIndex, sZ, sY, sX);

        int startValue = sZ * TEST_SIZE * TEST_SIZE + sY * TEST_SIZE + sX;
        for (uint64_t offset = 0; offset < count; ++offset) {
            if(base[offset] != startValue) {
//                laik_log(LAIK_LL_Warning, "Comparison failed at slice %i offset %zu, values %i, %f", sliceIndex, offset, startValue, base[offset]);
                return false;
            }
            startValue++;
        }
    }
    return true;
}

void test_create_partitioners_and_partitionings(Laik_Unit_Test_Data* testData) {
    testData->blockPartitioner = laik_new_block_partitioner1();
    testData->blockPartitioning = laik_new_partitioning(testData->blockPartitioner, testData->world, testData->space, 0);

    testData->masterPartitioner = laik_Master;
    testData->masterPartitioning = laik_new_partitioning(testData->masterPartitioner, testData->world, testData->space, 0);
}

//void tprintf(char* msg, ...) __attribute__ ((format (printf, 1, 2)));
//
//void tprintf(char* msg, ...) {
//    printf(msg, )
//}

#define TPRINTF(...) { printf("## TEST %i: ", laik_myid(laik_world(inst))); printf(__VA_ARGS__); }
//#define TPRINTF(...) {}

#endif //LAIK_FAULT_TOLERANCE_TEST_H
