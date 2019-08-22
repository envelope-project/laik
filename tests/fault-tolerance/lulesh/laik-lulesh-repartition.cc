#include "lulesh.h"
#include "laik_partitioners.h"
#include <iostream>

void
Domain::re_distribute_data_structures(Laik_Group *new_group, Laik_Partitioning *p_exclusive, Laik_Partitioning *p_halo,
                                      Laik_Partitioning *p_overlapping, Laik_Transition *t_to_exclusive,
                                      Laik_Transition *t_to_halo, Laik_Transition *t_to_overlapping_init,
                                      Laik_Transition *t_to_overlapping_reduce) {

#ifdef REPARTITIONING
    m_x.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_y.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_z.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_xd.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_yd.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_zd.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_xdd.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_ydd.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_zdd.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
#endif
    m_fx.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_fy.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_fz.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
    m_nodalMass.migrate(new_group, p_overlapping, p_overlapping, t_to_overlapping_init, t_to_overlapping_reduce);
#ifdef REPARTITIONING
    m_dxx.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_dyy.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_dzz.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
#endif
    m_delv_xi.migrate(new_group, p_exclusive, p_halo, t_to_exclusive, t_to_halo);
    m_delv_eta.migrate(new_group, p_exclusive, p_halo, t_to_exclusive, t_to_halo);
    m_delv_zeta.migrate(new_group, p_exclusive, p_halo, t_to_exclusive, t_to_halo);
#ifdef REPARTITIONING
    m_delx_xi.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_delx_eta.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_delx_zeta.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_e.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_p.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_q.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_ql.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_qq.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_v.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_volo.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    //m_vnew.migrate(new_group, p_exclusive,nullptr, nullptr, nullptr);
    m_delv.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_vdov.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_arealg.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_ss.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);
    m_elemMass.migrate(new_group, p_exclusive, nullptr, nullptr, nullptr);

#endif
    this->world = new_group;
}

void Domain::createCheckpoints(std::vector<Laik_Checkpoint*> &checkpoints) {
#ifdef REPARTITIONING
    checkpoints.push_back(m_x.checkpoint());
    checkpoints.push_back(m_y.checkpoint());
    checkpoints.push_back(m_z.checkpoint());
    checkpoints.push_back(m_xd.checkpoint());
    checkpoints.push_back(m_yd.checkpoint());
    checkpoints.push_back(m_zd.checkpoint());
    checkpoints.push_back(m_xdd.checkpoint());
    checkpoints.push_back(m_ydd.checkpoint());
    checkpoints.push_back(m_zdd.checkpoint());
#endif
    checkpoints.push_back(m_fx.checkpoint());
    checkpoints.push_back(m_fy.checkpoint());
    checkpoints.push_back(m_fz.checkpoint());
    checkpoints.push_back(m_nodalMass.checkpoint());
#ifdef REPARTITIONING
    checkpoints.push_back(m_dxx.checkpoint());
    checkpoints.push_back(m_dyy.checkpoint());
    checkpoints.push_back(m_dzz.checkpoint());
#endif
    checkpoints.push_back(m_delv_xi.checkpoint());
    checkpoints.push_back(m_delv_eta.checkpoint());
    checkpoints.push_back(m_delv_zeta.checkpoint());
#ifdef REPARTITIONING
    checkpoints.push_back(m_delx_xi.checkpoint());
    checkpoints.push_back(m_delx_eta.checkpoint());
    checkpoints.push_back(m_delx_zeta.checkpoint());
    checkpoints.push_back(m_e.checkpoint());
    checkpoints.push_back(m_p.checkpoint());
    checkpoints.push_back(m_q.checkpoint());
    checkpoints.push_back(m_ql.checkpoint());
    checkpoints.push_back(m_qq.checkpoint());
    checkpoints.push_back(m_v.checkpoint());
    checkpoints.push_back(m_volo.checkpoint());
    checkpoints.push_back(m_delv.checkpoint());
    checkpoints.push_back(m_vdov.checkpoint());
    checkpoints.push_back(m_arealg.checkpoint());
    checkpoints.push_back(m_ss.checkpoint());
    checkpoints.push_back(m_elemMass.checkpoint());
#endif
}

int Domain::restore(std::vector<Laik_Checkpoint *> &checkpoints, Laik_Group *newGroup) {
    int index = 0;
#ifdef REPARTITIONING
    m_x.restore(checkpoints[index++], newGroup);
    m_y.restore(checkpoints[index++], newGroup);
    m_z.restore(checkpoints[index++], newGroup);
    m_xd.restore(checkpoints[index++], newGroup);
    m_yd.restore(checkpoints[index++], newGroup);
    std::cout << "Restored m_yd." << std::endl;
    m_zd.restore(checkpoints[index++], newGroup);
    std::cout << "Restored m_zd." << std::endl;
    m_xdd.restore(checkpoints[index++], newGroup);
    m_ydd.restore(checkpoints[index++], newGroup);
    m_zdd.restore(checkpoints[index++], newGroup);
#endif
    m_fx.restore(checkpoints[index++], newGroup);
    m_fy.restore(checkpoints[index++], newGroup);
    m_fz.restore(checkpoints[index++], newGroup);
    m_nodalMass.restore(checkpoints[index++], newGroup);
#ifdef REPARTITIONING
    m_dxx.restore(checkpoints[index++], newGroup);
    m_dyy.restore(checkpoints[index++], newGroup);
    m_dzz.restore(checkpoints[index++], newGroup);
#endif
    m_delv_xi.restore(checkpoints[index++], newGroup);
    m_delv_eta.restore(checkpoints[index++], newGroup);
    m_delv_zeta.restore(checkpoints[index++], newGroup);
#ifdef REPARTITIONING
    m_delx_xi.restore(checkpoints[index++], newGroup);
    m_delx_eta.restore(checkpoints[index++], newGroup);
    m_delx_zeta.restore(checkpoints[index++], newGroup);
    m_e.restore(checkpoints[index++], newGroup);
    m_p.restore(checkpoints[index++], newGroup);
    m_q.restore(checkpoints[index++], newGroup);
    m_ql.restore(checkpoints[index++], newGroup);
    m_qq.restore(checkpoints[index++], newGroup);
    m_v.restore(checkpoints[index++], newGroup);
    m_volo.restore(checkpoints[index++], newGroup);
    m_delv.restore(checkpoints[index++], newGroup);
    m_vdov.restore(checkpoints[index++], newGroup);
    m_arealg.restore(checkpoints[index++], newGroup);
    m_ss.restore(checkpoints[index++], newGroup);
    m_elemMass.restore(checkpoints[index++], newGroup);
#endif
    return index;
}

void init_config_params(Laik_Group *group, int &b, int &f, int &d, int &u, int &l, int &r) {
    int col, row, plane, side;
    InitMeshDecomp(laik_size(group), laik_myid(group), &col, &row, &plane, &side);

    b = 1;
    f = 1;
    d = 1;
    u = 1;
    l = 1;
    r = 1;

    if (col == 0) {
        l = 0;
    }

    if (col == side - 1) {
        r = 0;
    }

    if (row == 0) {
        d = 0;
    }

    if (row == side - 1) {
        u = 0;
    }

    if (plane == 0) {
        b = 0;
    }

    if (plane == side - 1) {
        f = 0;
    }

    //state = 0;
}

void create_partitionings_and_transitions(Laik_Group *&world,
                                          Laik_Space *&indexSpaceElements,
                                          Laik_Space *&indexSpaceNodes,
                                          Laik_Space *&indexSapceDt,
                                          Laik_Partitioning *&exclusivePartitioning,
                                          Laik_Partitioning *&haloPartitioning,
                                          Laik_Partitioning *&overlapingPartitioning,
                                          Laik_Partitioning *&allPartitioning,
                                          Laik_Transition *&transitionToExclusive,
                                          Laik_Transition *&transitionToHalo,
                                          Laik_Transition *&transitionToOverlappingInit,
                                          Laik_Transition *&transitionToOverlappingReduce) {
    int halo_depth = 1;                                                  // depth of halos used in partitioner algorithms

    // run partitioner algorithms to
    // calculate partitionings
    // precalculate the transition object
    // NOTE it is enough to calculate the
    // partitioning objects once for all data
    // structures at the beginning because
    // the data structures in lulesh use
    // similar data distribution and the
    // distribution does not change during
    // the iterations. Only for re-partitioning
    // they have to be recalculated
    exclusivePartitioning = laik_new_partitioning(exclusive_partitioner(), world, indexSpaceElements, 0);
    haloPartitioning = laik_new_partitioning(overlaping_partitioner(halo_depth), world, indexSpaceElements,
                                             exclusivePartitioning);
    overlapingPartitioning = laik_new_partitioning(overlaping_reduction_partitioner(halo_depth),
                                                   world, indexSpaceNodes, 0);
    // create all partitioning for dt to perform reductions
    allPartitioning = laik_new_partitioning(laik_All, world, indexSapceDt, 0);


    // precalculate the transition object
    // NOTE it is enough to calculate the
    // transition objects once for all data
    // structures at the beginning because
    // the data structures, because the
    // source and target partitionings
    // do not change during the iterations
    // unless we have re-partitioning
    transitionToExclusive = laik_calc_transition(indexSpaceElements,
                                                 haloPartitioning,
                                                 exclusivePartitioning, LAIK_DF_None, LAIK_RO_None);
    transitionToHalo = laik_calc_transition(indexSpaceElements,
                                            exclusivePartitioning,
                                            haloPartitioning, LAIK_DF_Preserve, LAIK_RO_None);
    transitionToOverlappingInit = laik_calc_transition(indexSpaceNodes,
                                                       overlapingPartitioning, overlapingPartitioning,
                                                       LAIK_DF_Init, LAIK_RO_Sum);
    transitionToOverlappingReduce = laik_calc_transition(indexSpaceNodes,
                                                         overlapingPartitioning, overlapingPartitioning,
                                                         LAIK_DF_Preserve, LAIK_RO_Sum);
}

void remove_partitionings_and_transitions(Laik_Partitioning *&exclusivePartitioning,
                                          Laik_Partitioning *&haloPartitioning,
                                          Laik_Partitioning *&overlapingPartitioning,
                                          Laik_Partitioning *&allPartitioning,
                                          Laik_Transition *&transitionToExclusive,
                                          Laik_Transition *&transitionToHalo,
                                          Laik_Transition *&transitionToOverlappingInit,
                                          Laik_Transition *&transitionToOverlappingReduce) {
    laik_free_partitioning(exclusivePartitioning);
    laik_free_partitioning(haloPartitioning);
    laik_free_partitioning(overlapingPartitioning);
    laik_free_partitioning(allPartitioning);
    free(transitionToExclusive);
    free(transitionToHalo);
    free(transitionToOverlappingInit);
    free(transitionToOverlappingReduce);
}

void calculate_removing_list(Laik_Group *world, cmdLineOpts &opts, double side, double &newside, int &diffsize,
                             int *&removeList) {
    int cursize = laik_size(world);
    newside = cbrt(opts.repart);
    if (newside - ((int) floor(newside + 0.1)) != 0) {
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    diffsize = cursize - opts.repart;

    removeList = (int *) malloc(diffsize * sizeof(int));
    for (int i = 0; i < diffsize; i++) {
        removeList[i] = i + opts.repart;
    }

    // check if the repartitioning scenario is valid (the target process group is a cubic int and
    // the total size of elements in domain still valid)
    double new_nx = (double) opts.nx * (double) side / (double) newside;
    double verifier;

    if (modf(new_nx, &verifier) != 0.0) {
        std::cout << "Repartitioning is not allowed for inbalanced domains after repartitioning. \n";
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
}

void calculate_removing_list_ft(Laik_Group *world, cmdLineOpts &opts, double side, double &newside, int &diffsize,
                                int *&removeList, int *nodeStatuses) {
    int cursize = laik_size(world);
    newside = cbrt(opts.repart);
    if (newside - ((int) floor(newside + 0.1)) != 0) {
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
    diffsize = cursize - opts.repart;

    removeList = (int *) malloc(diffsize * sizeof(int));
    int nodeStatusIndex = 0;
    int i = 0;
    while (i < diffsize) {
        while (nodeStatusIndex < laik_size(world) && nodeStatuses[nodeStatusIndex] != LAIK_FT_NODE_FAULT) {
            nodeStatusIndex++;
        }
        if (nodeStatusIndex < laik_size(world)) {
            removeList[i] = nodeStatusIndex;
            nodeStatusIndex++;
            i++;
        } else {
            removeList[i] = i + opts.repart;
            i++;
        }
    }

    std::cout << "Remove list:";
    for (i = 0; i < diffsize; i++) {
        std::cout << " " << removeList[i];
    }
    std::cout << std::endl;

    // check if the repartitioning scenario is valid (the target process group is a cubic int and
    // the total size of elements in domain still valid)
    double new_nx = (double) opts.nx * (double) side / (double) newside;
    double verifier;

    if (modf(new_nx, &verifier) != 0.0) {
        std::cout << "Repartitioning is not allowed for inbalanced domains after repartitioning. \n";
        MPI_Abort(MPI_COMM_WORLD, -1);
    }
}
