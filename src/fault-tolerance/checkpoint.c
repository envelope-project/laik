//
// Created by Vincent Bode on 09/05/2019.
//

#include <laik-internal.h>
#include <assert.h>
#include <string.h>

Laik_Checkpoint laik_create_checkpoint(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *data) {
    int iteration = laik_get_iteration(laikInstance);
    laik_log(LAIK_LL_Info, "Checkpoint requested at iteration %i\n", iteration);

    Laik_Checkpoint checkpoint;

    Laik_Partitioning *backupPartitioning;

    switch (space->dims) {
        case 1:
            checkpoint.space = laik_new_space_1d(laikInstance, laik_space_size(space) * data->elemsize);
            break;
//    case 2:
//        backupSpace = laik_new_space_2d(laikInstance, laik_space_size(space));
//        break;
//    case 3:
//        backupSpace = laik_new_space_3d(laikInstance, laik_space_size(space));
//        break;
        default:
            laik_log(LAIK_LL_Error, "Unknown space dimensionality during checkpointing: %i\nAborting checkpoint\n",
                     space->dims);
    }
    laik_set_space_name(checkpoint.space, "checkpoint");


    checkpoint.data = laik_new_data(checkpoint.space, laik_UChar);

    backupPartitioning = laik_new_partitioning(laik_All, laik_world(laikInstance), checkpoint.space, NULL);

    laik_switchto_partitioning(checkpoint.data, backupPartitioning, LAIK_DF_None, LAIK_RO_None);

    void *backupBase;
    uint64_t backupCount;

    //Copy over the memory
    laik_map_def1(checkpoint.data, &backupBase, &backupCount);

    assert(data->activeMappings->count == 1);
    Laik_Mapping activeMapping = data->activeMappings->map[0];
    void *base = activeMapping.base;
    uint64_t count = activeMapping.count;

    assert(count * data->elemsize == backupCount);

    memcpy(backupBase, base, backupCount);

    laik_log(LAIK_LL_Info, "Checkpoint %s completed\n", checkpoint.space->name);
    return checkpoint;
}
