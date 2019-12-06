//
// Created by Vincent Bode on 15/06/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_TEST_OUTPUT_H
#define LAIK_FAULT_TOLERANCE_TEST_OUTPUT_H

#include "laik.h"
#include "laik-internal.h"
#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

void writeDataToFile(char *fileNamePrefix, char *fileNameExtension, Laik_Data *data) {
    char debugOutputFileName[1024];
    snprintf(debugOutputFileName, sizeof(debugOutputFileName), "%s%i%s", fileNamePrefix, data->space->inst->myid,
             fileNameExtension);

    FILE *myOutput;
    myOutput = fopen(debugOutputFileName, "wb");
    assert(myOutput);
    assert(data->activeMappings->count == 1);

    Laik_Mapping* mapping = laik_map_def1(data, NULL, NULL);
    double *base = (double *) mapping->base;
    uint64_t dim0Size = mapping->size[0];
    uint64_t dim1Size = mapping->size[1];
    uint64_t stride = mapping->layout->stride[1];

    uint64_t count = mapping->count;
    assert(dim0Size * dim1Size == count);

    fprintf(myOutput, "P2\n%" PRIu64" %" PRIu64 "\n%i", dim0Size, dim1Size, 255);

    for (unsigned long y = 0; y < dim1Size; ++y) {
        fprintf(myOutput, "\n");
        for (unsigned long x = 0; x < dim0Size; ++x) {
            unsigned char value = (unsigned char) (base[y * stride + x] * 255);
            fprintf(myOutput, "%i ", value);
        }
    }
//    if(fwrite(base, data->type->size, data->activeMappings->map[0].count, myOutput)) {
//        assert(0);
//    }

    if (fclose(myOutput)) {
        assert(0);
    }

    laik_log(LAIK_LL_Info, "Wrote data to file %s", debugOutputFileName);
}

bool laik_point_in_slice(int64_t gx, int64_t gy, int64_t gz, Laik_Slice *slice){
    return ((slice->space->dims < 1) || (gx >= slice->from.i[0] && gx < slice->to.i[0]))
           && ((slice->space->dims < 2) || (gy >= slice->from.i[1] && gy < slice->to.i[1]))
           && ((slice->space->dims < 3) || (gz >= slice->from.i[2] && gz < slice->to.i[2]));
}

int laik_point_find_slice(int64_t gx, int64_t gy, int64_t gz, Laik_Partitioning* partitioning) {
    assert(partitioning->saList->next == NULL);
    for (unsigned int i = 0; i < partitioning->saList->slices->count; ++i) {
        if(laik_point_in_slice(gx, gy, gz, &partitioning->saList->slices->tslice[i].s)) {
            return i;
        }
    }
    return -1;
}

void
writeColorDataToFile(char *fileNameExtension, Laik_Data *data, Laik_Partitioning *partitioning,
                     unsigned char colors[][3], bool binaryPPM, bool suppressRank, char *fileNamePrefix,
                     double minValue, double maxValue) {
    char debugOutputFileName[1024];
    int myid = data->space->inst->myid;
    if(suppressRank) {
        myid = 0;
    }
    snprintf(debugOutputFileName, sizeof(debugOutputFileName), "%s%i%s", fileNamePrefix, myid,
             fileNameExtension);
//    snprintf(debugOutputFileName, sizeof(debugOutputFileName), "%s%i%s", fileNamePrefix, 0,
//             fileNameExtension);

    FILE *myOutput;
    myOutput = fopen(debugOutputFileName, "wb");
    assert(myOutput);
    assert(data->activeMappings->count == 1);

    Laik_Mapping* mapping = laik_map_def1(data, NULL, NULL);
    double *base = (double *) mapping->base;
    uint64_t dim0Size = mapping->size[0];
    uint64_t dim1Size = mapping->size[1];
    uint64_t stride = mapping->layout->stride[1];

    uint64_t count = mapping->count;
    assert(dim0Size * dim1Size == count);

    if(binaryPPM) {
        fprintf(myOutput, "P6\n");
    } else {
        fprintf(myOutput, "P3\n");
    }
    fprintf(myOutput, "%" PRIu64 " %" PRIu64 "\n%i\n", dim0Size, dim1Size, 255);

    for (unsigned long y = 0; y < dim1Size; ++y) {

        for (unsigned long x = 0; x < dim0Size; ++x) {
            int colorIndex = laik_point_find_slice(x, y, 0, partitioning);
            colorIndex = partitioning->saList->slices->tslice[colorIndex].task;
            colorIndex = laik_location_get_world_offset(partitioning->group, colorIndex);
            double value = base[y * stride + x];
            double normalizedValue = (value - minValue) / (maxValue - minValue);

            for (int i = 0; i < 3; ++i) {
                unsigned char colorValue = (unsigned char)(colors[colorIndex][i] * normalizedValue);
                if(binaryPPM) {
                    fwrite(&colorValue, sizeof(unsigned char), 1, myOutput);
                } else {
                    fprintf(myOutput, "%i ", colorValue);
                }
            }
        }
        //TODO: This was moved from up above, is it not allowed to have a trailing newline?
        if(!binaryPPM) {
            fprintf(myOutput, "\n");
        }
    }
//    if(fwrite(base, data->type->size, data->activeMappings->map[0].count, myOutput)) {
//        assert(0);
//    }

    if (fclose(myOutput)) {
        assert(0);
    }

    laik_log(LAIK_LL_Info, "Wrote data to file %s", debugOutputFileName);
}



#endif //LAIK_FAULT_TOLERANCE_TEST_OUTPUT_H
