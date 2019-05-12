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

Laik_Checkpoint laik_create_checkpoint(Laik_Instance *laikInstance, Laik_Space *space, Laik_Data *d);

#endif //LAIK_LAIK_FAULT_TOLERANCE_H
