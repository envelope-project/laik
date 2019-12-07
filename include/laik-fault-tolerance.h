//
// Created by Vincent Bode on 11/05/2019.
//

#ifndef LAIK_LAIK_FAULT_TOLERANCE_H
#define LAIK_LAIK_FAULT_TOLERANCE_H

#define LAIK_CHECKPOINT_SLICE_ROTATION_DISTANCE 1
typedef enum {
    LAIK_FT_NODE_OK = 1,
    LAIK_FT_NODE_FAULT = -1
} LAIK_FT_NODE_STATUS;

struct _Laik_Checkpoint {
    Laik_Space* space;
    Laik_Data* data;
};

typedef struct _Laik_Checkpoint Laik_Checkpoint;

struct _Laik_NDimMapDataAllocation {
    void* base;
    uint64_t strideX, strideY, strideZ;
    uint64_t sizeX, sizeY, sizeZ;
    int64_t globalStartX, globalStartY, globalStartZ;
    int typeSize;
};

typedef struct _Laik_NDimMapDataAllocation Laik_NDimMapDataAllocation;

void laik_checkpoint_setupNDimAllocation(const Laik_Mapping *mappingSource, Laik_NDimMapDataAllocation *allocation);

Laik_Checkpoint *
laik_checkpoint_create(Laik_Data *data, Laik_Partitioner *backupPartitioner, int redundancyCount, int rotationDistance,
                       Laik_Group *backupGroup, enum _Laik_ReductionOperation reductionOperation);

void laik_checkpoint_restore(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space, Laik_Data *data);

int laik_failure_check_nodes(Laik_Instance *laikInstance, Laik_Group *checkGroup, int (*failedNodes));
int laik_failure_eliminate_nodes(Laik_Instance *instance, int count, int (*nodeStatuses));
void laik_failure_default_error_handler(Laik_Instance* inst, void *errors);

bool laik_checkpoint_remove_failed_slices(Laik_Checkpoint *checkpoint, Laik_Group *checkGroup, int *nodeStatuses);

Laik_Group* laik_world_fault_tolerant(Laik_Instance* instance);

int laik_location_get_world_offset(Laik_Group *group, int id);

void laik_checkpoint_free(Laik_Checkpoint *checkpoint);

#endif //LAIK_LAIK_FAULT_TOLERANCE_H
