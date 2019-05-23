//
// Created by Vincent Bode on 09/05/2019.
//

#include <laik-internal.h>
#include <assert.h>
#include <string.h>

#define SLICE_ROTATE_DISTANCE 1

Laik_Checkpoint initCheckpoint(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space,
                               const Laik_Data *data);

void initBuffers(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Data *data, void **base,
                 uint64_t *count, void **backupBase, uint64_t *backupCount);

laik_run_partitioner_t wrapPartitionerRun(const Laik_Partitioner *currentPartitioner);

Laik_Partitioner* create_checkpoint_partitioner(Laik_Partitioner *currentPartitioner);

Laik_Checkpoint laik_checkpoint_create(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *data,
                                       Laik_Partitioner *backupPartitioner) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint requested at iteration %i\n", iteration);

    Laik_Checkpoint checkpoint;

    checkpoint = initCheckpoint(laikInstance, &checkpoint, space, data);

    void *base, *backupBase;
    uint64_t count, backupCount;
    initBuffers(laikInstance, &checkpoint, data, &base, &count, &backupBase, &backupCount);
    
    uint64_t data_length = backupCount * data->elemsize;
    laik_log(LAIK_LL_Debug, "Checkpoint buffers allocated, copying data of size %lu\n", data_length);
    memcpy(backupBase, base, data_length);

    if(backupPartitioner == NULL) {
        //TODO: This partitioner needs to be released at some point
        laik_log(LAIK_LL_Debug, "Creating a backup partitioner from original partitioner %s\n", data->activePartitioning->partitioner->name);
        backupPartitioner = create_checkpoint_partitioner(data->activePartitioning->partitioner);
    }

    laik_log(LAIK_LL_Debug, "Switching to backup partitioning\n");
    Laik_Partitioning* partitioning = laik_new_partitioning(backupPartitioner, data->activePartitioning->group, space, 0);
    partitioning->name = "Backup partitioning";
    laik_switchto_partitioning(checkpoint.data, partitioning, LAIK_DF_Preserve, LAIK_RO_None);

    laik_log(LAIK_LL_Info, "Checkpoint %s completed\n", checkpoint.space->name);
    return checkpoint;
}


void
laik_checkpoint_restore(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space, Laik_Data *data) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint restore requested at iteration %i\n", iteration);

    void *base, *backupBase;
    uint64_t count, backupCount;

    assert(laik_space_size(space) == laik_space_size(checkpoint->space));
    initBuffers(laikInstance, checkpoint, data, &base, &count, &backupBase, &backupCount);

    memcpy(base, backupBase, backupCount * data->elemsize);

    laik_log(LAIK_LL_Info, "Checkpoint restore %s completed", checkpoint->space->name);
}

void initBuffers(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Data *data, void **base,
                 uint64_t *count, void **backupBase, uint64_t *backupCount) {
    // TODO: Temporarily unused
    (void)laikInstance;
//    Laik_Partitioner *backupPartitioner = data->activePartitioning->;
    Laik_Partitioning *backupPartitioning = data->activePartitioning;

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

Laik_Checkpoint initCheckpoint(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space,
                               const Laik_Data *data) {
    //TODO: Temporarily unused
    (void) laikInstance; (void)data;
//    (*checkpoint).space = laik_new_space_1d(laikInstance, laik_space_size(space) * data->elemsize);
//    laik_set_space_name((*checkpoint).space, "checkpoint");
    checkpoint->space = space;

    checkpoint->data = laik_new_data((*checkpoint).space, data->type);
    return (*checkpoint);
}


void run_wrapped_partitioner(Laik_SliceReceiver *receiver, Laik_PartitionerParams *params) {
    Laik_Partitioner *originalPartitioner = (Laik_Partitioner *) params->partitioner->data;
    Laik_PartitionerParams modifiedParams;
    Laik_Group modifiedGroup;

    // Change the original laik group rotating all ids by 1
    // This should cause data to be switched to the neighbor
    memcpy(&modifiedGroup, params->group, sizeof(Laik_Group));
    modifiedGroup.myid = (modifiedGroup.myid + 1) % modifiedGroup.size;

    laik_log(LAIK_LL_Debug, "wrap partitioner: modifying group of size %i, old id %i to new id %i", modifiedGroup.size, params->group->myid, modifiedGroup.myid);

    modifiedParams.partitioner = originalPartitioner;
    modifiedParams.group = &modifiedGroup;
    modifiedParams.other = params->other;
    modifiedParams.space = params->space;

    originalPartitioner->run(receiver, &modifiedParams);

    laik_log(LAIK_LL_Debug, "wrap partitioner: rotating slice array of size %i by %i.", receiver->array->count, SLICE_ROTATE_DISTANCE);

    //Currently, only distance 1 supported
    static_assert(SLICE_ROTATE_DISTANCE == 1, "");
    Laik_TaskSlice_Gen temp;
    //Save first element in temp
    memcpy(&temp, &(receiver->array->tslice[0]), sizeof(Laik_TaskSlice_Gen));
    for(unsigned int i = 0; i < receiver->array->count - 1; i++) {
        //Copy each element to the left
        memcpy(&(receiver->array->tslice[i]), &(receiver->array->tslice[i+1]), sizeof(Laik_TaskSlice_Gen));
    }
    memcpy(&(receiver->array->tslice[receiver->array->count - 1]), &temp, sizeof(Laik_TaskSlice_Gen));

    for (unsigned int j = 0; j < receiver->array->count; ++j) {
        laik_log(LAIK_LL_Debug, "slice at %i: start %lu", j, receiver->array->tslice[j].s.from.i[0]);
    }

}


Laik_Partitioner* create_checkpoint_partitioner(Laik_Partitioner *currentPartitioner) {
    return laik_new_partitioner("checkpoint-partitioner", run_wrapped_partitioner, currentPartitioner, currentPartitioner->flags);
}
