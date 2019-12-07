//
// Created by Vincent Bode on 06/10/2019.
//

#include "fault-tolerance-options.h"
#include <string.h>
#include <laik.h>
#include <stdio.h>
#include <laik-internal.h>

const FaultToleranceOptions FaultToleranceOptionsDefault = {
        -1,
        -1,
        0,
        0,
        -1,
        false,
        false
};

#define ENSURE_NUMBER_ARGUMENTS(num) if(*arg + num >= argc) { laik_log(LAIK_LL_Panic, "Missing argument for option %s.", argv[*arg]); return false;}

bool isFaultToleranceActive(FaultToleranceOptions * faultToleranceOptions) {
    return faultToleranceOptions->failureCheckFrequency > 0;
}

bool parseFaultToleranceOptions(int argc, char **argv, int *arg, int rank, FaultToleranceOptions *ftOptions) {
    if (strcmp("--plannedFailure", argv[*arg]) == 0) {
        if (rank == atoi(argv[*arg + 1])) {
            ENSURE_NUMBER_ARGUMENTS(2);
            ftOptions->failIteration = atoi(argv[*arg + 2]);
            laik_log(LAIK_LL_Info, "Rank %i will fail at iteration %i", rank, ftOptions->failIteration);
        }
        *arg += 2;
    }
    else if (strcmp("--checkpointFrequency", argv[*arg]) == 0) {
        ENSURE_NUMBER_ARGUMENTS(1);
        ftOptions->checkpointFrequency = atoi(argv[*arg + 1]);
        if (rank == 0) {
            laik_log(LAIK_LL_Info, "Setting checkpoint frequency to %i.", ftOptions->checkpointFrequency);
        }
        *arg += 1;
    }
    else if (strcmp("--redundancyCount", argv[*arg]) == 0) {
        ENSURE_NUMBER_ARGUMENTS(1);
        ftOptions->redundancyCount = atoi(argv[*arg + 1]);
        if (rank == 0) {
            laik_log(LAIK_LL_Info, "Setting redundancy count to %i.", ftOptions->redundancyCount);
        }
        *arg += 1;
    }
    else if (strcmp("--rotationDistance", argv[*arg]) == 0) {
        ENSURE_NUMBER_ARGUMENTS(1);
        ftOptions->rotationDistance = atoi(argv[*arg + 1]);
        if (rank == 0) {
            laik_log(LAIK_LL_Info, "Setting rotation distance to %i.", ftOptions->rotationDistance);
        }
        *arg += 1;
    }
    else if (strcmp("--failureCheckFrequency", argv[*arg]) == 0) {
        ENSURE_NUMBER_ARGUMENTS(1);
        ftOptions->failureCheckFrequency = atoi(argv[*arg + 1]);
        if (rank == 0) {
            laik_log(LAIK_LL_Info, "Setting failure check frequency to %i.", ftOptions->failureCheckFrequency);
        }
        *arg += 1;
    }
    else if (strcmp("--skipCheckpointRecovery", argv[*arg]) == 0) {
        ENSURE_NUMBER_ARGUMENTS(0);
        ftOptions->skipCheckpointRecovery = true;
        if (rank == 0) {
            laik_log(LAIK_LL_Info, "Will skip recovering from checkpoints.");
        }
    }
    else if (strcmp("--delayCheckpointRelease", argv[*arg]) == 0) {
        ENSURE_NUMBER_ARGUMENTS(0);
        ftOptions->delayCheckpointRelease = true;
        if (rank == 0) {
            laik_log(LAIK_LL_Info, "Using delayed checkpoint release.");
        }
    } else {
        return false;
    }
    return true;
}

void exitIfFailureIteration(int iter, FaultToleranceOptions* faultToleranceOptions, Laik_Instance* inst) {
    if (iter == faultToleranceOptions->failIteration) {
        TRACE_EVENT_S("FAILURE-GENERATE", "");
        laik_log(LAIK_LL_Warning,
                "Process with rank %i now exiting on iteration %i.\n", laik_myid(laik_world(inst)),
                iter);
        exit(0);
    }
}
