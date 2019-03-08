#include <laik.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define SIZE 8

/**
 * Very simple 1d finite element example
 *
 * Domain:
 *  chain of elements (e), with 2 nodes (n) as element boundaries:
 *   n0 - e0 - n1 - e1 - n2 - e2 - ... e7 - n8
 *
 * We use seperate data containers for elements and nodes, and derive
 * the node partitioning from the element partitioning
**/

// provide partitioning for nodes from partitioning of elements (<o>)
void runMyParter(Laik_SliceReceiver* r, Laik_PartitionerParams* p)
{
    // iterate over all element partitions used as basis
    for(int i = 0; i < laik_partitioning_slicecount(p->other); i++) {
        Laik_TaskSlice* ts = laik_partitioning_get_tslice(p->other, i);
        // does a private copy of original slice which can be modified
        Laik_Slice slc = *laik_taskslice_get_slice(ts);
        // extend 1d slice of elements by 1 at end for corresponding node partition
        slc.to.i[0]++;
        laik_append_slice(r, laik_taskslice_get_task(ts), &slc, 0, 0);
    }
}

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init(&argc, &argv);
    Laik_Group* myworld = laik_world(inst);

    // application defines the number of elements and nodes
    int size_nodes = (SIZE+1);
    int size_elems = SIZE;

    // 1d arrays for nodes
    Laik_Space* node_space = laik_new_space_1d(inst, size_nodes);
    Laik_Data* node = laik_new_data(node_space, laik_Double);

    // 1d arrays for elements
    Laik_Space* element_space = laik_new_space_1d(inst, size_elems);
    Laik_Data* element = laik_new_data(element_space, laik_Double);

    Laik_Partitioning *pNodes, *pElements;
    Laik_Partitioner *nodeParter;

    pElements = laik_new_partitioning(laik_new_block_partitioner1(),
                                      myworld, element_space, 0);
    nodeParter = laik_new_partitioner("myNodeParter", runMyParter, 0, 0);
    pNodes = laik_new_partitioning(nodeParter, myworld, node_space, pElements);

    double *ebase, *nbase;
    uint64_t ecount, ncount;

    // distribution of the elements
    laik_switchto_partitioning(element, pElements, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(element, (void**) &ebase, &ecount);

    // distribution of the nodes
    laik_switchto_partitioning(node, pNodes, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(node, (void**) &nbase, &ncount);

    // do something with elements and nodes...

    laik_finalize(inst);
    return 0;
}
