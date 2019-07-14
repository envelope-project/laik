//
// Created by Vincent Bode on 09/05/2019.
//

#include <laik-internal.h>
#include <assert.h>
#include <string.h>

#define SLICE_ROTATE_DISTANCE 1

Laik_Checkpoint* initCheckpoint(Laik_Instance *laikInstance, Laik_Space *space, const Laik_Data *data);

void initBuffers(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Data *data, void **base,
                 uint64_t *count, void **backupBase, uint64_t *backupCount);

laik_run_partitioner_t wrapPartitionerRun(const Laik_Partitioner *currentPartitioner);

Laik_Partitioner *create_checkpoint_partitioner(Laik_Partitioner *currentPartitioner);

void migrateData(Laik_Data *sourceData, Laik_Data *targetData, Laik_Partitioning *partitioning);

void bufCopy(Laik_Mapping *mappingSource, Laik_Mapping *mappingTarget);

Laik_Checkpoint* laik_checkpoint_create(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *data,
                                       Laik_Partitioner *backupPartitioner, Laik_Group *backupGroup,
                                       enum _Laik_ReductionOperation reductionOperation) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint requested at iteration %i for space %s data %s\n", iteration, space->name,
             data->name);

    Laik_Checkpoint* checkpoint;

    checkpoint = initCheckpoint(laikInstance, space, data);

//    void *base, *backupBase;
//    uint64_t count, backupCount;
//    initBuffers(laikInstance, &checkpoint, data, &base, &count, &backupBase, &backupCount);
//
//    uint64_t data_length = backupCount * data->elemsize;
//    laik_log(LAIK_LL_Debug, "Checkpoint buffers allocated, copying data of size %lu\n", data_length);
//    memcpy(backupBase, base, data_length);
    migrateData(data, checkpoint->data, data->activePartitioning);


    if (backupPartitioner == NULL) {
        //TODO: This partitioner needs to be released at some point
        laik_log(LAIK_LL_Debug, "Creating a backup partitioner from original partitioner %s\n",
                 data->activePartitioning->partitioner->name);
        backupPartitioner = create_checkpoint_partitioner(data->activePartitioning->partitioner);
    }

    laik_log(LAIK_LL_Debug, "Switching to backup partitioning\n");
    Laik_Partitioning *currentPartitioning = data->activePartitioning;
    if (currentPartitioning->group != backupGroup) {
        currentPartitioning = NULL;
    }
    Laik_Partitioning *partitioning = laik_new_partitioning(backupPartitioner, backupGroup, space, currentPartitioning);
//    Laik_SliceArray *sliceArray = partitioning->saList->slices;
//    assert(partitioning && sliceArray);
//    for(unsigned int i = 0; i < sliceArray->count; i++) {
//        Laik_TaskSlice_Gen taskSliceGen = sliceArray->tslice[i];
//        sliceArray->tslice[i].task = (sliceArray->tslice[i].task + 1) % backupGroup->size;
//    }
    partitioning->name = "Backup partitioning";

    laik_switchto_partitioning(checkpoint->data, partitioning, LAIK_DF_Preserve, reductionOperation);
    laik_log_begin(LAIK_LL_Warning);
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

//    void *base, *backupBase;
//    uint64_t count, backupCount;


    assert(checkpoint->space);
    assert(checkpoint->data);

    assert(laik_space_size(space) == laik_space_size(checkpoint->space));
//    initBuffers(laikInstance, checkpoint, data, &base, &count, &backupBase, &backupCount);
//
//    memcpy(base, backupBase, backupCount * data->elemsize);
    migrateData(checkpoint->data, data, data->activePartitioning);

    laik_log(LAIK_LL_Info, "Checkpoint restore completed at iteration %i for space %s data %s\n", iteration,
             space->name, data->name);
}

void initBuffers(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Data *data, void **base,
                 uint64_t *count, void **backupBase, uint64_t *backupCount) {
    // TODO: Temporarily unused
    (void) laikInstance;
//    Laik_Partitioner *backupPartitioner = data->activePartitioning->;
    assert(data);
    Laik_Partitioning *backupPartitioning = data->activePartitioning;
    assert(backupPartitioning);

    laik_log(LAIK_LL_Debug, "Switching checkpoint buffers to target active partitioning %s", backupPartitioning->name);
    laik_switchto_partitioning(checkpoint->data, backupPartitioning, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(checkpoint->data, backupBase, backupCount);

    assert(data->activeMappings->count == 1);
    Laik_Mapping activeMapping = data->activeMappings->map[0];
    *base = activeMapping.base;
    *count = activeMapping.count;

    laik_log(LAIK_LL_Debug, "Preparing buffer for %lu elements of size %i (%lu)\n", *count, data->elemsize,
             *backupCount);
    assert(*count == *backupCount);
}

void migrateData(Laik_Data *sourceData, Laik_Data *targetData, Laik_Partitioning *partitioning) {
    laik_log(LAIK_LL_Debug, "Switching data containers to partitioning %s", partitioning->name);
    if (sourceData->activePartitioning != partitioning) {
        laik_switchto_partitioning(sourceData, partitioning, LAIK_DF_Preserve, LAIK_RO_None);
    }
    if (targetData->activePartitioning != partitioning) {
        laik_switchto_partitioning(targetData, partitioning, LAIK_DF_Preserve, LAIK_RO_None);
    }

    int numberMyMappings = laik_my_mapcount(partitioning);
    laik_log(LAIK_LL_Debug, "Copying %i data mappings", numberMyMappings);
    for (int mappingNumber = 0; mappingNumber < numberMyMappings; ++mappingNumber) {
        Laik_Mapping *sourceMapping = laik_map(sourceData, mappingNumber, 0);
        Laik_Mapping *targetMapping = laik_map(targetData, mappingNumber, 0);

        bufCopy(sourceMapping, targetMapping);
    }
}

// Copies from 1 to 2! Not like memcpy
void bufCopy(Laik_Mapping *mappingSource, Laik_Mapping *mappingTarget) {
    void *baseTarget = mappingTarget->base, *baseSource = mappingSource->base;
    uint64_t *strideSource = mappingSource->layout->stride;
    uint64_t *strideTarget = mappingTarget->layout->stride;
    uint64_t *sizeSource = mappingSource->size;
    uint64_t *sizeTarget = mappingTarget->size;

    assert(baseSource != NULL && baseTarget != NULL);
    assert(memcmp(sizeTarget, sizeSource, sizeof(uint64_t) * 3) == 0);

    assert(mappingTarget->data->type == mappingSource->data->type);
    Laik_Type *type = mappingTarget->data->type;

    laik_log(LAIK_LL_Debug,
             "Copying mapping of type %s (size %i) with strides z:%lu y:%lu x:%lu and size z:%lu y:%lu x:%lu to mapping with strides z:%lu y:%lu x:%lu",
             type->name, type->size,
             strideSource[2], strideSource[1], strideSource[0],
             sizeSource[2], sizeSource[1], sizeSource[0],
             strideTarget[2], strideTarget[1], strideTarget[0]);

    assert(strideSource[0] != 0 || strideSource[1] != 0 || strideSource[2] != 0);
    assert(strideTarget[0] != 0 || strideTarget[1] != 0 || strideTarget[2] != 0);

    uint64_t sizeZ = sizeTarget[2];
    uint64_t sizeY = sizeTarget[1];
    uint64_t sizeX = sizeTarget[0];

    assert(mappingSource->layout->dims == mappingTarget->layout->dims);
    // Sets all dimension sizes above current dimension to 1 (so that loop is executed).
    switch (mappingSource->layout->dims) {
        case 1:
            sizeY = 1;
            sizeZ = 1;
            break;
        case 2:
            sizeZ = 1;
            break;
        case 3:
            break;
        default:
            laik_log(LAIK_LL_Panic, "Unknown dimensionality in bufCopy: %i", mappingSource->layout->dims);
    }

    for (uint64_t z = 0; z < sizeZ; ++z) {
        for (uint64_t y = 0; y < sizeY; ++y) {
            for (uint64_t x = 0; x < sizeX; ++x) {
                memcpy((unsigned char *) baseTarget +
                       ((z * strideTarget[2]) + (y * strideTarget[1]) + (x * strideTarget[0])) * type->size,
                       (unsigned char *) baseSource +
                       ((z * strideSource[2]) + (y * strideSource[1]) + (x * strideTarget[0])) * type->size,
                       type->size);
            }
        }
    }
}

Laik_Checkpoint* initCheckpoint(Laik_Instance *laikInstance, Laik_Space *space, const Laik_Data *data) {
    //TODO: Temporarily unused
    (void) laikInstance;
    (void) data;

    Laik_Checkpoint* checkpoint = malloc(sizeof(Laik_Checkpoint));
    if(checkpoint == NULL) {
        laik_panic("Out of memory allocating checkpoint!");
        assert(0);
    }
//    (*checkpoint).space = laik_new_space_1d(laikInstance, laik_space_size(space) * data->elemsize);
//    laik_set_space_name((*checkpoint).space, "checkpoint");
    checkpoint->space = space;

    checkpoint->data = laik_new_data((*checkpoint).space, data->type);
    laik_data_set_name(checkpoint->data, "Backup data");
    return checkpoint;
}


void run_wrapped_partitioner(Laik_SliceReceiver *receiver, Laik_PartitionerParams *params) {
    Laik_Partitioner *originalPartitioner = (Laik_Partitioner *) params->partitioner->data;
    Laik_PartitionerParams modifiedParams = {
            .space = params->space,
            .group = params->group,
            .other = params->other,
            .partitioner = originalPartitioner
    };
//    Laik_Group modifiedGroup;
//
//    // Change the original laik group rotating all ids by 1
//    // This should cause data to be switched to the neighbor
//    memcpy(&modifiedGroup, params->group, sizeof(Laik_Group));
//    modifiedGroup.myid = (modifiedGroup.myid + 1) % modifiedGroup.size;
//
//    laik_log(LAIK_LL_Debug, "wrap partitioner: modifying group of size %i, old id %i to new id %i", modifiedGroup.size,
//             params->group->myid, modifiedGroup.myid);
//
//    modifiedParams.partitioner = originalPartitioner;
////    modifiedParams.group = &modifiedGroup;
//    modifiedParams.group = params->group;
//    modifiedParams.other = params->other;
//    modifiedParams.space = params->space;

    originalPartitioner->run(receiver, &modifiedParams);

//    laik_log(LAIK_LL_Debug, "wrap partitioner: rotating slice array of size %i by %i. Number mappings: %i.",
//             receiver->array->count, SLICE_ROTATE_DISTANCE, receiver->array->map_count);
//
//    //Currently, only distance 1 supported
//    static_assert(SLICE_ROTATE_DISTANCE == 1, "");
//    Laik_TaskSlice_Gen temp;
//    //Save first element in temp
//    memcpy(&temp, &(receiver->array->tslice[0]), sizeof(Laik_TaskSlice_Gen));
//    for(unsigned int i = 0; i < receiver->array->count - 1; i++) {
//        //Copy each element to the left
//        memcpy(&(receiver->array->tslice[i]), &(receiver->array->tslice[i+1]), sizeof(Laik_TaskSlice_Gen));
//    }
//    memcpy(&(receiver->array->tslice[receiver->array->count - 1]), &temp, sizeof(Laik_TaskSlice_Gen));

    // Duplicate slices to neighbor. Make sure to duplicate only the original ones, and not the ones we add in the
    // process
    laik_log(LAIK_LL_Debug, "wrap partitioner: duplicating slices for redundant storage");
    unsigned int originalCount = receiver->array->count;
    for (unsigned int i = 0; i < originalCount; i++) {
        Laik_TaskSlice_Gen duplicateSlice = receiver->array->tslice[i];
        int taskId = (duplicateSlice.task + 1) % receiver->params->group->size;
        laik_append_slice(receiver, taskId, &duplicateSlice.s, duplicateSlice.tag, duplicateSlice.data);
    }
}

//void run_single_copy_backup(Laik_SliceReceiver* receiver, Laik_PartitionerParams params) {
//    params.partitioner->data
//    receiver->
//}

//void laik_create_backup_partitioning(Laik_Partitioning* currentPartitioning) {
//    SliceArray_Entry* entry = currentPartitioning->saList;
//    assert(entry->info == LAIK_AI_FULL);
//    entry->slices->tslice->s
//}


Laik_Partitioner *create_checkpoint_partitioner(Laik_Partitioner *currentPartitioner) {
    return laik_new_partitioner("checkpoint-partitioner", run_wrapped_partitioner, currentPartitioner,
                                currentPartitioner->flags);
}

int laik_location_get_world_offset(Laik_Group *group, int id);

bool laik_checkpoint_remove_failed_slices(Laik_Checkpoint *checkpoint, int (*nodeStatuses)[]) {
    Laik_Partitioning *backupPartitioning = checkpoint->data->activePartitioning;

    assert(backupPartitioning->saList->next == NULL && backupPartitioning->saList->info == LAIK_AI_FULL);
    Laik_SliceArray *sliceArray = backupPartitioning->saList->slices;
    for (unsigned int oldIndex = 0; oldIndex < sliceArray->count; ++oldIndex) {
        Laik_TaskSlice_Gen *taskSlice = &sliceArray->tslice[oldIndex];
        //TODO: export this to some sort of constant
        int taskIdInGroup = taskSlice->task;
        int taskIdInWorld = laik_location_get_world_offset(backupPartitioning->group, taskIdInGroup);
        if ((*nodeStatuses)[taskIdInWorld] != LAIK_FT_NODE_OK) {
            // Set this task slice's size to zero (don't get any data from here)
            taskSlice->s.from.i[0] = INT64_MIN;
            taskSlice->s.from.i[1] = INT64_MIN;
            taskSlice->s.from.i[2] = INT64_MIN;
            taskSlice->s.to.i[0] = INT64_MIN;
            taskSlice->s.to.i[1] = INT64_MIN;
            taskSlice->s.to.i[2] = INT64_MIN;
        }
    }
    laik_log_begin(LAIK_LL_Warning);
    laik_log_append("Eliminated partitioning:\n");
    laik_log_Partitioning(backupPartitioning);
    laik_log_flush("\n");

    return laik_partitioning_coversSpace(backupPartitioning);
}

// For a specific group and id (offset into the group), find the offset into the top level group (should be world) equal
// to the referenced rank
int laik_location_get_world_offset(Laik_Group *group, int id) {
    while(group->parent != NULL) {
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
