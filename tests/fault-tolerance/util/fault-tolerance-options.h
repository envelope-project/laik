//
// Created by Vincent Bode on 06/10/2019.
//

#ifndef LAIK_FAULT_TOLERANCE_OPTIONS_H
#define LAIK_FAULT_TOLERANCE_OPTIONS_H

#include <stdbool.h>
#include <laik.h>

#define FAULT_TOLERANCE_OPTIONS_HELP " Fault tolerance options:\n"\
    "  --plannedFailure <rank> <iteration> (default no failure, can be used once per rank)\n"\
    "  --checkpointFrequency <numIterations> (default -1, no checkpoints)\n"\
    "  --redundancyCount <count> (set number of redundant data slices to keep in checkpoints, default 1)\n"\
    "  --rotationDistance <distance> (set the distance between a process the process holding the same data redundantly)\n"\
    "  --failureCheckFrequency <numIterations> (defaults to checkpoint frequency)\n"\
    "  --skipCheckpointRecovery (default off, turn on to keep working with broken data after failure)\n"\
    "  --delayCheckpointRelease (release old checkpoint only after creating a new one, has higher memory usage but can tolerate failure during checkpointing)\n"

struct _FaultToleranceOptions {
    int failIteration;
    int checkpointFrequency;
    int redundancyCount;
    int rotationDistance;
    int failureCheckFrequency;
    bool skipCheckpointRecovery;
    bool delayCheckpointRelease;
};

typedef struct _FaultToleranceOptions FaultToleranceOptions;
extern const FaultToleranceOptions FaultToleranceOptionsDefault;

bool parseFaultToleranceOptions(int argc, char **argv, int *arg, int rank, FaultToleranceOptions *ftOptions);
void exitIfFailureIteration(int iter, FaultToleranceOptions* faultToleranceOptions, Laik_Instance* inst);

#endif //LAIK_FAULT_TOLERANCE_OPTIONS_H
