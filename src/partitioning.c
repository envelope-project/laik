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

// add filter to only keep slices intersecting with slices in <sa> for task <tid>
void laik_slicefilter_add_idxfilter(Laik_SliceFilter* sf, Laik_SliceArray* sa, int tid)
{
    assert(sa);
    assert(sa->off != 0);
    assert(sa->space->dims == 1); // TODO: only for 1d for now

    assert((tid >= 0) && (tid < (int) sa->tid_count));
    // no own slices?
    unsigned mycount = sa->off[tid+1] - sa->off[tid];
    if (mycount == 0) return;

    Laik_TaskSlice_Gen* ts1 = sa->tslice + sa->off[tid];
    Laik_TaskSlice_Gen* ts2 = sa->tslice + sa->off[tid+1]-1;

    PFilterPar* par = malloc(sizeof(PFilterPar));
    par->len = mycount;
    par->from = ts1->s.from.i[0];
    par->to   = ts2->s.to.i[0];
    par->ts = ts1;

    if      (sf->pfilter1 == 0) sf->pfilter1 = par;
    else if (sf->pfilter2 == 0) sf->pfilter2 = par;
    else assert(0);

    laik_log(1,"Set pfilter to intersection with %d slices between [%lld;%lld[",
             par->len, (long long) par->from, (long long) par->to);

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

    // no slices stored yet
    p->saList = 0;

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

// free resources allocated for a partitioning object
void laik_free_partitioning(Laik_Partitioning* p)
{
    SliceArray_Entry* e = p->saList;
    while(e) {
        laik_slicearray_free(e->slices);
        e = e->next;
    }
    free(p);
}


// slices from a partitioner run without filter
Laik_SliceArray* laik_partitioning_allslices(Laik_Partitioning* p)
{
    SliceArray_Entry* e = p->saList;
    while(e) {
        if (e->info == LAIK_AI_FULL)
            return e->slices;
        e = e->next;
    }
    return 0;
}

// slices from a partitioner run including own slices
Laik_SliceArray* laik_partitioning_myslices(Laik_Partitioning* p)
{
    SliceArray_Entry* e = p->saList;
    while(e) {
        if (e->info == LAIK_AI_FULL) {
            // a full run also includes own slices
            return e->slices;
        }
        if ((e->info == LAIK_AI_SINGLETASK) &&
            (e->filter_tid == p->group->myid)) {
            return e->slices;
        }

        e = e->next;
    }
    return 0;
}

// slices from a run with intersection of own slices in <p> and <p2>
Laik_SliceArray* laik_partitioning_interslices(Laik_Partitioning* p,
                                               Laik_Partitioning* p2)
{
    SliceArray_Entry* e = p->saList;
    while(e) {
        if (e->info == LAIK_AI_FULL) {
            // a full run also includes wanted ones
            return e->slices;
        }
        if ((e->info == LAIK_AI_INTERSECT) &&
            (e->other == p2)) {
            return e->slices;
        }

        e = e->next;
    }
    return 0;
}


// store a slice array in partitioning, returning created entry
static
SliceArray_Entry* laik_partitioning_add_slices(Laik_Partitioning* p, Laik_SliceArray* sa)
{
    assert(p != 0);
    assert(sa != 0);

    SliceArray_Entry* e = malloc(sizeof(SliceArray_Entry));
    if (e == 0) {
        laik_panic("Out of memory allocating Laik_Partitioning object");
        exit(1); // not actually needed, laik_panic never returns
    }

    e->slices = sa;
    e->info = LAIK_AI_UNKNOWN;
    e->next = p->saList;
    p->saList = e;

    return e;
}

// internal: run partitioner given for partitioning, using given filter
// and add resulting slice array to partitioning
static
SliceArray_Entry* laik_partitioning_run(Laik_Partitioning* p, Laik_SliceFilter* sf)
{
    assert(p->partitioner != 0);

    Laik_PartitionerParams params;
    params.space       = p->space;
    params.group       = p->group;
    params.partitioner = p->partitioner;
    params.other       = p->other;

    Laik_SliceArray* sa;
    sa = laik_run_partitioner(&params, sf);

    if (laik_log_begin(2)) {
        laik_log_append("run partitioner '%s' for '%s' (group %d, space '%s'): %d slices",
                        p->partitioner->name, p->name,
                        p->group->gid, p->space->name, sa->count);
        if (sf) {
            laik_log_append("\n  using ");
            laik_log_SliceFilter(sf);
        }
        laik_log_flush(0);
    }

    return laik_partitioning_add_slices(p, sa);
}


// run the partitioner specified for the partitioning, keeping all slices
void laik_partitioning_store_allslices(Laik_Partitioning* p)
{
    SliceArray_Entry* e = laik_partitioning_run(p, 0);
    e->info = LAIK_AI_FULL;
}


// run the partitioner specified for the partitioning, keeping only slices of this task
void laik_partitioning_store_myslices(Laik_Partitioning* p)
{
    Laik_SliceFilter* sf = laik_slicefilter_new();
    laik_slicefilter_set_myfilter(sf, p->group);

    SliceArray_Entry* e = laik_partitioning_run(p, sf);
    laik_slicefilter_free(sf);

    e->info = LAIK_AI_SINGLETASK;
    e->filter_tid = p->group->myid;
}

// run the partitioner specified for the partitioning, keeping slices
// intersecting with own slices from <p2> (and own slices in <p>).
// The resulting slices are required for the transition calculation
// from <p> to <p2> in calcAddRections().
// Using these slices instead of all slices from a partitioner run
// can significantly reduce memory consumption for slice storage
void laik_partitioning_store_intersectslices(Laik_Partitioning* p,
                                             Laik_Partitioning* p2)
{
    Laik_SliceFilter* sf;
    Laik_SliceArray *sa, *sa2;

    // requires same space and same tid group
    assert(p->group == p2->group);
    assert(p->space == p2->space);

    sf = laik_slicefilter_new();
    sa = laik_partitioning_myslices(p);
    sa2 = laik_partitioning_myslices(p2);
    if ((sa == 0) || (sa2 == 0)) {
        // partitioner for own slices not run yet: but we need them!
        laik_panic("Request for intersection without base slices");
        exit(1); // not actually needed, laik_panic never returns
    }
    int myid = p->group->myid;
    laik_slicefilter_add_idxfilter(sf, sa, myid);
    if (p2 != p) // no need to add same slices twice to filter
        laik_slicefilter_add_idxfilter(sf, sa2, myid);

    SliceArray_Entry* e = laik_partitioning_run(p, sf);
    laik_slicefilter_free(sf);

    e->info = LAIK_AI_INTERSECT;
    e->other = p2;
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
    Laik_SliceArray* sa = laik_partitioning_allslices(p);
    assert(sa != 0); // TODO: API user error
    return laik_slicearray_slicecount(sa);
}

// partitioner API: return a slice from an existing partitioning
// useful to implement partitioners deriving their partitions from slices
// of another partitioning, or for incremental partitioners (returning
// a new, slightly changed version of a partitiong e.g. for load balancing)
Laik_TaskSlice* laik_partitioning_get_tslice(Laik_Partitioning* p, int n)
{
    static Laik_TaskSlice ts;

    Laik_SliceArray* sa = laik_partitioning_allslices(p);
    assert(sa != 0); // TODO: API user error

    if (n >= (int) sa->count) return 0;
    ts.sa = sa;
    ts.no = n;
    return &ts;
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
    Laik_SliceArray* sa = laik_partitioning_allslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_isAll(sa);
}

// public:
// does this cover the full space with one slice in exactly one process?
// Such a partitioning is generated by the "laik_Master" partitioner
// Return -1 if no, else process rank
int laik_partitioning_isSingle(Laik_Partitioning* p)
{
    // no filter allowed
    Laik_SliceArray* sa = laik_partitioning_allslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_isSingle(sa);
}

// do the slices of this partitioning cover the full space?
bool laik_partitioning_coversSpace(Laik_Partitioning* p)
{
    // no filter allowed
    Laik_SliceArray* sa = laik_partitioning_allslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_coversSpace(sa);
}

// public: are the borders of two partitionings equal?
bool laik_partitioning_isEqual(Laik_Partitioning* p1, Laik_Partitioning* p2)
{
    // no filters allowed
    Laik_SliceArray* sa1 = laik_partitioning_allslices(p1);
    assert(sa1 != 0); // TODO: API user error
    Laik_SliceArray* sa2 = laik_partitioning_allslices(p2);
    assert(sa2 != 0); // TODO: API user error

    return laik_slicearray_isEqual(sa1, sa2);
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
    laik_partitioning_store_allslices(p);
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

    SliceArray_Entry* e = p->saList;
    while(e) {
        if (e->info == LAIK_AI_SINGLETASK) {
            assert(e->filter_tid < oldg->size);
            e->filter_tid = fromOld[e->filter_tid];
        }
        laik_slicearray_migrate(e->slices, fromOld, (unsigned int) newg->size);
        e = e->next;
    }

    p->group = newg;
}

// get number of slices for this task
int laik_my_slicecount(Laik_Partitioning* p)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);

    Laik_SliceArray* sa = laik_partitioning_myslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_tidslicecount(sa, myid);
}

// get number of mappings for this task
int laik_my_mapcount(Laik_Partitioning* p)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this process is not part of the process group
    assert(myid < p->group->size);

    Laik_SliceArray* sa = laik_partitioning_myslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_tidmapcount(sa, myid);
}

// get number of slices within a given mapping for this task
int laik_my_mapslicecount(Laik_Partitioning* p, int mapNo)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    Laik_SliceArray* sa = laik_partitioning_myslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_tidmapslicecount(sa, myid, mapNo);
}

// get slice number <n> from the slices for this task
Laik_TaskSlice* laik_my_slice(Laik_Partitioning* p, int n)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    Laik_SliceArray* sa = laik_partitioning_myslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_tidslice(sa, myid, n);
}

// get slice number <n> within mapping <mapNo> from the slices for this task
Laik_TaskSlice* laik_my_mapslice(Laik_Partitioning* p, int mapNo, int n)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    Laik_SliceArray* sa = laik_partitioning_allslices(p);
    if (sa == 0)
        sa = laik_partitioning_myslices(p);
    assert(sa != 0); // TODO: API user error

    return laik_slicearray_tidmapslice(sa, myid, mapNo, n);
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
