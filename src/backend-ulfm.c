//
// Created by Vincent Bode on 13/12/2019.
//

#include <assert.h>
#include <backends/tcp/mpi.h>
#include <mpi.h>
#include <mpi-ext.h>
#include <laik.h>
#include <laik-internal.h>
#include <laik-backend-mpi-internal.h>

// These are additional methods part of ULFM
static void laik_mpi_eliminate_nodes(struct _Laik_Group * oldGroup, struct _Laik_Group * newGroup, int* nodeStatuses);
static int laik_mpi_status_check(struct _Laik_Group *group, int *nodeStatuses);

static Laik_Backend laik_backend_ulfm = {
        .name        = "ULFM (MPI two-sided)",
        .finalize    = laik_mpi_finalize,
        .prepare     = laik_mpi_prepare,
        .cleanup     = laik_mpi_cleanup,
        .exec        = laik_mpi_exec,
        .updateGroup = laik_mpi_updateGroup,
        .log_action  = laik_mpi_log_action,
        .sync        = laik_mpi_sync,
        .eliminateNodes = laik_mpi_eliminate_nodes,
        .statusCheck = laik_mpi_status_check,
};

Laik_Instance* laik_init_mpi(int* argc, char*** argv) {
    return laik_init_mpi_generic_backend(argc, argv, &laik_backend_ulfm);
}

static void laik_mpi_eliminate_nodes(Laik_Group* oldGroup, Laik_Group* newGroup, int* nodeStatuses) {
    (void) oldGroup; (void)newGroup; (void)nodeStatuses;

#ifdef USE_ULFM

    int err;
    MPI_Comm oldComm = ((MPIGroupData *) oldGroup->backend_data)->comm;

    // We still need the old communicator to recover the checkpoints, don't invalidate it just yet.

    MPIGroupData* gd = (MPIGroupData*) newGroup->backend_data;
    assert(gd == 0); // must not be updated yet
    gd = malloc(sizeof(MPIGroupData));
    if (!gd) {
        laik_panic("Out of memory allocating MPIGroupData object");
        exit(1); // not actually needed, laik_panic never returns
    }
    newGroup->backend_data = gd;

    // Was this assertion wrong?
//    assert(oldComm != NULL && gd->comm != NULL);
    assert(oldComm != NULL);
    err = MPIX_Comm_shrink(oldComm, &gd->comm);
    if(err != MPI_SUCCESS) {
        laik_mpi_panic(err);
    }
    assert(gd->comm != NULL);

    int newSize;
    MPI_Comm_size(gd->comm, &newSize);
    if(newSize != newGroup->size) {
        laik_log(LAIK_LL_Panic, "The size of the new mpi group (%i) is different to the new group size (%i).", newSize, newGroup->size);
        assert(0);
    }
#else
    laik_log(LAIK_LL_Panic, "Application tried to eliminate nodes but no fault tolerance capability was built.");
#endif
}

static int laik_mpi_status_check(Laik_Group *group, int *nodeStatuses) {

#ifdef USE_ULFM

    laik_log(LAIK_LL_Debug, "Starting agreement protocol\n");


    MPI_Comm originalComm = ((MPIGroupData *) group->backend_data)->comm;

    int result;
    int reduceFlag = 1;
    do {
        MPIX_Comm_failure_ack(originalComm);
        result = MPIX_Comm_agree(originalComm, &reduceFlag);
    } while (result != MPI_SUCCESS);

    MPI_Group failedGroup, checkGroup;
    MPIX_Comm_failure_get_acked(originalComm, &failedGroup);

    int n = -1;
    MPI_Group_size(failedGroup, &n);
    int ranks[n];
    int checkGroupRanks[n];

    MPI_Comm_group(originalComm, &checkGroup);

    laik_log(LAIK_LL_Debug, "Failed MPI_Group size is %i", n);

    for (int i = 0; i < n; ++i) {
        ranks[i] = i;
    }

    // WARNING: Different from old implementation, this group contains only failed ones, not the survivors
    MPI_Group_translate_ranks(failedGroup, n, ranks, checkGroup, checkGroupRanks);
    if (nodeStatuses != NULL) {
        for (int i = 0; i < group->size; ++i) {
            nodeStatuses[i] = LAIK_FT_NODE_OK;
        }

        for (int i = 0; i < n; ++i) {
            laik_log(LAIK_LL_Debug, "Failed node %i translated to check group rank %i.", i, checkGroupRanks[i]);
            nodeStatuses[checkGroupRanks[i]] = LAIK_FT_NODE_FAULT;
        }
    }

    MPI_Group_free(&failedGroup);
    MPI_Group_free(&checkGroup);
    return n;
#else
    laik_log(LAIK_LL_Warning, "Application tried to perform a status check but no fault tolerance capability was built. Will assume that all nodes are reachable.");
    for (int i = 0; i < laik_size(group); ++i) {
        nodeStatuses[i] = LAIK_FT_NODE_OK;
    }
#endif
}
