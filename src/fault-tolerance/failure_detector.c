//
// Created by Vincent Bode on 28/05/2019.
//

#include <laik-internal.h>
#include <laik-backend-tcp.h>
#include "glib.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>


Laik_Space *nodeSpace;
Laik_Data *nodeData;
Laik_Partitioning *each;
Laik_Partitioning *all;

void laik_set_fault_tolerant_world(Laik_Group *group);

int laik_failure_check_nodes(Laik_Instance *laikInstance, Laik_Group *checkGroup, int *failedNodes) {
    int checkGroupSize = laik_size(checkGroup);
    if (nodeSpace == NULL || all->group != checkGroup) {
        laik_log(LAIK_LL_Debug, "Resetting failure check container.");
        if (nodeSpace != NULL) {
            laik_free_space(nodeSpace);
            laik_free(nodeData);
            laik_free_partitioning(all);
            laik_free_partitioning(each);
        }
        nodeSpace = laik_new_space_1d(laikInstance, checkGroupSize);
        laik_set_space_name(nodeSpace, "Failure detection space");
        nodeData = laik_new_data(nodeSpace, laik_UChar);
        laik_data_set_name(nodeData, "Failure detection data container");
        all = laik_new_partitioning(laik_All, checkGroup, nodeSpace, NULL);
        each = laik_new_partitioning(laik_new_block_partitioner1(), checkGroup, nodeSpace, NULL);
    }
    laik_switchto_partitioning(nodeData, each, LAIK_DF_None, LAIK_RO_None);
    unsigned char *nodeBase;
    uint64_t nodeCount;
    laik_map_def1(nodeData, (void **) &nodeBase, &nodeCount);
    assert(nodeCount == 1);
    *nodeBase = LAIK_FT_NODE_OK;

    laik_switchto_partitioning(nodeData, all, LAIK_DF_Preserve, LAIK_RO_None);
    laik_map_def1(nodeData, (void **) &nodeBase, &nodeCount);

    int failuresFound = 0;

    for (unsigned int i = 0; i < nodeCount; ++i) {
        if (nodeBase[i] != LAIK_FT_NODE_OK) {
            laik_log(LAIK_LL_Warning, "Node %i (global %i) has abnormal status %d", i,
                     laik_location_get_world_offset(checkGroup, i), nodeBase[i]);
            if (failedNodes != NULL) {
                failedNodes[i] = LAIK_FT_NODE_FAULT;
            }
            failuresFound++;
        } else {
            laik_log(LAIK_LL_Warning, "Node %i (global %i) has normal status %d", i,
                     laik_location_get_world_offset(checkGroup, i), nodeBase[i]);
            if (failedNodes != NULL) {
                failedNodes[i] = LAIK_FT_NODE_OK;
            }
        }

        //Clear the value to make sure it isn't accidentally reused
        nodeBase[i] = LAIK_FT_NODE_FAULT;
    }

    return failuresFound;
}

int laik_failure_eliminate_nodes(Laik_Instance *instance, int count, int *nodeStatuses) {
    (void) instance;
    (void) count;
    (void) nodeStatuses;
//    Laik_Group* world = laik_world(instance);
//    laik_log(LAIK_LL_Info, "Attempting to eliminate %d failed nodes (world size from %d to %d)", count, world->size, world->size - count);
//    world->size -= count;
//    instance->size -= count;
//    int shiftCounter = 0;
//
//    for(int i = 0; i < count; i++) {
//        int nodeToRemove = (*nodesToRemove)[i];
//
//        assert(nodeToRemove != world->myid);
//        if(nodeToRemove < world->myid) {
//            shiftCounter++;
//        }
//    }
//
//    world->myid -= shiftCounter;
//
//    // Only TCP backend supported for now
//    //Fixme: Its strcmp
//    assert(strcmp(instance->backend->name, "TCP Backend") == 0);
//    unsigned char* backendData = (unsigned char*)world->backend_data;
//    TCPGroupData* backendGroupData = tcpGroupData(world);
//    MPI_Comm communicator = backendGroupData->comm;
//    GArray tasks = communicator->tasks;
//
//    //Fixme: Its a hack
//    //Adjust the mappings into the backend ranks
////    GArray* tasks = (GArray*)backendData;
//    shiftCounter = 0;
//    unsigned int inCounter = 0;
//    for(int i = 0; i < world-> size + count; i++) {
//        g_array_index(tasks, size_t, i - shiftCounter) = i;
//        laik_log(LAIK_LL_Info, "TCP mapping rank %d now points to %d", i - shiftCounter, i);
//        if((*nodesToRemove)[inCounter] == (unsigned int)i) {
//            shiftCounter++;
//            inCounter++;
//        }
//    }
//
//    backendData += sizeof(GArray*);
//    // Backend data now points to our own rank in the backend
//    *((size_t *)backendData) = world->myid;
//
//    laik_log(LAIK_LL_Info, "Changed own rank to %d", world->myid);

    Laik_Group *world = laik_world(instance);

    Laik_Group *newGroup = laik_clone_group(world);

    int newRank = 0;
    for (int worldRank = 0; worldRank < world->size; ++worldRank) {
        if (nodeStatuses[worldRank] != LAIK_FT_NODE_OK) {
            newGroup->fromParent[worldRank] = -1;
        } else {
            newGroup->fromParent[worldRank] = newRank;
            newGroup->toParent[newRank] = worldRank;

            if (worldRank == world->myid) {
                newGroup->myid = newRank;
            }

            newRank++;
        }
    }
    newGroup->size = newRank;

    instance->backend->eliminateNodes(world, newGroup, nodeStatuses);

    laik_log(LAIK_LL_Warning, "New world size: %i", newGroup->size);
    laik_set_fault_tolerant_world(newGroup);
    return 0;
}

int current_world_index = 0;

void laik_set_fault_tolerant_world(Laik_Group *group) {
    current_world_index = group->gid;
}

//
////TODO: Store current world index for each instance;
Laik_Group *laik_world_fault_tolerant(Laik_Instance *instance) {
    return instance->group[current_world_index];
}
