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

    Laik_Partitioning *backupPartitioning;
    backupPartitioning = laik_new_partitioning(laik_All, laik_world(laikInstance), (*checkpoint).space, NULL);

    laik_switchto_partitioning((*checkpoint).data, backupPartitioning, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1((*checkpoint).data, backupBase, backupCount);

    assert(data->activeMappings->count == 1);
    Laik_Mapping activeMapping = data->activeMappings->map[0];
    *base = activeMapping.base;
    *count = activeMapping.count;

    assert(*count * data->elemsize == *backupCount);
}

Laik_Checkpoint initCheckpoint(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, const Laik_Space *space,
                               const Laik_Data *data) {
    (*checkpoint).space = laik_new_space_1d(laikInstance, laik_space_size(space) * data->elemsize);
    laik_set_space_name((*checkpoint).space, "checkpoint");

    (*checkpoint).data = laik_new_data((*checkpoint).space, laik_UChar);
    return (*checkpoint);
}
