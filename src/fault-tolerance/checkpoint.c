//
// Created by Vincent Bode on 09/05/2019.
//

#include <laik-internal.h>
#include <assert.h>
#include <string.h>

Laik_Checkpoint initCheckpoint(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Space *space,
                               const Laik_Data *data);

void initBuffers(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Data *data, void **base,
                 uint64_t *count, void **backupBase, uint64_t *backupCount);

laik_run_partitioner_t wrapPartitionerRun(const Laik_Partitioner *currentPartitioner);

Laik_Checkpoint laik_checkpoint_create(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *data) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint requested at iteration %i\n", iteration);

    Laik_Checkpoint checkpoint;

    checkpoint = initCheckpoint(laikInstance, &checkpoint, space, data);

    void *base, *backupBase;
    uint64_t count, backupCount;
    initBuffers(laikInstance, &checkpoint, data, &base, &count, &backupBase, &backupCount);

    memcpy(backupBase, base, backupCount);

    laik_log(LAIK_LL_Info, "Checkpoint %s completed\n", checkpoint.space->name);
    return checkpoint;
}


void laik_checkpoint_restore(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space, Laik_Data *data) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint restore requested at iteration %i\n", iteration);

    void *base, *backupBase;
    uint64_t count, backupCount;

    assert(laik_space_size(space) * data->elemsize == laik_space_size(checkpoint->space));
    initBuffers(laikInstance, checkpoint, data, &base, &count, &backupBase, &backupCount);

    memcpy(base, backupBase, backupCount);

    laik_log(LAIK_LL_Info, "Checkpoint restore %s completed", checkpoint->space->name);
}

void initBuffers(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Data *data, void **base,
                 uint64_t *count, void **backupBase, uint64_t *backupCount) {

    Laik_Partitioner *backupPartitioner = laik_new_block_partitioner1();
    Laik_Partitioning *backupPartitioning = laik_new_partitioning(backupPartitioner, laik_world(laikInstance), checkpoint->space, NULL);

    laik_switchto_partitioning(checkpoint->data, backupPartitioning, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(checkpoint->data, backupBase, backupCount);

    assert(data->activeMappings->count == 1);
    Laik_Mapping activeMapping = data->activeMappings->map[0];
    *base = activeMapping.base;
    *count = activeMapping.count;

    laik_log(LAIK_LL_Debug, "Preparing buffer for %lu elements of size %i (%lu)\n", *count, data->elemsize, *backupCount);
    assert(*count * data->elemsize == *backupCount);
}

Laik_Checkpoint initCheckpoint(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Space *space,
                               const Laik_Data *data) {
    (*checkpoint).space = laik_new_space_1d(laikInstance, laik_space_size(space) * data->elemsize);
    laik_set_space_name((*checkpoint).space, "checkpoint");

    (*checkpoint).data = laik_new_data((*checkpoint).space, laik_UChar);
    return (*checkpoint);
}

void run_wrapped_partitioner(Laik_SliceReceiver* receiver, Laik_PartitionerParams* params) {
    Laik_Partitioner* originalPartitioner = (Laik_Partitioner*)params->partitioner->data;
    Laik_PartitionerParams modifiedParams;
    Laik_Group modifiedGroup;

    // Change the original laik group rotating all ids by 1
    // This should cause data to be switched to the neighbor
    memcpy(&modifiedGroup, params->group, sizeof(Laik_Group));
    modifiedGroup.myid = (modifiedGroup.myid + 1) % modifiedGroup.size;

    modifiedParams.partitioner = originalPartitioner;
    modifiedParams.group = &modifiedGroup;
    modifiedParams.other = params->other;
    modifiedParams.space = params->space;

    originalPartitioner->run(receiver, &modifiedParams);
}

Laik_Partitioner create_checkpoint_partitioner(Laik_Partitioner* currentPartitioner) {
    Laik_Partitioner backupPartitioner;
    backupPartitioner.name = currentPartitioner->name;
    backupPartitioner.flags = currentPartitioner->flags;
    backupPartitioner.data = currentPartitioner;
    backupPartitioner.run = run_wrapped_partitioner;
    return backupPartitioner;
}
