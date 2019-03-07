/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017, 2018 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * partitioning.c
 *
 * This file contains functions related to processing partitionings
 * (Laik_Partitioning objects).
 * It is part of the Index Space module.
*/


#include "laik-internal.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>


/// SliceFilter

Laik_SliceFilter* laik_slicefilter_new()
{
    Laik_SliceFilter* sf;

    sf = malloc(sizeof(Laik_SliceFilter));
    if (sf == 0) {
        laik_panic("Out of memory allocating Laik_SliceFilter object");
        exit(1); // not actually needed, laik_panic never returns
    }

    // no filter set in the beginning
    sf->filter_func = 0;

    sf->filter_tid = -1;
    sf->pfilter1 = 0;
    sf->pfilter2 = 0;

    return sf;
}

void laik_slicefilter_free(Laik_SliceFilter* sf)
{
    free(sf->pfilter1);
    free(sf->pfilter2);
    free(sf);
}

// helper for laik_slicefilter_set_myfilter
static
bool tidfilter(Laik_SliceFilter* sf, int task, const Laik_Slice* s)
{
    (void) s; // unused parameter of filter signature

    assert(sf->filter_tid >= 0); // only to be called with "my" filter active
    return (sf->filter_tid == task);
}

// set filter to only keep slices for own process when adding slices
void laik_slicefilter_set_myfilter(Laik_SliceFilter* sf, Laik_Group* g)
{
    assert(sf->filter_func == 0); // no filter set yet
    sf->filter_tid = g->myid;
    sf->filter_func = tidfilter;
}


// helper for laik_slicefilter_set_idxfilter

// check if [from;to[ intersects slices given in par
static bool idxfilter_check(int64_t from, int64_t to, PFilterPar* par)
{
    assert(par->len > 0);

    laik_log(1,"  filter [%lld;%lld[ check with range [%lld;%lld[",
             (long long) from, (long long) to,
             (long long) par->from, (long long) par->to);

    if ((from >= par->to) || (to <= par->from)) {
        laik_log(1,"    no intersection!");
        return false;
    }

    // binary search
    Laik_TaskSlice_Gen* ts = par->ts;
    unsigned int off1 = 0, off2 = par->len;
    while(off1 < off2) {
        unsigned int mid = (off1 + off2) / 2;
        laik_log(1,"  filter check at %d: [%lld;%lld[",
                 mid,
                 (long long) ts[mid].s.from.i[0],
                 (long long) ts[mid].s.to.i[0]);

        if (from >= ts[mid].s.to.i[0]) {
            laik_log(1,"    larger, check against %d: [%lld;%lld[",
                     mid+1,
                     (long long) ts[mid+1].s.from.i[0],
                     (long long) ts[mid+1].s.to.i[0]);
            if (to <= ts[mid+1].s.from.i[0]) {
                laik_log(1,"    no intersection!");
                return false;
            }
            off1 = mid;
            continue;
        }
        if (to <= ts[mid].s.from.i[0]) {
            laik_log(1,"    smaller, check against %d: [%lld;%lld[",
                     mid-1,
                     (long long) ts[mid-1].s.from.i[0],
                     (long long) ts[mid-1].s.to.i[0]);
            if (from >= ts[mid-1].s.to.i[0]) {
                laik_log(1,"    no intersection!");
                return false;
            }
            off2 = mid;
            continue;
        }
        break;
    }
    laik_log(1,"    found intersection!");
    return true;
}


static
bool idxfilter(Laik_SliceFilter* sf, int task, const Laik_Slice* s)
{
    (void) task; // unused parameter of filter signature

    int64_t from = s->from.i[0];
    int64_t to = s->to.i[0];

    if (sf->pfilter1 && idxfilter_check(from, to, sf->pfilter1)) return true;
    if (sf->pfilter2 && idxfilter_check(from, to, sf->pfilter2)) return true;
    return false;
}

// add filter to only keep slices intersecting with slices in <sa>
void laik_slicefilter_add_idxfilter(Laik_SliceFilter* sf, Laik_SliceArray* sa)
{
    assert(sa);
    assert(sa->off != 0);
    assert(sa->space->dims == 1); // TODO: only for 1d for now

    if (sa->count == 0) return;

    PFilterPar* par = malloc(sizeof(PFilterPar));
    par->len = sa->count;
    par->from = sa->tslice[0].s.from.i[0];
    par->to   = sa->tslice[sa->count - 1].s.to.i[0];
    par->ts = sa->tslice;

    if      (sf->pfilter1 == 0) sf->pfilter1 = par;
    else if (sf->pfilter2 == 0) sf->pfilter2 = par;
    else assert(0);

    laik_log(1,"Set pfilter to intersection with %d slices between [%lld;%lld[",
             par->len,
             (long long) par->from, (long long) par->to);

    // install filter function
    sf->filter_func = idxfilter;
}




/**
 * A partitioning is a set of slices (= consecutive ranges) into a space,
 * with each slice assigned to a process from a LAIK process group.
 *
 * Partitionings are created by
 * - running a partitioner algorithm, or by
 * - reading a pre-created and stored, serialized version from memory
 * Both can be done in a distributed way. Each process of the parallel
 * application may provide only part of the full partitioning, eventually
 * followed by a global synchronization (this is not needed if every process
 * provides the full partitioning).
 *
 * Application processes iteratively ask for the slices assigned to them.
 * Slices are returned in a given normalized order.
 *
 * Partitioner algorithms can be offline and online: an online partitioner
 * is called whenever the application asks for the next slice. In this case,
 * the partitioner is responsible for providing the slices in an adequate
 * order, and coordinating the different called partitioner functions in the
 * parallel processes (e.g. by implementing a global slice queue).
 *
 * In contrast, an offline partitioner algorithm can provide arbitrarily
 * overlapping slices in any arbitrary order. Further, it can group
 * slices by tagging them with group IDs. It is up to the application to make
 * use of such groups. After running the offline partitioner algorithm,
 * LAIK ensures a normalized ordering by optionally splitting/merging slices
 * to remove overlaps and sorting them, grouping the slices if requested.
 *
 * A deterministic offline partitioner algorithm must ensure that it returns
 * the same partitioning, given the same input (space, process group,
 * base partitioning). LAIK may call such partitioners multiple times,
 * especially in different processes to avoid communication, but also in
 * the same process to only store slices currently needed.
 *
 * General useful partitioner algorithms are provided by LAIK.
 * However, the API which needs to be used by a partitioner is specified
 * and applications can provide their own partitioner implementations.
 */

static int partitioning_id = 0;

// internal helper
Laik_Partitioning* laik_partitioning_new(char* name,
                                         Laik_Group* g, Laik_Space* s,
                                         Laik_Partitioner* pr,
                                         Laik_Partitioning* other)
{
    Laik_Partitioning* p;

    assert(s != 0);
    assert(g != 0);

    p = malloc(sizeof(Laik_Partitioning));
    if (p == 0) {
        laik_panic("Out of memory allocating Laik_Partitioning object");
        exit(1); // not actually needed, laik_panic never returns
    }

    p->id = partitioning_id++;
    p->name = strdup("            ");
    sprintf(p->name, "%.10s-%d", name ? name : "part", p->id);

    p->group = g;
    p->space = s;
    p->partitioner = pr;

    p->slices = laik_slicearray_new(s, (unsigned int) g->size);
    p->filter = 0;

    p->other = other;

    return p;
}




/**
 * Create an empty, invalid partitioning on a given space for a given
 * process group.
 *
 * To make the partitioning valid, either a partitioning algorithm has to be
 * run, filling the partitioning with slices, or a partitioner algorithm
 * is set to be run whenever partitioning slices are to be consumed (online).
 *
 * Before running the partitioner, filters on tasks or indexes can be set
 * such that not all slices get stored during the partitioner run.
 * However, such filtered partitionings are not generally useful, e.g.
 * for calculating transitions.
 *
 * Different internal formats are used depending on how a partitioner adds
 * indexes. E.g. adding single indexes are managed differently than slices.
 */
Laik_Partitioning* laik_new_empty_partitioning(Laik_Group* g, Laik_Space* s,
                                               Laik_Partitioner* pr,
                                               Laik_Partitioning* other)
{
    return laik_partitioning_new(0, g, s, pr, other);
}

// create a new empty partitioning using same parameters as in given one
Laik_Partitioning* laik_clone_empty_partitioning(Laik_Partitioning* p)
{
    return laik_partitioning_new(p->name, p->group, p->space,
                                 p->partitioner, p->other);
}




// partitioner API: add a slice
// - specify slice groups by giving slices the same tag
// - arbitrary data can be attached to slices if no merge step is done
void laik_append_slice(Laik_Partitioning* p, int task, const Laik_Slice *s,
                       int tag, void* data)
{
    // if filter is installed, check if we should store the slice
    Laik_SliceFilter* sf = p->filter;
    if (sf) {
        bool res = (sf->filter_func)(sf, task, s);
        laik_log(1,"appending slice %d:[%lld;%lld[: %s",
                 task,
                 (long long) s->from.i[0], (long long) s->to.i[0],
                 res ? "keep":"skip");
        if (res == false) return;
    }

    laik_slicearray_append(p->slices, task, s, tag, data);
}

// partitioner API: add a single 1d index slice, optimized for fast merging
// if a partitioner only uses this method, an optimized internal format is used
void laik_append_index_1d(Laik_Partitioning* p, int task, int64_t idx)
{
    Laik_SliceFilter* sf = p->filter;
    if (sf) {
        Laik_Slice slc;
        laik_slice_init_1d(&slc, p->space, idx, idx + 1);
        bool res = (sf->filter_func)(sf, task, &slc);
        laik_log(1,"appending slice %d:[%lld;%lld[: %s",
                 task,
                 (long long) slc.from.i[0], (long long) slc.to.i[0],
                 res ? "keep":"skip");
        if (res == false) return;
    }

    if (p->slices->tslice) {
        // append as generic slice
        Laik_Slice slc;
        laik_slice_init_1d(&slc, p->space, idx, idx + 1);
        laik_slicearray_append(p->slices, task, &slc, 1, 0);
        return;
    }

    laik_slicearray_append_single1d(p->slices, task, idx);
}


// public: return the space a partitioning is used for
Laik_Space* laik_partitioning_get_space(Laik_Partitioning* p)
{
    return p->space;
}

// public: return the process group a partitioning is used for
// slices are associated to processes using their rank in the process group
Laik_Group* laik_partitioning_get_group(Laik_Partitioning* p)
{
    return p->group;
}

// public/partitioner API: total number of slices in this partitioning
// only allowed for offline partitioners, may be expensive
int laik_partitioning_slicecount(Laik_Partitioning* p)
{
    return laik_slicearray_slicecount(p->slices);
}

// partitioner API: return a slice from an existing partitioning
// useful to implement partitioners deriving their partitions from slices
// of another partitioning, or for incremental partitioners (returning
// a new, slightly changed version of a partitiong e.g. for load balancing)
Laik_TaskSlice* laik_partitioning_get_tslice(Laik_Partitioning* p, int n)
{
    static Laik_TaskSlice ts;

    if (n >= (int) p->slices->count) return 0;

    ts.sa = p->slices;
    ts.no = n;
    return &ts;
}

Laik_SliceArray* laik_partitioning_slices(Laik_Partitioning* p)
{
    return p->slices;
}

// public: get a custom data pointer from the partitioner
void* laik_partitioner_data(Laik_Partitioner* partitioner)
{
    return partitioner->data;
}


// free resources allocated for a partitioning object
void laik_free_partitioning(Laik_Partitioning* p)
{
    if (p->slices)
        laik_slicearray_free(p->slices);
    if (p->filter)
        laik_slicefilter_free(p->filter);
    free(p);
}


// public functions to check for assumptions an application may have
// for a partitioning as generated by an partitioner. Especially, this
// allows to check for correct implementation of self-written partitioners

// public: does this cover the full space with one slice for each process?
// such a partitioning is generated by the "laik_All" partitioner.
// (requires normalized order: task slices are sorted according to processes)
bool laik_partitioning_isAll(Laik_Partitioning* p)
{
    // no filter allowed
    assert(p->filter == 0);

    return laik_slicearray_isAll(p->slices);
}

// public:
// does this cover the full space with one slice in exactly one process?
// Such a partitioning is generated by the "laik_Master" partitioner
// Return -1 if no, else process rank
int laik_partitioning_isSingle(Laik_Partitioning* p)
{
    // no filter allowed
    assert(p->filter == 0);

    return laik_slicearray_isSingle(p->slices);
}

// do the slices of this partitioning cover the full space?
bool laik_partitioning_coversSpace(Laik_Partitioning* p)
{
    // no filter allowed
    assert(p->filter == 0);

    return laik_slicearray_coversSpace(p->slices);
}

// public: are the borders of two partitionings equal?
bool laik_partitioning_isEqual(Laik_Partitioning* p1, Laik_Partitioning* p2)
{
    // no filters allowed
    assert(p1->filter == 0);
    assert(p2->filter == 0);

    return laik_slicearray_isEqual(p1->slices, p2->slices);
}

// make partitioning valid after a partitioner run by freezing (immutable)
void laik_freeze_partitioning(Laik_Partitioning* p, bool doMerge)
{
    laik_slicearray_freeze(p->slices, doMerge);
}

void laik_partitioning_set_myfilter(Laik_Partitioning* p)
{
    assert(p->filter == 0);
    p->filter = laik_slicefilter_new();
    laik_slicefilter_set_myfilter(p->filter, p->group);
}

void laik_partitioning_add_idxfilter(Laik_Partitioning* p,
                                     Laik_Partitioning* filter)
{
    assert(filter->slices->off != 0);
    if (p->filter == 0)
        p->filter = laik_slicefilter_new();
    laik_slicefilter_add_idxfilter(p->filter, filter->slices);
}


// run a partitioner on a yet invalid, empty partitioning
void laik_run_partitioner(Laik_Partitioning* p)
{
    // has to be invalid yet
    assert(p->slices->off == 0);
    Laik_Partitioner* pr = p->partitioner;
    // must have a partitioner set
    assert(pr != 0);

    if (p->other) {
        assert(p->other->group == p->group);
        // we do not check for same space, as there are use cases
        // where you want to derive a partitioning of one space from
        // the partitioning of another
    }

    (pr->run)(pr, p, p->other);

    bool doMerge = false;
    if (pr) doMerge = (pr->flags & LAIK_PF_Merge) > 0;
    laik_freeze_partitioning(p, doMerge);

    if (laik_log_begin(1)) {
        laik_log_append("run partitioner '%s' for '%s' (group %d, myid %d, space '%s'):",
                        pr ? pr->name : "(other)", p->name,
                        p->group->gid, p->group->myid, p->space->name);
        if (p->other) {
            laik_log_append("\n  other: ");
            laik_log_Partitioning(p->other);
        }
        laik_log_append("\n  ");
        laik_log_SliceArray(p->slices);
        laik_log_flush(0);
    }
    else
        laik_log(2, "run partitioner '%s' for '%s' (group %d, space '%s'): %d slices",
                 pr ? pr->name : "(other)", p->name,
                 p->group->gid, p->space->name, p->slices->count);


    // by default, check if partitioning covers full space
    // unless partitioner has flag to not do it, or task filter is used
    bool doCoverageCheck = false;
    if (pr) doCoverageCheck = (pr->flags & LAIK_PF_NoFullCoverage) == 0;
    if (p->filter) doCoverageCheck = false;
    if (doCoverageCheck) {
        if (!laik_partitioning_coversSpace(p))
            laik_log(LAIK_LL_Panic, "partitioning borders do not cover space");
    }
}


// public: create a new partitioning by running an offline partitioner
// the partitioner may be derived from another partitioning which is
// forwarded to the partitioner algorithm
Laik_Partitioning* laik_new_partitioning(Laik_Partitioner* pr,
                                         Laik_Group* g, Laik_Space* space,
                                         Laik_Partitioning* otherP)
{
    Laik_Partitioning* p;
    p = laik_new_empty_partitioning(g, space, pr, otherP);
#if 0
    // for 1d space, default to always calculate only own slices
    if (space->dims == 1)
        laik_partitioning_set_myfilter(p);
#endif
    laik_run_partitioner(p);
    return p;
}



// migrate partitioning borders to new group without changing borders
// - added tasks get empty partitions
// - removed tasks must have empty partitiongs
void laik_partitioning_migrate(Laik_Partitioning* p, Laik_Group* newg)
{
    Laik_Group* oldg = p->group;
    int* fromOld; // mapping of IDs from old group to new group

    if (newg->parent == oldg) {
        // new group is child of old
        fromOld = newg->fromParent;
    }
    else if (oldg->parent == newg) {
        // new group is parent of old
        fromOld = oldg->toParent;
    }
    else {
        // other cases not supported
        assert(0);
    }

    laik_slicearray_migrate(p->slices, fromOld, (unsigned int) newg->size);
    p->group = newg;
}

// get number of slices for this task
int laik_my_slicecount(Laik_Partitioning* p)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);

    return laik_slicearray_tidslicecount(p->slices, myid);
}

// get number of mappings for this task
int laik_my_mapcount(Laik_Partitioning* p)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this process is not part of the process group
    assert(myid < p->group->size);

    return laik_slicearray_tidmapcount(p->slices, myid);
}

// get number of slices within a given mapping for this task
int laik_my_mapslicecount(Laik_Partitioning* p, int mapNo)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    return laik_slicearray_tidmapslicecount(p->slices, myid, mapNo);
}

// get slice number <n> from the slices for this task
Laik_TaskSlice* laik_my_slice(Laik_Partitioning* p, int n)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    return laik_slicearray_tidslice(p->slices, myid, n);
}

// get slice number <n> within mapping <mapNo> from the slices for this task
Laik_TaskSlice* laik_my_mapslice(Laik_Partitioning* p, int mapNo, int n)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    return laik_slicearray_tidmapslice(p->slices, myid, mapNo, n);
}

// get borders of slice number <n> from the 1d slices for this task
Laik_TaskSlice* laik_my_slice_1d(Laik_Partitioning* p, int n,
                                 int64_t* from, int64_t* to)
{
    assert(p->space->dims == 1);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    const Laik_Slice* s = ts ? laik_taskslice_get_slice(ts) : 0;
    if (from) *from = s ? s->from.i[0] : 0;
    if (to)   *to   = s ? s->to.i[0] : 0;
    return ts;
}

// get borders of slice number <n> from the 2d slices for this task
Laik_TaskSlice* laik_my_slice_2d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2)
{
    assert(p->space->dims == 2);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    const Laik_Slice* s = ts ? laik_taskslice_get_slice(ts) : 0;
    if (x1) *x1 = s ? s->from.i[0] : 0;
    if (x2) *x2 = s ? s->to.i[0] : 0;
    if (y1) *y1 = s ? s->from.i[1] : 0;
    if (y2) *y2 = s ? s->to.i[1] : 0;

    return ts;
}

// get borders of slice number <n> from the 3d slices for this task
Laik_TaskSlice* laik_my_slice_3d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2,
                                 int64_t* z1, int64_t* z2)
{
    assert(p->space->dims == 3);
    Laik_TaskSlice* ts = laik_my_slice(p, n);
    const Laik_Slice* s = ts ? laik_taskslice_get_slice(ts) : 0;
    if (x1) *x1 = s ? s->from.i[0] : 0;
    if (x2) *x2 = s ? s->to.i[0] : 0;
    if (y1) *y1 = s ? s->from.i[1] : 0;
    if (y2) *y2 = s ? s->to.i[1] : 0;
    if (z1) *z1 = s ? s->from.i[2] : 0;
    if (z2) *z2 = s ? s->to.i[2] : 0;

    return ts;
}

// give an access phase a name, for debug output
void laik_partitioning_set_name(Laik_Partitioning* p, char* n)
{
    p->name = strdup(n);
}
