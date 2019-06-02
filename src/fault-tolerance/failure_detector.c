//
// Created by Vincent Bode on 28/05/2019.
//

#include <laik-internal.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>


Laik_Space *nodeSpace;
Laik_Data *nodeData;
Laik_Partitioning *each;
Laik_Partitioning *all;

int laik_failure_check_nodes(Laik_Instance *laikInstance, Laik_Group *checkGroup, unsigned int (*failedNodes)[]) {
    int checkGroupSize = laik_size(checkGroup);
    if(nodeSpace == NULL) {
        nodeSpace = laik_new_space_1d(laikInstance, checkGroupSize);
        nodeData = laik_new_data(nodeSpace, laik_UChar);
        all = laik_new_partitioning(laik_All, checkGroup, nodeSpace, NULL);
        each = laik_new_partitioning(laik_new_block_partitioner1(), checkGroup, nodeSpace, NULL);
    }
    laik_switchto_partitioning(nodeData, each, LAIK_DF_None, LAIK_RO_None);
    unsigned char *nodeBase;
    uint64_t nodeCount;
    laik_map_def1(nodeData, (void **)&nodeBase, &nodeCount);
    assert(nodeCount == 1);
    *nodeBase = 1;

    laik_switchto_partitioning(nodeData, all, LAIK_DF_Preserve, LAIK_RO_None);
    laik_map_def1(nodeData, (void **)&nodeBase, &nodeCount);

    int failuresFound = 0;

    for (unsigned int i = 0; i < nodeCount; ++i) {
        if(nodeBase[i] != 1) {
            laik_log(LAIK_LL_Warning, "Node %i has abnormal status %d", i, nodeBase[i]);
            if(failedNodes != NULL) {
                (*failedNodes)[failuresFound] = i;
            }
            failuresFound++;
        } else {
            laik_log(LAIK_LL_Info, "Node %i has normal status %d", i, nodeBase[i]);
        }
    }

    return failuresFound;
}

