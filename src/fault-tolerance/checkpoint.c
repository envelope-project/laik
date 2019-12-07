//
// Created by Vincent Bode on 09/05/2019.
//

#include <laik-internal.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>

Laik_Checkpoint *initCheckpoint(Laik_Instance *laikInstance, Laik_Space *space, const Laik_Data *data);

Laik_Partitioner *
create_checkpoint_partitioner(Laik_Partitioner *currentPartitioner, int redundancyCount, int rotationDistance,
                              bool suppressBackupSliceTag);

void migrateData(Laik_Data *sourceData, Laik_Data *targetData, Laik_Partitioning *partitioning);

void bufCopy(Laik_Mapping *mappingSource, Laik_Mapping *mappingTarget);

Laik_Checkpoint *laik_checkpoint_create(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *data,
                                        Laik_Partitioner *backupPartitioner, int redundancyCount, int rotationDistance,
                                        Laik_Group *backupGroup, enum _Laik_ReductionOperation reductionOperation) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint requested at iteration %i for space %s data %s\n", iteration, space->name,
             data->name);

    Laik_Checkpoint *checkpoint;

    checkpoint = initCheckpoint(laikInstance, space, data);

    migrateData(data, checkpoint->data, data->activePartitioning);

    if (backupGroup == NULL) {
        backupGroup = data->activePartitioning->group;
    }

    if (backupPartitioner == NULL) {
        laik_log(LAIK_LL_Debug, "Using original partitioner %s\n",
                 data->activePartitioning->partitioner->name);
        backupPartitioner = data->activePartitioning->partitioner;
    }

    if (redundancyCount != 0) {
        //TODO: This partitioner needs to be released at some point
        //Todo: Should it be possible to not suppress tags? Cannot really think of a use case for this
        backupPartitioner = create_checkpoint_partitioner(backupPartitioner, redundancyCount, rotationDistance, true);
    }

    laik_log(LAIK_LL_Debug, "Switching to backup partitioning\n");
    Laik_Partitioning *currentPartitioning = data->activePartitioning;
    if (currentPartitioning->group != backupGroup) {
        currentPartitioning = NULL;
    }
    Laik_Partitioning *partitioning = laik_new_partitioning(backupPartitioner, backupGroup, space, currentPartitioning);
    partitioning->name = "Backup partitioning";

    laik_switchto_partitioning(checkpoint->data, partitioning, LAIK_DF_Preserve, reductionOperation);
    laik_log_begin(LAIK_LL_Debug);
    laik_log_append("Active partitioning: \n");
    laik_log_Partitioning(data->activePartitioning);

    laik_log_append("\nBackup partitioning: \n");
    laik_log_Partitioning(partitioning);
    laik_log_flush(NULL);

    laik_log(LAIK_LL_Info, "Checkpoint %s completed\n", checkpoint->space->name);
    return checkpoint;
}


void
laik_checkpoint_restore(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space, Laik_Data *data) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint restore requested at iteration %i for space %s data %s\n", iteration,
             space->name, data->name);

    assert(checkpoint->space);
    assert(checkpoint->data);
    assert(laik_space_size(space) == laik_space_size(checkpoint->space));

    migrateData(checkpoint->data, data, data->activePartitioning);

    laik_log(LAIK_LL_Info, "Checkpoint restore completed at iteration %i for space %s data %s\n", iteration,
             space->name, data->name);
}

void migrateData(Laik_Data *sourceData, Laik_Data *targetData, Laik_Partitioning *partitioning) {
    laik_log_begin(LAIK_LL_Debug);
    laik_log_append("Migrate source partitioning:\n");
    laik_log_Partitioning(sourceData->activePartitioning);
    laik_log_append("\nto target partitioning:\n");
    laik_log_Partitioning(targetData->activePartitioning);
    laik_log_flush("\nusing partitioning %s.\n", partitioning->name);

    if (sourceData->activePartitioning != partitioning) {
        laik_switchto_partitioning(sourceData, partitioning, LAIK_DF_Preserve, LAIK_RO_None);
    }
    if (targetData->activePartitioning != partitioning) {
        laik_switchto_partitioning(targetData, partitioning, LAIK_DF_Preserve, LAIK_RO_None);
    }

    int numberMyMappings = laik_my_mapcount(partitioning);
    laik_log(LAIK_LL_Debug, "Copying %i data mappings", numberMyMappings);
    for (int mappingNumber = 0; mappingNumber < numberMyMappings; ++mappingNumber) {
        Laik_Mapping *sourceMapping = laik_get_map(sourceData, mappingNumber);
        Laik_Mapping *targetMapping = laik_get_map(targetData, mappingNumber);

        bufCopy(sourceMapping, targetMapping);
    }
}

// Copies from 1 to 2! Not like memcpy
void bufCopy(Laik_Mapping *mappingSource, Laik_Mapping *mappingTarget) {
    Laik_NDimMapDataAllocation source, target;
    laik_checkpoint_setupNDimAllocation(mappingSource, &source);
    laik_checkpoint_setupNDimAllocation(mappingTarget, &target);

    assert(source.base != NULL && target.base != NULL);
    assert(source.sizeZ == target.sizeZ && source.sizeY == target.sizeY && source.sizeX == target.sizeX);
    assert(mappingTarget->data->type == mappingSource->data->type);
    assert(mappingSource->layout->dims == mappingTarget->layout->dims);

    Laik_Type *type = mappingTarget->data->type;

    laik_log(LAIK_LL_Debug,
             "Copying mapping of type %s (size %i) with strides z:%" PRIu64
             " y:%" PRIu64 " x:%" PRIu64 " and size z:%" PRIu64 " y:%" PRIu64
             " x:%" PRIu64 " to mapping with strides z:%" PRIu64 " y:%" PRIu64 " x:%" PRIu64,
             type->name, type->size,
             source.strideZ, source.strideY, source.strideX,
             source.sizeZ, source.sizeY, source.sizeX,
             target.strideZ, target.strideY, target.strideX);

    for (uint64_t z = 0; z < source.sizeZ; ++z) {
        for (uint64_t y = 0; y < source.sizeY; ++y) {
            for (uint64_t x = 0; x < source.sizeX; ++x) {
                memcpy((unsigned char *) target.base +
                       ((z * target.strideZ) + (y * target.strideY) + (x * target.strideX)) * type->size,
                       (unsigned char *) source.base +
                       ((z * source.strideZ) + (y * source.strideY) + (x * source.strideX)) * type->size,
                       type->size);
            }
        }
    }
}

Laik_Checkpoint *initCheckpoint(Laik_Instance *laikInstance, Laik_Space *space, const Laik_Data *data) {
    //TODO: Temporarily unused
    (void) laikInstance;

    Laik_Checkpoint *checkpoint = malloc(sizeof(Laik_Checkpoint));
    if (checkpoint == NULL) {
        laik_panic("Out of memory allocating checkpoint!");
        assert(0);
    }
    checkpoint->space = space;

    checkpoint->data = laik_new_data((*checkpoint).space, data->type);
    laik_data_set_name(checkpoint->data, "Backup data");
    return checkpoint;
}

struct _LaikCheckpointPartitionerData {
    int redundancyCounts;
    int rotationDistance;
    bool suppressBackupSliceTag;
    Laik_Partitioner *originalPartitioner;
};
typedef struct _LaikCheckpointPartitionerData LaikCheckpointPartitionerData;

void run_wrapped_partitioner(Laik_SliceReceiver *receiver, Laik_PartitionerParams *params) {
    LaikCheckpointPartitionerData *checkpointPartitionerData = params->partitioner->data;
    Laik_Partitioner *originalPartitioner = checkpointPartitionerData->originalPartitioner;
    Laik_PartitionerParams modifiedParams = {
            .space = params->space,
            .group = params->group,
            .other = params->other,
            .partitioner = originalPartitioner
    };

    originalPartitioner->run(receiver, &modifiedParams);

    // Duplicate slices to neighbor. Make sure to duplicate only the original ones, and not the ones we add in the
    // process
    laik_log(LAIK_LL_Info, "wrap partitioner: duplicating slices for redundant storage (%i times, %i distance)",
             checkpointPartitionerData->redundancyCounts, checkpointPartitionerData->rotationDistance);
    unsigned int originalCount = receiver->array->count;
    for (int redundancyCount = 0; redundancyCount < checkpointPartitionerData->redundancyCounts; ++redundancyCount) {
        for (unsigned int i = 0; i < originalCount; i++) {
            Laik_TaskSlice_Gen duplicateSlice = receiver->array->tslice[i];
            int taskId = (duplicateSlice.task + (redundancyCount + 1) * checkpointPartitionerData->rotationDistance) % receiver->params->group->size;
            int tag = checkpointPartitionerData->suppressBackupSliceTag ? 0 : duplicateSlice.tag;
            if(duplicateSlice.task == taskId) {
                laik_log_begin(LAIK_LL_Panic);
                laik_log_append("A checkpoint slice (");
                laik_log_Slice(&duplicateSlice.s);
                laik_log_append(") and one of its redundant copies are being placed on the same task with id %i. "
                                "This means that redundancy is incorrectly configured. "
                                "Please adjust redundancy count and rotation distance.", taskId);
                laik_log_flush("");
            }
            laik_append_slice(receiver, taskId, &duplicateSlice.s, tag, duplicateSlice.data);
        }
    }
}



Laik_Partitioner *
create_checkpoint_partitioner(Laik_Partitioner *currentPartitioner, int redundancyCount, int rotationDistance,
                              bool suppressBackupSliceTag) {
    Laik_Partitioner *checkpointPartitioner = laik_new_partitioner("checkpoint-partitioner", run_wrapped_partitioner,
                                                                   currentPartitioner,
                                                                   currentPartitioner->flags);
    LaikCheckpointPartitionerData* partitionerData;
    partitionerData = malloc(sizeof(Laik_Partitioner));
    if (!partitionerData) {
        laik_panic("Out of memory allocating LaikCheckpointPartitionerData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    checkpointPartitioner->data = partitionerData;
    partitionerData->rotationDistance = rotationDistance;
    partitionerData->redundancyCounts = redundancyCount;
    partitionerData->suppressBackupSliceTag = suppressBackupSliceTag;
    partitionerData->originalPartitioner = currentPartitioner;
    return checkpointPartitioner;
}

void set_slice_to_empty(Laik_Slice *slice) {
    //TODO: Find out why this fix works
    (*slice).from.i[0] = INT64_MIN;
    (*slice).from.i[1] = INT64_MIN;
    (*slice).from.i[2] = INT64_MIN;
    (*slice).to.i[0] = INT64_MIN;
    (*slice).to.i[1] = INT64_MIN;
    (*slice).to.i[2] = INT64_MIN;
}

void laik_checkpoint_remove_redundant_slices(Laik_Checkpoint *checkpoint) {
    Laik_Partitioning *backupPartitioning = checkpoint->data->activePartitioning;

    assert(backupPartitioning->saList->next == NULL && backupPartitioning->saList->info == LAIK_AI_FULL);
    Laik_SliceArray *sliceArray = backupPartitioning->saList->slices;
    for (unsigned int oldIndex = 0; oldIndex < sliceArray->count; ++oldIndex) {
        for (unsigned int newIndex = 0; newIndex < oldIndex; ++newIndex) {
            if (laik_slice_isEqual(&sliceArray->tslice[oldIndex].s, &sliceArray->tslice[newIndex].s)) {
                set_slice_to_empty(&sliceArray->tslice[oldIndex].s);
            }
        }
    }
}

bool laik_checkpoint_remove_failed_slices(Laik_Checkpoint *checkpoint, Laik_Group *checkGroup, int *nodeStatuses) {
    Laik_Partitioning *backupPartitioning = checkpoint->data->activePartitioning;

    assert(backupPartitioning->saList->next == NULL && backupPartitioning->saList->info == LAIK_AI_FULL);

    assert(backupPartitioning->group->gid == checkGroup->gid);
    Laik_SliceArray *sliceArray = backupPartitioning->saList->slices;
    for (unsigned int oldIndex = 0; oldIndex < sliceArray->count; ++oldIndex) {
        Laik_TaskSlice_Gen *taskSlice = &sliceArray->tslice[oldIndex];
        //TODO: export this to some sort of constant
        int taskIdInGroup = taskSlice->task;
//        int taskIdInWorld = laik_location_get_world_offset(backupPartitioning->group, taskIdInGroup);
        if (nodeStatuses[taskIdInGroup] != LAIK_FT_NODE_OK) {
            // Set this task slice's size to zero (don't get any data from here)
            set_slice_to_empty(&taskSlice->s);
        }
    }
    laik_log_begin(LAIK_LL_Debug);
    laik_log_append("Eliminated partitioning:\n");
    laik_log_Partitioning(backupPartitioning);
    laik_log_flush("\n");

    laik_checkpoint_remove_redundant_slices(checkpoint);

    laik_log_begin(LAIK_LL_Debug);
    laik_log_append("Non-redundant partitioning:\n");
    laik_log_Partitioning(backupPartitioning);
    laik_log_flush("\n");

    return laik_partitioning_coversSpace(backupPartitioning);
}

// For a specific group and id (offset into the group), find the offset into the top level group (should be world) equal
// to the referenced rank
int laik_location_get_world_offset(Laik_Group *group, int id) {
    while (group->parent != NULL) {
        // Ensure we don't go out of bounds
        assert(id >= 0 && id < group->size);
        // Ensure a mapping from this group's ids to the parent group's ids is provided
        assert(group->toParent != NULL);

        id = group->toParent[id];
        group = group->parent;
    }
    assert(id >= 0 && id < group->size);
    return id;
}

void laik_checkpoint_free(Laik_Checkpoint *checkpoint) {
    laik_free(checkpoint->data);
    free(checkpoint);
}

void laik_checkpoint_setupNDimAllocation(const Laik_Mapping *mappingSource, Laik_NDimMapDataAllocation *allocation) {
    (*allocation).base = mappingSource->base;
    uint64_t *strideSource = mappingSource->layout->stride;
    const uint64_t *sizeSource = mappingSource->size;
    const int64_t  *fromSource = mappingSource->allocatedSlice.from.i;

    (*allocation).typeSize = mappingSource->data->type->size;

    assert(strideSource[0] != 0 || strideSource[1] != 0 || strideSource[2] != 0);

    (*allocation).sizeZ = sizeSource[2];
    (*allocation).sizeY = sizeSource[1];
    (*allocation).sizeX = sizeSource[0];

    (*allocation).strideZ = strideSource[2];
    (*allocation).strideY = strideSource[1];
    (*allocation).strideX = strideSource[0];

    allocation->globalStartZ = fromSource[2];
    allocation->globalStartY = fromSource[1];
    allocation->globalStartX = fromSource[0];

    // Sets all dimension sizes above current dimension to 1 (so that loop is executed).
    switch (mappingSource->layout->dims) {
        case 1:
            (*allocation).sizeY = 1;
            (*allocation).sizeZ = 1;
            allocation->globalStartY = 0;
            allocation->globalStartZ = 0;
            break;
        case 2:
            (*allocation).sizeZ = 1;
            allocation->globalStartZ = 0;
            break;
        case 3:
            break;
        default:
            laik_log(LAIK_LL_Panic, "Unknown dimensionality in test verify sample data: %i",
                     mappingSource->layout->dims);
    }
}
