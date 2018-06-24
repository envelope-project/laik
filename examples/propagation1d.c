#include <laik.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "laik-internal.h"

#define SIZE 8

void runLuleshPartitioner1d(Laik_Partitioner* pr,
                            Laik_Partitioning* ba, Laik_Partitioning* otherBA)
{
    (void) pr; /* FIXME: Why have this parameter if it's never used */

    assert(ba->space->dims == otherBA->space->dims);
    Laik_Space* space = ba->space;
    //Laik_Group* g = ba->group;
    Laik_Slice slc = space->s;

    for(int i = 0; i < otherBA->count; i++) {
        slc.from.i[0] = otherBA->tslice[i].s.from.i[0];
        slc.to.i[0] = otherBA->tslice[i].s.to.i[0]+1;
        laik_append_slice(ba, otherBA->tslice[i].task, &slc, 0, 0);
    }

}

Laik_Partitioner* laik_new_lulesh_partitioner_1d()
{
    return laik_new_partitioner("lulesh1d", runLuleshPartitioner1d, 0, LAIK_PF_Merge);
}

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);

    #ifdef DBG
        if (laik_myid(world)==0)
        {
            int pause = 1;
            while (pause != 0);
        }
        
    #endif
    
    // application defines the number of elements and nodes
    int size_nodes = (SIZE+1);
    int size_elems = SIZE;

    // 1d arrays for nodes
    Laik_Space* node_space = laik_new_space_1d(inst, size_nodes);
    Laik_Data* node = laik_new_data(node_space, laik_Double);

    // 1d arrays for elements
    Laik_Space* element_space = laik_new_space_1d(inst, size_elems);
    Laik_Data* element = laik_new_data(element_space, laik_Double);

    Laik_AccessPhase *pNodes, *pElements;

    pElements = laik_new_accessphase(world, element_space,
                                   laik_new_block_partitioner1(), 0);
    pNodes = laik_new_accessphase(world, node_space,
                                   laik_new_lulesh_partitioner_1d(), pElements);

    double *base;
    uint64_t count;

    // distribution of the elements
    laik_switchto_phase(element, pElements, LAIK_DF_CopyOut, LAIK_RO_None);
    laik_map_def1(element, (void**) &base, &count);

    // distribution of the nodes
    laik_switchto_phase(node, pNodes, LAIK_DF_CopyOut, LAIK_RO_None);
    laik_map_def1(node, (void**) &base, &count);

    laik_finalize(inst);
    return 0;
}
