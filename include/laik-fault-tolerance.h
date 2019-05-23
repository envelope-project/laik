//
// Created by Vincent Bode on 11/05/2019.
//

#ifndef LAIK_LAIK_FAULT_TOLERANCE_H
#define LAIK_LAIK_FAULT_TOLERANCE_H

struct _Laik_Checkpoint {
    Laik_Space* space;
    Laik_Data* data;
};

typedef struct _Laik_Checkpoint Laik_Checkpoint;

Laik_Checkpoint laik_checkpoint_create(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *data,
                                       Laik_Partitioner *backupPartitioner);

void laik_checkpoint_restore(Laik_Instance *laikInstance, Laik_Checkpoint *checkpoint, Laik_Space *space, Laik_Data *data);

#endif //LAIK_LAIK_FAULT_TOLERANCE_H
