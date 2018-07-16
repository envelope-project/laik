/* This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * Distributed Markov chain example, using LAIK reduction.
 */

#include <laik.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

typedef struct _MGraph {
    int n;   // number of states
    int out;  // fan-out
    int* cm; // connectivity
    double* pm; // probabilities
} MGraph;

// global options
int doPrint = 0;

// LAIK world
Laik_Group* world = 0;

// Produce a graph with <n> nodes and some arbitrary connectivity
// with a fan-in <in>. The resulting graph will be stored in
// <cm>[i,c], which is a <n> * (<in> +1) matrix storing the incoming nodes
// of node i in row i, using columns 1 .. <in> (column 0 is set to i).
// <pm>[i,j] is initialized with the probability of the transition
// from node <cm>[i,j] to node i, with cm[i,0] the prob for staying.
void init(MGraph* mg, int fineGrained)
{
    int n = mg->n;
    int out = mg->out;
    int* cm = mg->cm;
    double* pm = mg->pm;
    double sum;

    // some kind of ring structure
    for(int i=0; i < n; i++) {
        int step = 1;
        cm[i * (out + 1) + 0] = i; // stay in i
        pm[i * (out + 1) + 0] = 5;
        sum = 5;
        for(int j = 1; j <= out; j++) {
            int toNode = (i + step) % n;
            double prob = (double) ((j+i) % (5 * out)) + 1;
            sum += prob;
            cm[i * (out + 1) + j] = toNode;
            pm[i * (out + 1) + j] = prob;
            step = 2 * step + j + fineGrained * (i % 37);
            while(step > n) step -= n;
        }
        // normalization: all outgoing probabilities need to sum up to 1.0
        for(int j = 0; j <= out; j++) {
            pm[i * (out + 1) + j] /= sum;
        }
    }
}

void print(MGraph* mg)
{
    int n = mg->n;
    int out = mg->out;
    int* cm = mg->cm;
    double* pm = mg->pm;

    for(int i = 0; i < n; i++) {
        laik_log_begin(2);
        laik_log_append("State %2d: stay %.3f ", i, pm[i * (out + 1)]);
        for(int j = 1; j <= out; j++)
            laik_log_append("=(%.3f)=>%-2d  ",
                            pm[i * (out + 1) + j], cm[i * (out + 1) + j]);
        laik_log_flush("\n");
    }
}


void run_markovPartitioner(Laik_Partitioner* pr,
                           Laik_Partitioning* ba, Laik_Partitioning* otherBA)
{
    MGraph* mg = laik_partitioner_data(pr);
    int out = mg->out;
    int* cm = mg->cm;

    // go over states and add itself and incoming states to new partitioning
    int sliceCount = laik_partitioning_slicecount(otherBA);
    for(int i = 0; i < sliceCount; i++) {
        Laik_TaskSlice* ts = laik_partitioning_get_tslice(otherBA, i);
        const Laik_Slice* s = laik_taskslice_get_slice(ts);
        int task = laik_taskslice_get_task(ts);
        for(int st = s->from.i[0]; st < s->to.i[0]; st++) {
            int off = st * (out + 1);
            // j=0: state itself
            for(int j = 0; j <= out; j++)
                laik_append_index_1d(ba, task, cm[off + j]);
        }
    }
}



// Iteratively calculate probability distribution, return last written data.
// This version expects one (sparse) mapping of data1/data2 each
Laik_Data* runSparse(MGraph* mg, int miter,
                     Laik_Data* data1, Laik_Data* data2,
                     Laik_Partitioning* pWrite, Laik_Partitioning* pRead)
{
    int out = mg->out;
    int* cm = mg->cm;
    double* pm = mg->pm;

    if (miter == 0) return data1;

    // start reading from data1, writing to data2
    Laik_Data *dRead = data1, *dWrite = data2;
    double *src, *dst;
    uint64_t srcCount, dstCount;
    int64_t srcFrom, srcTo, dstFrom;

    int iter = 0;
    while(1) {
        laik_set_iteration(laik_data_get_inst(data1), iter+1);

        // switch dRead to pRead, dWrite to pWrite
        laik_switchto_partitioning(dRead,  pRead, LAIK_DF_Preserve, LAIK_RO_Sum);
        laik_map_def1(dRead, (void**) &src, &srcCount);
        laik_my_slice_1d(pRead, 0, &srcFrom, &srcTo);
        assert(srcFrom < srcTo);
        assert(srcCount == (uint64_t) (srcTo - srcFrom));

        laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_Init, LAIK_RO_Sum);
        laik_map_def1(dWrite, (void**) &dst, &dstCount);
        dstFrom = laik_local2global_1d(dWrite, 0);

        if (doPrint) {
            laik_log_begin(2);
            laik_log_append("Src values before iter %d:\n", iter);
            for(int i = srcFrom; i < srcTo; i++)
                laik_log_append("  %d: %f", i, src[i - srcFrom]);
            laik_log_flush("\n");
        }

        // spread values according to probability distribution
        for(int i = srcFrom; i < srcTo; i++) {
            int off = i * (out + 1);
            for(int j = 0; j <= out; j++) {
                if (doPrint)
                    laik_log(2,
                             "  adding %f from state %d to state %d: before %f, after %f",
                             src[i - srcFrom] * pm[off + j], i, cm[off + j],
                            dst[cm[off + j] - dstFrom],
                            dst[cm[off + j] - dstFrom] + src[i - srcFrom] * pm[off + j]);

                dst[cm[off + j] - dstFrom] += src[i - srcFrom] * pm[off + j];
            }
        }

        if (doPrint) {
            laik_log_begin(2);
            laik_log_append("Src values after after %d:\n", iter);
            for(int64_t i = srcFrom; i < srcTo; i++)
                laik_log_append("  %d: %f", i, dst[i - dstFrom]);
            laik_log_flush("\n");
        }

        iter++;
        if (iter == miter) break;

        // swap role of data1 and data2
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }
    }

    return dWrite;
}

// iteratively calculate probability distribution, return last written data
// this assumes a compact mapping for data1/2, using indirection
Laik_Data* runIndirection(MGraph* mg, int miter,
                          Laik_Data* data1, Laik_Data* data2, Laik_Data* idata,
                          Laik_Partitioning* pWrite, Laik_Partitioning* pRead)
{
    int out = mg->out;
    double* pm = mg->pm;

    if (miter == 0) return data1;

    // local index array
    int* iarray;
    uint64_t icount;
    laik_map_def1(idata, (void**) &iarray, &icount);

    // start reading from data1, writing to data2
    Laik_Data *dRead = data1, *dWrite = data2;
    double *src, *dst;
    uint64_t srcCount, dstCount;
    int64_t srcFrom, srcTo;

    int iter = 0;
    while(1) {
        laik_set_iteration(laik_data_get_inst(data1), iter+1);

        // switch dRead to pRead, dWrite to pWrite
        laik_switchto_partitioning(dRead,  pRead, LAIK_DF_Preserve, LAIK_RO_Sum);
        laik_map_def1(dRead, (void**) &src, &srcCount);
        laik_my_slice_1d(pRead, 0, &srcFrom, &srcTo);
        assert(srcFrom < srcTo);
        assert(srcCount == (uint64_t) (srcTo - srcFrom));

        laik_switchto_partitioning(dWrite, pWrite, LAIK_DF_Init, LAIK_RO_Sum);
        laik_map_def1(dWrite, (void**) &dst, &dstCount);

        if (doPrint) {
            laik_log_begin(2);
            laik_log_append("Src values at iter %d:\n", iter);
            for(int i = srcFrom; i < srcTo; i++)
                laik_log_append("  %d: %f", i, src[i - srcFrom]);
            laik_log_flush("\n");
        }

        // spread values according to probability distribution
        for(uint64_t i = 0; i < srcCount; i++) {
            int off = i * (out + 1);
            int goff = (i + srcFrom) * (out + 1);
            for(int j = 0; j <= out; j++)
                dst[iarray[off + j]] += src[i] * pm[goff + j];
        }

        iter++;
        if (iter == miter) break;

        // swap role of data1 and data2
        if (dRead == data1) { dRead = data2; dWrite = data1; }
        else                { dRead = data1; dWrite = data2; }
    }

    return dWrite;
}


int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    world = laik_world(inst);

    int n = 100000;
    int out = 10;
    int miter = 10;
    int onestate = -1;
    int doCompact = 0;
    int doIndirection = 0;
    int useSingleIndex = 0;
    int fineGrained = 0;
    int doProfiling = 0;
    doPrint = 0;

    int arg = 1;
    while((arg < argc) && (argv[arg][0] == '-')) {
        switch(argv[arg][1]) {
        case 'c': doCompact = 1; break;
        case 'i': doIndirection = 1; break;
        case 's': useSingleIndex = 1; break;
        case 'f': fineGrained = 1; break;
        case 'v': doPrint = 1; break;
        case 'p': doProfiling = 1; break;
        case 'h':
        default:
            printf("markov [options] [<states> [<fan-out> [<iterations> [<istate>]]]]\n"
                   "\nParameters:\n"
                   "  <states>     : number of states (def %d)\n"
                   "  <fan-ou>     : number of outgoing edges per state (def %d)\n"
                   "  <iterations> : number of iterations to run\n"
                   "  <istate>     : if given: state with initial value 1, others 0\n"
                   "                 default: all states set to same value\n"
                   "\nOptions:\n"
                   " -i: use indirection with pre-calculated local indexes\n"
                   " -c: use a compact mapping (implies -i)\n"
                   " -s: use single index hint\n"
                   " -f: use pseudo-random connectivity (much more slices)\n"
                   " -v: be verbose using laik_log(), level 2\n"
                   " -p: write profiling measurements to 'markov2_profiling.txt'\n"
                   " -h: this help text\n", n, out);
            exit(1);
        }
        arg++;
    }
    if (argc > arg) n = atoi(argv[arg]);
    if (argc > arg + 1) out = atoi(argv[arg + 1]);
    if (argc > arg + 2) miter = atoi(argv[arg + 2]);
    if (argc > arg + 3) onestate = atoi(argv[arg + 3]);

    if (n == 0) n = 100000;
    if (out == 0) out = 10;
    if (doCompact) doIndirection = 1;
    if (onestate >= n) onestate = -1;

    if (laik_myid(world) == 0) {
        printf("Init Markov chain with %d states, max fan-out %d.\n", n, out);
        printf("Running %d iterations.%s%s%s\n", miter,
               useSingleIndex ? " Partitioner using single indexes.":"",
               doCompact ? " Using compact mapping.":"",
               doIndirection ? " Using indirection.":"");
        if (onestate >= 0)
            printf("Initial values: all 0, just state %d set to 1.\n", onestate);
        else
            printf("All initial values set to %f.\n", 1.0 / n);
    }

    MGraph mg;
    mg.n = n;
    mg.out = out;
    mg.cm = malloc(n * (out + 1) * sizeof(int));
    mg.pm = malloc(n * (out + 1) * sizeof(double));

    init(&mg, fineGrained);
    if (doPrint) print(&mg);

    // two 1d arrays, using same space
    Laik_Space* space = laik_new_space_1d(inst, n);
    Laik_Data* data1 = laik_new_data(space, laik_Double);
    Laik_Data* data2 = laik_new_data(space, laik_Double);

    //profiling
    if (doProfiling)
        laik_enable_profiling_file(inst, "markov2_profiling.txt");

    // partitionings used:
    // - pWrite: distribution of states
    // - pRead : access to values of incoming states
    // - pMaster: all data at master, for checksum
    // pRead/pWrite are assigned to either data1/data2,
    // exchanged after every iteration
    Laik_Partitioning *pRead, *pWrite, *pMaster;
    Laik_Partitioner* pr;
    pRead = laik_new_partitioning(laik_new_block_partitioner1(),
                                  world, space, 0);
    pr = laik_new_partitioner("markov-out", run_markovPartitioner, &mg,
                              LAIK_PF_Merge |
                              (useSingleIndex ? LAIK_PF_SingleIndex : 0) |
                              (doCompact ? LAIK_PF_Compact : 0));
    pWrite = laik_new_partitioning(pr, world, space, pRead);
    pMaster = laik_new_partitioning(laik_Master, world, space, 0);

    // for indirection, we store local indexes in a LAIK container
    Laik_Type* itype = laik_type_register("l-indexes", (out + 1) * sizeof(int));
    Laik_Data* idata = laik_new_data(space, itype);

    if (doIndirection) {
        // register initialization function for global-to-local index data
        // this is called whenever the partitioning is changing
        // FIXME: add API to specify function for init
        laik_switchto_partitioning(idata, pRead, LAIK_DF_None, LAIK_RO_None);
        // TODO: move to inititialization function
        int* iarray;
        uint64_t icount, ioff;
        laik_map_def1(idata, (void**) &iarray, &icount);
        for(uint64_t i = 0; i < icount; i++) {
            int gi = laik_local2global_1d(idata, i);
            for(int j = 0; j <= out; j++) {
                int gidx = mg.cm[gi * (out + 1) + j];
                assert(laik_global2local_1d(idata, gidx, &ioff) != 0);
                iarray[i * (out + 1) + j] = ioff;
            }
        }
    }


    laik_set_phase(inst, 1, "Init", 0);

    laik_reset_profiling(inst);
    laik_profile_user_start(inst);

    // distributed initialization of data1
    // (uses pRead with disjuntive partitioning here, in contrast to reading
    //  from owned states later in the iterations)
    double *v;
    uint64_t count, off;
    laik_switchto_partitioning(data1, pRead, LAIK_DF_None, LAIK_RO_None);
    laik_map_def1(data1, (void**) &v, &count);
    double p = (onestate < 0) ? (1.0 / n) : 0.0;
    for(uint64_t i = 0; i < count; i++)
        v[i] = p;
    if (onestate >= 0) {
        // set state <phase> to probability 1
        if (laik_global2local_1d(data1, onestate, &off)) {
            // if global index 0 is local, it must be at local index 0
            assert(off == (uint64_t) onestate);
            v[off] = 1.0;
        }
    }

    laik_profile_user_stop(inst);
    laik_writeout_profile();
    laik_reset_profiling(inst);
    laik_profile_user_start(inst);

    laik_set_phase(inst, 2, "Calc", 0);

    Laik_Data* dRes;
    if (doIndirection)
        dRes = runIndirection(&mg, miter, data1, data2,
                              idata, pWrite, pRead);
    else
        dRes = runSparse(&mg, miter, data1, data2, pWrite, pRead);

    laik_profile_user_stop(inst);
    laik_writeout_profile();
    laik_reset_profiling(inst);
    laik_set_phase(inst, 3, "Collect", 0);

    laik_switchto_partitioning(dRes, pMaster, LAIK_DF_Preserve, LAIK_RO_Sum);
    laik_writeout_profile();
    laik_map_def1(dRes, (void**) &v, &count);
    laik_set_phase(inst, 4, "Out", 0);
    if (laik_myid(world) == 0) {

        assert((int)count == n);

        if (doPrint) {
            laik_log_begin(2);
            laik_log_append("Result values:\n");
            for(int i = 0; i < n; i++)
                laik_log_append("  %d: %f", i, v[i]);
            laik_log_flush("\n");
        }

        double sum = 0.0;
        for(int i=0; i < n; i++)
            sum += v[i];
        printf("Result probs: p0 = %g, p1 = %g, p2 = %g, Sum: %f\n",
               v[0], v[1], v[2], sum);
    }

    laik_finalize(inst);
    return 0;
}
