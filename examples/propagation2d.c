#ifdef USE_MPI
#include "laik-backend-mpi.h"
#else
#include "laik-backend-single.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

#include "laik.h"

// application specific method to calculate border nodes for an element
// for 2d lulesh, each element has 4 neighbour nodes
// every 4 consecutive nodes belong to one elements
// eg 4x4:
// neighbours of 0 -> 0,1,5,6.
// then comes the neighbours of the next element and so on.

void calculate_task_topology(int numRanks, int* Rx, int* Ry){
    *Ry = (int) sqrt(numRanks);
    while ( (*Ry) > 0) {
        if (numRanks%(*Ry) == 0)
            break;
        (*Ry)--;
    }
    *Rx = numRanks/(*Ry);
    assert ( (*Rx)*(*Ry) == numRanks );
}

void calculate_my_coordinate(int numRanks, int rank, int* rx, int* ry){
    int Rx;
    int Ry;
    calculate_task_topology(numRanks, &Rx, &Ry);
    *rx = rank%Rx;
    *ry = rank/Rx;
}

//partitioner handler for the elements
void runLuleshElementPartitioner(Laik_Partitioner* pr,
                                 Laik_BorderArray* ba, Laik_BorderArray* otherBA)
{
    (void) otherBA; // required due to interface signature

    Laik_Space* space = laik_borderarray_getspace(ba);
    const Laik_Slice* slice = laik_space_getslice(space);
    Laik_Group* group = laik_borderarray_getgroup(ba);
    int N_local_x = *((int*) laik_partitioner_data(pr));
    int N_local_y = N_local_x; // only squre subdomain are supported!
    int N_tasks_x;
    int N_tasks_y;
    calculate_task_topology(laik_size(group), &N_tasks_x, &N_tasks_y);
    int N_elems_x = N_local_x * N_tasks_x ;

    Laik_Slice slc = *slice;

    for(int ix = 0; ix < N_tasks_x; ix++) {
        for(int iy = 0; iy < N_tasks_y; iy++) {
            for(int jy = 0; jy < N_local_x; jy++) {
                slc.from.i[0] = ix * N_local_x + iy * N_local_y * N_elems_x +
                                jy* N_elems_x + 0;
                slc.to.i[0] = ix * N_local_x + iy * N_local_y * N_elems_x +
                              jy* N_elems_x + N_local_x;

                //laik_log(1, "from: %d, to: %d, task: %d \n", slc.from.i[0], slc.to.i[0], ix+iy*N_tasks_x);
                laik_append_slice(ba, ix+iy*N_tasks_x, &slc, 0, 0);
            }
        }
    }
}

Laik_Partitioner* laik_new_lulesh_element_partitioner(int *size)
{
    return laik_new_partitioner("element", runLuleshElementPartitioner, (void*) size, LAIK_PF_Merge);
}

//partitioner handler for the nodes
void runLuleshNodePartitioner(Laik_Partitioner* pr,
                                Laik_BorderArray* ba, Laik_BorderArray* otherBA)
{
    Laik_Space* space = laik_borderarray_getspace(ba);
    Laik_Space* otherSpace = laik_borderarray_getspace(otherBA);
    const Laik_Slice* slice = laik_space_getslice(space);
    Laik_Group* group = laik_borderarray_getgroup(ba);


    assert(laik_space_getdimensions(space) == laik_space_getdimensions(otherSpace));
    //assert (otherSpace->neighbours != 0);

    Laik_Slice slc = *slice;
    int* neighbours = (int*) laik_partitioner_data(pr);
    int Rx,Ry,rx,ry;
    calculate_task_topology(laik_size(group), &Rx, &Ry);

    // for all the slices in the element partitioner
    // we find the neighbouring nodes and add a slice
    // to the new partioning
    int sliccountE = laik_borderarray_getcount(otherBA);
    for(int i = 0; i < sliccountE; i++) {
        Laik_TaskSlice* ts = laik_borderarray_get_tslice(otherBA, i);
        const Laik_Slice* s = laik_taskslice_getslice(ts);
        int task = laik_taskslice_gettask(ts);
        calculate_my_coordinate(laik_size(group), task, &rx, &ry);
        slc.from.i[0] = neighbours[ 4 * s->from.i[0] + 0 ] + 0;
        slc.to.i[0] = neighbours [ 4 * (s->to.i[0] - 1) + 1 ] + 1;
        laik_append_slice(ba, task, &slc, 0, 0);

        slc.from.i[0] = neighbours[ 4 * s->from.i[0] + 2] + 0;
        slc.to.i[0] = neighbours [ 4 * (s->to.i[0] - 1) + 3 ] + 1;
        laik_append_slice(ba, task, &slc, 0, 0);
    }
}

Laik_Partitioner* laik_new_lulesh_node_partitioner(int* neighbours)
{
    return laik_new_partitioner("node", runLuleshNodePartitioner, neighbours, LAIK_PF_Merge);
}


// create a global list of neighbors of the elements
int* build_element_neighbour_list(int Lx, int Ly){

    int num_nodes=Lx*Ly;

    //allocate memory for the integers
    int* neighbours = (int*) malloc (sizeof(int)*num_nodes*4);

    int jy=0;
    for (int node = 0; node < num_nodes; node++)
    {
        jy = node/Lx;
        neighbours[4*node + 0] =  node + jy;
        neighbours[4*node + 1] =  node + jy + 1;
        neighbours[4*node + 2] =  node + jy + Lx + 1;
        neighbours[4*node + 3] =  node + jy + Lx + 2;
    }

    return neighbours;
}

void free_neighbour_list(int* neighbour){
    free(neighbour);
}

// fuction to refere to the neighburs of the elements
int get_element_neighbour(int* neighbours, int element, int node_index)
{
    return neighbours[4*element+node_index];
}

// for the debug
void print_data(Laik_Data* d, Laik_AccessPhase *p)
{
    double *base;
    uint64_t count;
    int nSlices = laik_phase_my_slicecount(p);
    for (int s = 0; s < nSlices; s++) {
        laik_map_def(d, s, (void**) &base, &count);
        for (uint64_t i = 0; i < count; i++)
        {
            laik_log(1,"%f\n", base[i]);
        }
        laik_log(1,"\n");
    }
}

// for testing
double data_check_sum(Laik_Data* d, Laik_AccessPhase *p, Laik_Group* world) {
    double *base;
    uint64_t count;
    int nSlices = laik_phase_my_slicecount(p);
    double sum=0.0;
    for(int s = 0; s < nSlices; s++) {
        laik_map_def(d, s, (void**) &base, &count);
        for (uint64_t i = 0; i < count; i++)
        {
            sum+=base[i];
        }
    }

    Laik_Data* laik_sum = laik_new_data_1d(world, laik_Double, 1);
    laik_switchto_new(laik_sum, laik_All, LAIK_DF_ReduceOut | LAIK_DF_Sum);
    laik_map_def1(laik_sum, (void**) &base, &count);
    *base=sum;
    laik_switchto_new(laik_sum, laik_All, LAIK_DF_CopyIn);
    laik_map_def1(laik_sum, (void**) &base, &count);

    return *base;
}

void apply_boundary_condition(Laik_Data* data, Laik_AccessPhase* pr , int Rx,  int Ry, int rx, int ry, double value)
{
    double *baseN;
    uint64_t countN, i;
    int nSlices=laik_phase_my_slicecount(pr);
    int n;

    if (rx==0){
        i=0;
        for (n = 0; n < nSlices; ++n) {
            laik_map_def(data, n, (void**) &baseN, &countN);
            baseN[i]=value;
        }
    }
    if (rx==(Rx-1)){
        for (n = 0; n < nSlices; ++n) {
            laik_map_def(data, n, (void**) &baseN, &countN);
            i=countN-1;
            baseN[i]=value;
        }
    }
    if (ry==0){
        n=0;
        laik_map_def(data, n, (void**) &baseN, &countN);
        for (i = 0; i < countN; ++i) {
            baseN[i]=value;
        }
    }
    if (ry==(Ry-1)){
        n=nSlices-1;
        laik_map_def(data, n, (void**) &baseN, &countN);
        for (i = 0; i < countN; ++i) {
            baseN[i]=value;
        }
    }
}

int main(int argc, char* argv[])
{
#ifdef USE_MPI
    Laik_Instance* inst = laik_init_mpi(&argc, &argv);
#else
    Laik_Instance* inst = laik_init_single();
#endif
    Laik_Group* world = laik_world(inst);

    #ifdef DBG

        if (laik_myid(world)==0)
        {
            int pause = 1;
            while (pause != 0);
        }

    #endif

    // handling the arguments
    int size = 0;
    int maxIt = 0;
    if (argc > 1) size = atoi(argv[1]);
    if (argc > 2) maxIt = atoi(argv[2]);

    if (size < 1) size = 10;
    if (maxIt < 1) maxIt = 5;

    // not all the configurations are supported
    // number of elements per task should be
    // devisable by the number of tasks

    int id = laik_myid(world);
    int numRanks = laik_size(world);
    int Rx, Ry, rx, ry;
    calculate_task_topology(numRanks,&Rx,&Ry);
    calculate_my_coordinate(numRanks,id,&rx,&ry);
    int Nx = size;
    int Ny = size;  //at the moment the partitioners only support Ny=Nx
    int Lx = Nx*Rx;
    int Ly = Ny*Ry;

    int size_nodes = (Lx+1)*(Ly+1);
    int size_elems = Lx*Ly;

    // create a list of neighbours for elements
    int* neighbours = build_element_neighbour_list(Lx,Ly);

    // 1d arrays for elements
    Laik_Space* element_space = laik_new_space_1d(inst, size_elems);
    Laik_Data* element = laik_new_data(world, element_space, laik_Double);

    // 1d arrays for nodes
    Laik_Space* node_space = laik_new_space_1d(inst, size_nodes);
    Laik_Data* node = laik_new_data(world, node_space, laik_Double);


    Laik_AccessPhase *pNodes, *pElements;

    pElements = laik_new_accessphase(world, element_space,
        laik_new_lulesh_element_partitioner(&Nx), 0);
    pNodes = laik_new_accessphase(world, node_space,
        laik_new_lulesh_node_partitioner(neighbours), pElements);

    double *baseN, *baseE;
    uint64_t countN, countE;

    // initialization phase
    // distribution of the elements
    laik_switchto(element, pElements, LAIK_DF_CopyOut);

    // map the partitioning to the memory
    // first the number of slices in the
    // partitioning is calculated and then
    // for each slide we return the back-end
    // memory address and count to sweep over
    // the memory elements
    int nSlicesElem = laik_phase_my_slicecount(pElements);
    for (int n = 0; n < nSlicesElem; ++n) {
        laik_map_def(element, n, (void**) &baseE, &countE);
        for (uint64_t i = 0; i < countE; i++)
        {
            baseE[i]=1.0;
        }
    }
    laik_switchto(element, pElements, LAIK_DF_CopyIn);

    // distribution of the nodes
    laik_switchto(node, pNodes, LAIK_DF_ReduceOut | LAIK_DF_Sum);
    //laik_switchto(node, pNodes, LAIK_DF_CopyOut);
    int nSlicesNodes = laik_phase_my_slicecount(pNodes);
    for (int n = 0; n < nSlicesNodes; ++n)
    {
        laik_map_def(node, n, (void**) &baseN, &countN);
        for (uint64_t i = 0; i < countN; i++)
        {
            baseN[i]=0.0;
        }
    }
    laik_switchto(node, pNodes, LAIK_DF_CopyIn);
    // set the boundary conditions on the nodes
    apply_boundary_condition(node,pNodes,Rx,Ry,rx,ry,0);

    // for debug only
    laik_log(1,"print elements:");
    print_data(element, pElements);
    laik_log(1,"print nodes:");
    print_data(node,pNodes);

    laik_log(1,"Initialization done.\n");

    // propagate the values from elements to the nodes
    // perform the propagation maxIt times
    uint64_t gi, gj0, gj1, gj2, gj3, j0, j1, j2, j3;
    int m0, m1, m2, m3;
    int nMapsElements;
    nMapsElements = laik_phase_my_mapcount(pElements);
    for (int it = 0; it < maxIt; it++)
    {
        // back-propagation
        // update the elements using their neighbours
        // go through all the elements and refere
        // to their neighbouring nodes and update the
        //elements
        laik_switchto(element, pElements, LAIK_DF_CopyOut);
        for (int m = 0; m < nMapsElements; m++) {
            laik_map_def(element, m, (void **)&baseE, &countE);

            for (uint64_t i = 0; i < countE; i++)
            {

                gi = laik_maplocal2global_1d(element, m, i);

                gj0 = get_element_neighbour(neighbours, gi, 0);
                gj1 = get_element_neighbour(neighbours, gi, 1);
                gj2 = get_element_neighbour(neighbours, gi, 2);
                gj3 = get_element_neighbour(neighbours, gi, 3);

                laik_global2maplocal_1d(node, gj0, &m0, &j0);
                laik_global2maplocal_1d(node, gj1, &m1, &j1);
                laik_global2maplocal_1d(node, gj2, &m2, &j2);
                laik_global2maplocal_1d(node, gj3, &m3, &j3);

                laik_map_def(node, m0, (void **)&baseN, &countN);
                baseE[i] += baseN[j0]/4 ;
                laik_map_def(node, m1, (void **)&baseN, &countN);
                baseE[i] += baseN[j1]/4;
                laik_map_def(node, m2, (void **)&baseN, &countN);
                baseE[i] += baseN[j2]/4;
                laik_map_def(node, m3, (void **)&baseN, &countN);
                baseE[i] += baseN[j3]/4;

            }
        }
        laik_switchto(element, pElements, LAIK_DF_CopyIn);


        //laik_switchto(node, pNodes, LAIK_DF_CopyOut);
        // propagation
        // update the nodes using elements
        // go through all the elements and refere
        // to their neighbouring nodes and update them
        laik_switchto(node, pNodes, LAIK_DF_Init | LAIK_DF_ReduceOut | LAIK_DF_Sum);
        for(int m = 0; m < nMapsElements; m++) {
            laik_map_def(element, m, (void **)&baseE, &countE);

            for (uint64_t i = 0; i < countE; i++)
            {

                gi = laik_maplocal2global_1d(element, m, i);

                gj0 = get_element_neighbour(neighbours, gi, 0);
                gj1 = get_element_neighbour(neighbours, gi, 1);
                gj2 = get_element_neighbour(neighbours, gi, 2);
                gj3 = get_element_neighbour(neighbours, gi, 3);

                laik_global2maplocal_1d(node, gj0, &m0, &j0);
                laik_global2maplocal_1d(node, gj1, &m1, &j1);
                laik_global2maplocal_1d(node, gj2, &m2, &j2);
                laik_global2maplocal_1d(node, gj3, &m3, &j3);

                //laik_log(1,"slice: %d, element: %d, global index: %d\n", m, i, gi);
                //laik_log(1,"global indexes for neighbours of element: %d: neighbour0:%d, neighbour1:%d, neighbour2:%d, neighbour3:%d\n"
                //                , gi, gj0, gj1, gj2, gj3);
                //laik_log(1,"local indexes for neighbours of element: %d: neighbour0:%d in mapping %d, neighbour1:%d in mapping %d, neighbour2:%d  in mapping %d, neighbour3:%d in mapping %d\n"
                //                , gi, j0, m0, j1, m1, j2, m2, j3, m3);

                laik_map_def(node, m0, (void **)&baseN, &countN);
                baseN[j0] += baseE[i]/4 ;
                laik_map_def(node, m1, (void **)&baseN, &countN);
                baseN[j1] += baseE[i]/4 ;
                laik_map_def(node, m2, (void **)&baseN, &countN);
                baseN[j2] += baseE[i]/4 ;
                laik_map_def(node, m3, (void **)&baseN, &countN);
                baseN[j3] += baseE[i]/4 ;
            }
        }
        laik_switchto(node, pNodes, LAIK_DF_CopyIn);
        apply_boundary_condition(node,pNodes,Rx,Ry,rx,ry,pow(2,it));
    }
    // for debug only
    laik_log(1,"print elements:");
    print_data(element, pElements);
    laik_log(1,"print nodes:");
    print_data(node,pNodes);
    // print check_sum for test
    double sum;
    sum = data_check_sum(element,pElements, world);
    if (id==0)
    {
        printf("expected : %f\n",1.0);
        printf("calculated: %f\n", sum/ (Lx*Ly*pow(2,maxIt-1)) ); //(normalized summation)
        //printf("for elements: %f\n", sum/ (pow(2,maxIt-1)) ); //(normalized summation)
        //printf("for elements: %f\n", sum/ (Lx*Ly) ); //(normalized summation)
    }

    free_neighbour_list(neighbours);
    laik_finalize(inst);
    return 0;
}
