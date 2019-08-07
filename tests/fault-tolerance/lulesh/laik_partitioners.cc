#include "laik_partitioners.h"
#include "lulesh.h"

/** 
 * @brief  Exclusiv Partitioner
 * @note   For ELEMENTs Data structures.
 * @param  pr: 
 * @param  p: 
 * @param  oldP: 
 * @retval None
 */
void runExclusivePartitioner(Laik_SliceReceiver* rcv, Laik_PartitionerParams* par)
{
    int numRanks = laik_size(par->group);
    int myRank = laik_myid(par->group);
    int col, row, plane, side;
    InitMeshDecomp(numRanks, myRank, &col, &row, &plane, &side);

    // get the size of the
    const Laik_Slice* slice = laik_space_asslice(par->space);
    int edgeElems= (int) (cbrt( (slice->to.i[0]+1) / numRanks ) + 0.1 );

    int Nx=edgeElems;
    int Ny=edgeElems;
    int Nz=edgeElems;
    int Rx= side;
    int Ry= side;
    int Rz= side;
    int Lx = Rx*Nx;
    int Ly = Ry*Ny;
    //int Lz = Rz*Nz;
    int Pxy= Lx*Ly;
    //int Pxz= Lx*Lz;
    //int Pyz= Ly*Lz;

    // sine all the tasks run the same partitioning algorithm
    // we should loop over all the tasks and not just this
    // task
    Laik_Slice slc;
    uint64_t from, to;
    int r=0;
    int nx=0;
    int tag=0;
    for (int rz = 0; rz < Rz; rz++)
    {
        for (int ry = 0; ry < Ry; ry++)
        {
            for (int rx = 0; rx < Rx; rx++)
            {
                r = rx + ry*Rx + rz*Rx*Ry; // task number
                // loop over z and x  to create the slices in the
                // partitioning
                for (int ny = 0; ny < Ny; ny++)
                {
                    for (int nz = 0; nz < Nz; nz++)
                    {
                        // tag = global index where nx = 0 + safety shift = Ny+10
                        nx=0;
                        tag = nx + Lx*ny + Pxy*nz +
                                rx*Nx + ry*Lx*Ny + Pxy*Nz*rz + Ny*10;
                        nx=0;
                        from = nx + Lx*ny + Pxy*nz +
                                rx*Nx + ry*Lx*Ny + Pxy*Nz*rz;
                        nx=Nx;
                        to = nx + Lx*ny + Pxy*nz +
                                rx*Nx + ry*Lx*Ny + Pxy*Nz*rz;

                        laik_slice_init_1d(&slc, par->space, from, to);
                        laik_append_slice(rcv, r, &slc, tag, 0);
                    }
                }
            }
        }
    }

}


Laik_Partitioner* exclusive_partitioner()
{
    return laik_new_partitioner("exclusive", runExclusivePartitioner, 0,
                                LAIK_PF_Merge);
}

/** 
 * @brief  Halo Partitioner for Elements
 * @note   Overlaping layouts for the elements data structure. 
 * @param  pr: 
 * @param  p: 
 * @param  oldP: 
 * @retval None
 */
void runOverlapingPartitioner(Laik_SliceReceiver* rcv, Laik_PartitionerParams* par)
{
    int numRanks = laik_size(par->group);
    int myRank = laik_myid(par->group);
    int col, row, plane, side;
    InitMeshDecomp(numRanks, myRank, &col, &row, &plane, &side);

    // get the size of the
    const Laik_Slice* slice = laik_space_asslice(par->space);
    int edgeElems= (int) ( cbrt( (slice->to.i[0]+1) / numRanks) + 0.1 );

    // the number of halos in each boundary
    int d = *(int*) laik_partitioner_data(par->partitioner);

    if (d>edgeElems)
    {
        laik_log (LAIK_LL_Error, "number of halo is too large!"
                                 " fix your application");
        exit(0);
    }

    int Nx=edgeElems;
    int Ny=edgeElems;
    int Nz=edgeElems;
    int Rx= side;
    int Ry= side;
    int Rz= side;
    int Lx = Rx*Nx;
    int Ly = Ry*Ny;
    //int Lz = Rz*Nz;
    int Pxy= Lx*Ly;
    //int Pxz= Lx*Lz;
    //int Pyz= Ly*Lz;

    // sine all the tasks run the same partitioning algorithm
    // we should loop over all the tasks and not just this
    // task
    Laik_Slice slc;
    uint64_t from, to;
    int r=0;
    int nx=0;
    int tag;
    for (int rz = 0; rz < Rz; rz++)
    {
        for (int ry = 0; ry < Ry; ry++)
        {
            for (int rx = 0; rx < Rx; rx++)
            {
                r = rx + ry*Rx + rz*Rx*Ry; // task number
                // loop over y and z  to create the slices in the
                // partitioning
                for (int ny = ((ry==0) ?0:-d); ny < ((ry==Ry-1)?Ny:Ny+d) ; ny++)
                {
                    for (int nz = ( (rz==0)?0:-d ) ;
                         nz < ( (rz==Rz-1)?Nz:Nz+d ) ; nz++)
                    {
                        // tag = global index where nx = 0 + safety shift = Ny+10
                        nx=0;
                        tag = nx + Lx*ny + Pxy*nz +
                                rx*Nx + ry*Lx*Ny + Pxy*Nz*rz  + Ny*10;
                        nx= (rx==0)?0:-d;
                        from = nx + Lx*ny + Pxy*nz +
                                rx*Nx + ry*Lx*Ny + Pxy*Nz*rz;
                        nx= (rx==Rx-1)?Nx:Nx+d;
                        to = nx + Lx*ny + Pxy*nz +
                                rx*Nx + ry*Lx*Ny + Pxy*Nz*rz;

                        laik_slice_init_1d(&slc, par->space, from, to);
                        laik_append_slice(rcv, r, &slc, tag, 0);
                        //laik_log((Laik_LogLevel)2,"tag: %d\n",tag);
                    }
                }
            }
        }
    }

}

/** 
 * @brief  Partitioner for Nodes (Reduction Partitioner)
 * @note   A Halo Liked Reduction Partitioner for partitiong the
 *         Nodes Datastructure.
 * @param  &depth: Depth of the Halo
 * @retval 
 */
Laik_Partitioner* overlaping_partitioner(int &depth)
{
    //void* data = (void*) &depth;
    return laik_new_partitioner("halo", runOverlapingPartitioner,
                                (void*) &depth, LAIK_PF_Merge);
}

void runOverlapingReductionPartitioner(Laik_SliceReceiver* rcv, Laik_PartitionerParams* par)
{
    int numRanks = laik_size(par->group);
    int myRank = laik_myid(par->group);
    int col, row, plane, side;
    InitMeshDecomp(numRanks, myRank, &col, &row, &plane, &side);

    // get the size of the
    const Laik_Slice* slice = laik_space_asslice(par->space);
    int edgeNodes= (int) ( (cbrt( (slice->to.i[0]+1)) -1)/
            cbrt(numRanks) + 1   + 0.1);

    //laik_log ((Laik_LogLevel)2, "elems: %d", edgeNodes);

    // the number of halos in each boundary
    int d = *(int*) laik_partitioner_data(par->partitioner);

    if (d>edgeNodes)
    {
        laik_log (LAIK_LL_Error, "number of halo is too large!"
                                 " fix your application");
        exit(0);
    }

    int Nx=edgeNodes;
    int Ny=edgeNodes;
    int Nz=edgeNodes;
    int Rx= side;
    int Ry= side;
    int Rz= side;
    int Lx = Rx*(Nx-1)+1;
    int Ly = Ry*(Ny-1)+1;
    int Pxy= Lx*Ly;


    // sine all the tasks run the same partitioning algorithm
    // we should loop over all the tasks and not just this
    // task
    Laik_Slice slc;
    uint64_t from, to;
    int r=0;
    int nx=0;
    int tag=0;
    for (int rz = 0; rz < Rz; rz++)
    {
        for (int ry = 0; ry < Ry; ry++)
        {
            for (int rx = 0; rx < Rx; rx++)
            {
                //r = ry + rx*Ry + rz*Rx*Ry; // (yxz)
                r = rx + ry*Rx + rz*Rx*Ry; // (xyz)
                //r = rx + rz*Rx + ry*Rx*Rz; // (xzy)
                //r = ry + rz*Ry + rx*Rz*Ry; // (yzx)
                //r = rz + ry*Rz + rx*Rz*Ry; // (zyx)
                //r = rz + rx*Rz + ry*Rx*Rz; // (zxy)

                // loop over y and z to create the slices in the
                // partitioning
                for (int ny = 0 ; ny < Ny; ny++)
                {
                    for (int nz = 0 ; nz < Nz; nz++)
                    {
                        nx=0;
                        // unique tags
                        tag = nx + Lx*ny + Pxy*nz +  rx*(Nx-1) + ry*Lx*(Ny-1) + rz*Pxy*(Nz-1) + Ny*100;
                        nx=0;
                        from = nx + Lx*ny + Pxy*nz +  rx*(Nx-1) + ry*Lx*(Ny-1) + rz*Pxy*(Nz-1);
                        nx=Nx;
                        to = nx + Lx*ny + Pxy*nz +  rx*(Nx-1) + ry*Lx*(Ny-1) + rz*Pxy*(Nz-1);
                        laik_slice_init_1d(&slc, par->space, from, to);
                        laik_append_slice(rcv, r, &slc, tag, 0);
                    }
                }
            }
        }
    }

}

Laik_Partitioner* overlaping_reduction_partitioner(int &depth)
{
    //void* data = (void*) &depth;
    return laik_new_partitioner("overlapingReduction",
                                runOverlapingReductionPartitioner,
                                (void*) &depth, LAIK_PF_Merge);
}
