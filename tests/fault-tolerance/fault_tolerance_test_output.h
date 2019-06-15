//
// Created by Vincent Bode on 15/06/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_OUTPUT_H
#define LAIK_FAULT_TOLERANCE_TEST_OUTPUT_H

#include "laik.h"
#include "laik-internal.h"
#include <stdio.h>
#include <assert.h>

void writeDataToFile(char *fileNamePrefix, char *fileNameExtension, Laik_Data *data) {
    char debugOutputFileName[1024];
    snprintf(debugOutputFileName, sizeof(debugOutputFileName), "%s%i%s", fileNamePrefix, data->space->inst->myid,
             fileNameExtension);

    FILE *myOutput;
    myOutput = fopen(debugOutputFileName, "wb");
    assert(myOutput);
    assert(data->activeMappings->count == 1);

    double *base = (double *) data->activeMappings->map[0].base;
    uint64_t stride = data->activeMappings->map[0].size[1];
    uint64_t dim0Size = data->activeMappings->map[0].size[0];

    int max = -1;
    uint64_t count = data->activeMappings->map[0].count;
    for (unsigned long i = 0; i < count; ++i) {
        if(max < (unsigned char) (base[i] * 255)) {
            max = (unsigned char) (base[i] * 255);
        }
    }
    assert(dim0Size * stride == count);

    fprintf(myOutput, "P2\n%lu %lu\n%i", dim0Size, stride, max);

    for (unsigned long i = 0; i < count; ++i) {
        if (i % stride == 0) {
            fprintf(myOutput, "\n");
        }
        fprintf(myOutput, "%i ", (unsigned char) (base[i] * 255));
    }
//    if(fwrite(base, data->type->size, data->activeMappings->map[0].count, myOutput)) {
//        assert(0);
//    }

    if (fclose(myOutput)) {
        assert(0);
    }
}


#endif //LAIK_FAULT_TOLERANCE_TEST_OUTPUT_H
