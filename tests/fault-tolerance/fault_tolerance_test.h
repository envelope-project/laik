//
// Created by Vincent Bode on 19/05/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_H
#define LAIK_FAULT_TOLERANCE_TEST_H

#include "fault_tolerance_test_hash.h"
#include <assert.h>

#define TEST_SIZE 256

struct _Laik_Unit_Test_Data {
    Laik_Instance *inst;
    Laik_Group *world;

    Laik_Space *space;
    Laik_Data *data;

    Laik_Partitioner *blockPartitioner;
    Laik_Partitioning *blockPartitioning;

    Laik_Partitioner *masterPartitioner;
    Laik_Partitioning *masterPartitioning;
};

typedef struct _Laik_Unit_Test_Data Laik_Unit_Test_Data;

void __test_assert_success(const char *expression, const char *msg, int64_t expect, int64_t expr, const char *file,
                        unsigned int line) {
    laik_log(LAIK_LL_Info, "[OK] Test assertion %s: %s. Expected %" PRIi64 ", got %" PRIi64 " in %s:%i.", expression, msg, expect,
             expr, file, line);
}

void __test_assert_fail(const char *expression, const char *msg, int64_t expect, int64_t expr, const char *file,
                        unsigned int line) {
    laik_log(LAIK_LL_Panic, "[FAIL] Test assertion %s: %s. Expected %" PRIi64 ", got %" PRIi64 " in %s:%i.", expression, msg, expect,
             expr, file, line);
    abort();
}

#define test_assert(expect, expr, msg)                            \
  ((void) sizeof ((expr) ? 1 : 0), __extension__ ({            \
      if (expr == expect)                                \
        __test_assert_success (#expr, msg, expect, expr, __FILE__, __LINE__);    \
      else                                \
        __test_assert_fail (#expr, msg, expect, expr, __FILE__, __LINE__);    \
    }))

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
    laik_set_loglevel(LAIK_LL_Debug);
    testData->inst = laik_init(argc, argv);
    testData->world = laik_world(testData->inst);
    laik_log(LAIK_LL_Info, "Setting up test environment");

    // For testing purposes: world size 4
    assert(laik_size(testData->world) == 4);

    // provides meta-information for logging
    laik_set_phase(testData->inst, 0, "init", 0);
}

void test_write_sample_data(Laik_Data *data) {
    Laik_Partitioning *partitioning = laik_data_get_partitioning(data);
    for (int sliceIndex = 0; sliceIndex < laik_my_mapcount(partitioning); ++sliceIndex) {
        Laik_Mapping *mappingSource = laik_get_map(data, sliceIndex);

        Laik_NDimMapDataAllocation allocation;
        laik_checkpoint_setupNDimAllocation(mappingSource, &allocation);

        for (uint64_t z = 0; z < allocation.sizeZ; ++z) {
            for (uint64_t y = 0; y < allocation.sizeY; ++y) {
                for (uint64_t x = 0; x < allocation.sizeX; ++x) {
                    double value = (z + allocation.globalStartZ) * TEST_SIZE * TEST_SIZE
                                   + (y + allocation.globalStartY) * TEST_SIZE
                                   + x + allocation.globalStartX;
                    double *pos = (double *) ((unsigned char *) allocation.base +
                                              ((z * allocation.strideZ)
                                               + (y * allocation.strideY)
                                               + (x * allocation.strideX))
                                              * allocation.typeSize);
                    *pos = value;
                }
            }
        }

    }
}

//
void test_create_sample_data(Laik_Unit_Test_Data *testData, int dimensions) {
    switch (dimensions) {
        case 1:
            testData->space = laik_new_space_1d(testData->inst, TEST_SIZE);
            break;
        case 2:
            testData->space = laik_new_space_2d(testData->inst, TEST_SIZE, TEST_SIZE);
            break;
        case 3:
            testData->space = laik_new_space_3d(testData->inst, TEST_SIZE, TEST_SIZE, TEST_SIZE);
            break;
        default:
            test_assert(true, (dimensions > 0) && (dimensions <= 3), "Test data creation dimensionality");
    }
    testData->data = laik_new_data(testData->space, laik_Double);

    // Create some sample originalData to checkpoint
    Laik_Partitioning *masterPartitioning = laik_new_partitioning(laik_Master, testData->world, testData->space, 0);
    laik_switchto_partitioning(testData->data, masterPartitioning, LAIK_DF_None, LAIK_RO_None);

    test_write_sample_data(testData->data);
}


bool test_verify_sample_data(Laik_Data *data) {
    Laik_Partitioning *partitioning = laik_data_get_partitioning(data);
    for (int sliceIndex = 0; sliceIndex < laik_my_mapcount(partitioning); ++sliceIndex) {
        Laik_Mapping *mappingSource = laik_get_map(data, sliceIndex);

        Laik_NDimMapDataAllocation allocation;
        laik_checkpoint_setupNDimAllocation(mappingSource, &allocation);

        for (uint64_t z = 0; z < allocation.sizeZ; ++z) {
            for (uint64_t y = 0; y < allocation.sizeY; ++y) {
                for (uint64_t x = 0; x < allocation.sizeX; ++x) {
                    double value = (z + allocation.globalStartZ) * TEST_SIZE * TEST_SIZE
                                    + (y + allocation.globalStartY) * TEST_SIZE
                                    + x + allocation.globalStartX;
                    double *pos = (double *) ((unsigned char *) allocation.base +
                                              ((z * allocation.strideZ)
                                              + (y * allocation.strideY)
                                              + (x * allocation.strideX))
                                              * allocation.typeSize);
                    if(value != *pos) {
//                        fprintf(stderr, "Test failed at xyz %lu %lu %lu + xyz %lu %lu %lu, expected %f got %f\n", x, y, z, mappingSource->allocatedSlice.from.i[2], mappingSource->allocatedSlice.from.i[1], mappingSource->allocatedSlice.from.i[0], value, *pos);
                        return false;
                    }
                }
            }
        }

    }
    return true;
}

void test_create_partitioners_and_partitionings(Laik_Unit_Test_Data *testData) {
    testData->blockPartitioner = laik_new_block_partitioner1();
    testData->blockPartitioning = laik_new_partitioning(testData->blockPartitioner, testData->world, testData->space,
                                                        0);

    testData->masterPartitioner = laik_Master;
    testData->masterPartitioning = laik_new_partitioning(testData->masterPartitioner, testData->world, testData->space,
                                                         0);
}

//void tprintf(char* msg, ...) __attribute__ ((format (printf, 1, 2)));
//
//void tprintf(char* msg, ...) {
//    printf(msg, )
//}

#define TPRINTF(...) { printf("## TEST %i: ", laik_myid(laik_world(inst))); printf(__VA_ARGS__); }
//#define TPRINTF(...) {}

#endif //LAIK_FAULT_TOLERANCE_TEST_H
