#include <laik.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/**
 * Simple 2d finite element example
 *
 * Elements with square shape are regularly arranged in a 2d grid, with each
 * element bound by 4 nodes in its square corners. Corners are shared by
 * neighboring elements.
 *
 * As example, 16 elements arranged in a 4x4 grid need 5x5 nodes as element
 * corners. Using a x/y order for numbering elements and nodes, this results
 * in element e0 with nodes n0, n1, n5, n6 as corners, and neighbor elements
 * e1 (to the right) and e4 (to the bottom). e0 and e1 share nodes n1 and n6,
 * while n6 is shared by elements e0, e1, e4, and e5.
 *
 *   n0    n1    n2    n3    n4
 *      e0    e1    e2    e3
 *   n5    n6    n7    n8    n9
 *      e4    e5    e7    e8
 *   ...
 *      e12 ...
 *   n20   n21 ...
 *
 * Distribution of work is done by splitting the elements into a 2d grid,
 * e.g. with 4 tasks into 2 x 2, with elements 0, 1, 4, 5 mapped to task 0.
 * With 1d indexing of elements, task 0 gets two slices: [0-1] and [4-5].
 *
 * We start with element values 1.0 and node values 0.0, and do
 * multiple iterations. In each
 *  (3) add 1/4 of node values to each element value the node is corner of
 *  (1) propagate values from elements to nodes
 *  (2) aggregate propagated values in nodes (we do a sum here)
 *
 * While elements are exclusively partitioned, nodes may directly lie on
 * partition boundaries, shared by multiple processes with having private
 * copies. In (1), such private copies will contain only partial sums.
 * We can use a LAIK transition to sum up the partitial values of private
 * copies corresponding to the same nodes, resulting in full sums.
 *
 * This example uses seperate data containers for elements and nodes.
 * The partitioning of nodes is derived from the partitoning of elements.
**/

// search for a good 2d grid partitioning
void calculate_task_topology(int numRanks, int* Rx, int* Ry)
{
    *Ry = (int) sqrt(numRanks);
    while ( (*Ry) > 0) {
        if (numRanks%(*Ry) == 0)
            break;
        (*Ry)--;
    }
    *Rx = numRanks/(*Ry);
    assert ( (*Rx)*(*Ry) == numRanks );
}

void calculate_my_coordinate(int numRanks, int rank, int* rx, int* ry)
{
    int Rx;
    int Ry;
    calculate_task_topology(numRanks, &Rx, &Ry);
    *rx = rank % Rx;
    *ry = rank / Rx;
}

// partitioner algorithm for the 1d array of elements, using a 2d grid
void run_element_partitioner(Laik_Partitioning* p, Laik_Partitioner* pr,
                             Laik_Space* space, Laik_Group* group,
                             Laik_Partitioning* other)
{
    (void) other; // unused, required due to interface signature

    int N_local_x = *((int*) laik_partitioner_data(pr));
    int N_local_y = N_local_x; // only square subdomain are supported!
    int N_tasks_x;
    int N_tasks_y;
    int N_tasks = laik_size(group);
    calculate_task_topology(N_tasks, &N_tasks_x, &N_tasks_y);
    assert(N_tasks = N_tasks_x * N_tasks_y);
    int N_elems_x = N_local_x * N_tasks_x;
    int N_elems_y = N_local_y * N_tasks_y;
    assert( (int) laik_space_size(space) == N_elems_x * N_elems_y);

    Laik_Slice slc;

    for(int ix = 0; ix < N_tasks_x; ix++) {
        for(int iy = 0; iy < N_tasks_y; iy++) {
            for(int jy = 0; jy < N_local_y; jy++) {
                int idx = ix * N_local_x + (iy * N_local_y + jy) * N_elems_x;
                laik_slice_init_1d(&slc, space, idx, idx + N_local_x);
                laik_append_slice(p, ix + iy * N_tasks_x, &slc, 0, 0);
            }
        }
    }
}

Laik_Partitioner* get_element_partitioner(int *size)
{
    return laik_new_partitioner("element", run_element_partitioner,
                                (void*) size, 0);
}

// partitioner for 1d array of nodes, derived from element partitioning
void run_node_partitioner(Laik_Partitioning* p, Laik_Partitioner* pr,
                          Laik_Space* space, Laik_Group* group,
                          Laik_Partitioning* other)
{
    int* neighbours = (int*) laik_partitioner_data(pr);
    int Rx,Ry,rx,ry;
    calculate_task_topology(laik_size(group), &Rx, &Ry);

    Laik_Slice slc;

    // for all the slices in the element partitioner
    // we find the neighbouring nodes and add a slice
    // to the new partioning
    int sliccountE = laik_partitioning_slicecount(other);
    for(int i = 0; i < sliccountE; i++) {
        Laik_TaskSlice* ts = laik_partitioning_get_tslice(other, i);
        const Laik_Slice* s = laik_taskslice_get_slice(ts);
        int task = laik_taskslice_get_task(ts);

        // add bottom/top node rows bounding the elements of this slice.
        // as we merge slices afterwards, there is no problem eventually
        // adding the same nodes twice
        calculate_my_coordinate(laik_size(group), task, &rx, &ry);
        laik_slice_init_1d(&slc, space,
                           neighbours[ 4 * s->from.i[0] + 0 ] + 0,
                           neighbours[ 4 * (s->to.i[0] - 1) + 1 ] + 1);
        laik_append_slice(p, task, &slc, 0, 0);

        laik_slice_init_1d(&slc, space,
                           neighbours[ 4 * s->from.i[0] + 2] + 0,
                           neighbours [ 4 * (s->to.i[0] - 1) + 3 ] + 1);
        laik_append_slice(p, task, &slc, 0, 0);
    }
}

Laik_Partitioner* get_node_partitioner(int* neighbours)
{
    return laik_new_partitioner("node", run_node_partitioner,
                                neighbours, LAIK_PF_Merge);
}


// create a global list of neighbors of the elements
int* build_element_neighbour_list(int Lx, int Ly)
{
    int num_nodes = Lx * Ly;

    // allocate memory for the integers
    int* neighbours = (int*) malloc (sizeof(int)*num_nodes*4);

    int jy = 0;
    for(int node = 0; node < num_nodes; node++)
    {
        jy = node / Lx;
        neighbours[4*node + 0] =  node + jy;
        neighbours[4*node + 1] =  node + jy + 1;
        neighbours[4*node + 2] =  node + jy + Lx + 1;
        neighbours[4*node + 3] =  node + jy + Lx + 2;
    }

    return neighbours;
}

void free_neighbour_list(int* neighbour)
{
    free(neighbour);
}

// fuction to refere to the neighburs of the elements
int get_element_neighbour(int* neighbours, int element, int node_index)
{
    return neighbours[4*element+node_index];
}

// for the debug
void print_data(Laik_Data* d, Laik_Partitioning* p)
{
    double *base;
    uint64_t count;
    int nSlices = laik_my_slicecount(p);
    for (int s = 0; s < nSlices; s++) {
        laik_map_def(d, s, (void**) &base, &count);
        laik_log_begin(1);
        for (uint64_t i = 0; i < count; i++) {
            laik_log_append(" %f", base[i]);
        }
        laik_log_flush("");
    }
}

// for testing
double data_check_sum(Laik_Data* d, Laik_Partitioning* p, Laik_Group* world)
{
    double *base;
    uint64_t count;
    int nSlices = laik_my_slicecount(p);
    double sum = 0.0;
    for(int s = 0; s < nSlices; s++) {
        laik_map_def(d, s, (void**) &base, &count);
        for (uint64_t i = 0; i < count; i++) {
            sum += base[i];
        }
    }

    Laik_Space* sumspace;
    Laik_Data* sumdata;
    Laik_Partitioning* sumpart;
    sumspace = laik_new_space_1d(laik_inst(world), 1);
    sumdata  = laik_new_data(sumspace, laik_Double);
    sumpart  = laik_new_partitioning(laik_All, world, sumspace, 0);
    laik_switchto_partitioning(sumdata, sumpart, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(sumdata, (void**) &base, 0);
    *base = sum;
    laik_switchto_partitioning(sumdata, sumpart, LAIK_DF_Preserve, LAIK_RO_Sum);
    laik_map_def1(sumdata, (void**) &base, 0);

    return *base;
}

// this assumes the 2d grid partitioning
void apply_boundary_condition(Laik_Data* data, Laik_Partitioning* p,
                              int Rx, int Ry, int rx, int ry, double value)
{
    double *baseN;
    uint64_t countN, i;
    int nSlices = laik_my_slicecount(p);
    int n;

    if (rx == 0) {
        i = 0;
        for(n = 0; n < nSlices; ++n) {
            laik_map_def(data, n, (void**) &baseN, &countN);
            baseN[i]=value;
        }
    }
    if (rx == Rx - 1) {
        for(n = 0; n < nSlices; ++n) {
            laik_map_def(data, n, (void**) &baseN, &countN);
            i=countN-1;
            baseN[i]=value;
        }
    }
    if (ry == 0) {
        n = 0;
        laik_map_def(data, n, (void**) &baseN, &countN);
        for(i = 0; i < countN; ++i) {
            baseN[i]=value;
        }
    }
    if (ry == Ry - 1) {
        n = nSlices - 1;
        laik_map_def(data, n, (void**) &baseN, &countN);
        for(i = 0; i < countN; ++i) {
            baseN[i]=value;
        }
    }
}

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);

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

    int myid = laik_myid(world);
    int numRanks = laik_size(world);

    int Rx, Ry; // processes are assigned to elements using a Rx * Ry grid
    int rx, ry; // in this grid, this process is at coordinate (rx/ry)
    calculate_task_topology(numRanks, &Rx, &Ry);
    calculate_my_coordinate(numRanks, myid, &rx, &ry);

    // size is input: size * size elements are associated to this process
    int Nx = size;
    int Ny = size;  // at the moment the partitioners only support Ny=Nx
    int Lx = Nx*Rx; // total number of elements in X dimension
    int Ly = Ny*Ry;

    int size_nodes = (Lx+1)*(Ly+1);
    int size_elems = Lx*Ly;

    // create a list of neighbours for elements
    int* neighbours = build_element_neighbour_list(Lx,Ly);

    // 1d arrays for elements
    Laik_Space* element_space = laik_new_space_1d(inst, size_elems);
    Laik_Data* element = laik_new_data(element_space, laik_Double);

    // 1d arrays for nodes
    Laik_Space* node_space = laik_new_space_1d(inst, size_nodes);
    Laik_Data* node = laik_new_data(node_space, laik_Double);

    // partitionings are defined by our own custom partitioner functions
    Laik_Partitioning *pNodes, *pElements;

    pElements = laik_new_partitioning(get_element_partitioner(&Nx),
                                      world, element_space, 0);

    pNodes = laik_new_empty_partitioning(world, node_space,
                                         get_node_partitioner(neighbours),
                                         pElements);
    laik_partitioning_set_myfilter(pNodes);
    laik_run_partitioner(pNodes);

    double *baseN, *baseE;
    uint64_t countN, countE;

    // for initialization, assign partitionings to LAIK containers

    // for elements
    // note: we never change the partitioning again, ie. no allocation change
    laik_switchto_partitioning(element, pElements, LAIK_DF_None, LAIK_RO_None);

    // for the element partition assigned to me:
    // go over all my slices and find out where they are allocated in memory.
    // set the double value for each element to 1.0
    int nSlicesElem = laik_my_slicecount(pElements);
    for (int n = 0; n < nSlicesElem; ++n) {
        laik_map_def(element, n, (void**) &baseE, &countE);
        for (uint64_t i = 0; i < countE; i++) {
            baseE[i] = 1.0;
        }
    }

    // same for nodes, initialize node value to 0.0
    laik_switchto_partitioning(node, pNodes, LAIK_DF_None, LAIK_RO_None);
    int nSlicesNodes = laik_my_slicecount(pNodes);
    for (int n = 0; n < nSlicesNodes; ++n) {
        laik_map_def(node, n, (void**) &baseN, &countN);
        for (uint64_t i = 0; i < countN; i++) {
            baseN[i] = 0.0;
        }
    }

    // set the boundary conditions on the nodes
    apply_boundary_condition(node, pNodes, Rx, Ry, rx, ry, 0.0);

    // for debug only
    laik_log(1,"print elements:");
    print_data(element, pElements);
    laik_log(1,"print nodes:");
    print_data(node, pNodes);

    laik_log(1,"Initialization done.\n");

    // propagate the values from elements to the nodes
    // perform the propagation maxIt times
    uint64_t gi, gj0, gj1, gj2, gj3, j0, j1, j2, j3;
    int m0, m1, m2, m3;
    int nMapsElements;
    nMapsElements = laik_my_mapcount(pElements);

    for (int it = 0; it < maxIt; it++) {
        // back-propagation:
        // - update the elements using their neighbours,
        // - go through all the elements and refer to their
        //   neighbouring nodes and update the elements
        for (int m = 0; m < nMapsElements; m++) {
            laik_map_def(element, m, (void **)&baseE, &countE);

            for (uint64_t i = 0; i < countE; i++) {
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
                baseE[i] += baseN[j0] / 4;
                laik_map_def(node, m1, (void **)&baseN, &countN);
                baseE[i] += baseN[j1] / 4;
                laik_map_def(node, m2, (void **)&baseN, &countN);
                baseE[i] += baseN[j2] / 4;
                laik_map_def(node, m3, (void **)&baseN, &countN);
                baseE[i] += baseN[j3] / 4;
            }
        }

        // forward propagation:
        // - update the nodes using elements
        // - go through all the elements and refer
        //   to their neighbouring nodes and update them
        laik_switchto_partitioning(node, pNodes, LAIK_DF_Init, LAIK_RO_Sum);
        for(int m = 0; m < nMapsElements; m++) {
            laik_map_def(element, m, (void **)&baseE, &countE);

            for (uint64_t i = 0; i < countE; i++) {
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
                baseN[j0] += baseE[i] / 4;
                laik_map_def(node, m1, (void **)&baseN, &countN);
                baseN[j1] += baseE[i] / 4;
                laik_map_def(node, m2, (void **)&baseN, &countN);
                baseN[j2] += baseE[i] / 4;
                laik_map_def(node, m3, (void **)&baseN, &countN);
                baseN[j3] += baseE[i] / 4;
            }
        }
        laik_switchto_partitioning(node, pNodes, LAIK_DF_Preserve, LAIK_RO_Sum);
        apply_boundary_condition(node, pNodes, Rx, Ry, rx, ry, pow(2, it));

        // for debug only
        laik_log(1,"print elements (after iteration %d):", it);
        print_data(element, pElements);
        laik_log(1,"print nodes (after iteration %d):", it);
        print_data(node,pNodes);
    }

    // print check_sum for test
    double sum;
    sum = data_check_sum(element, pElements, world);
    if (myid==0) {
        printf("expected : %f\n",1.0);
        printf("calculated: %f\n", sum / (Lx*Ly*pow(2,maxIt-1)) ); //(normalized summation)
        //printf("for elements: %f\n", sum/ (pow(2,maxIt-1)) ); //(normalized summation)
        //printf("for elements: %f\n", sum/ (Lx*Ly) ); //(normalized summation)
    }

    free_neighbour_list(neighbours);
    laik_finalize(inst);
    return 0;
}
