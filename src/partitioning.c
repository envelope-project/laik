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

// for string.h to declare strdup
#define __STDC_WANT_LIB_EXT2__ 1

#include <assert.h>
#include <string.h>
#include <stdio.h>


/// RangeFilter

Laik_RangeFilter* laik_rangefilter_new()
{
    Laik_RangeFilter* sf;

    sf = malloc(sizeof(Laik_RangeFilter));
    if (sf == 0) {
        laik_panic("Out of memory allocating Laik_RangeFilter object");
        exit(1); // not actually needed, laik_panic never returns
    }

    // no filter set in the beginning
    sf->filter_func = 0;

    sf->filter_tid = -1;
    sf->pfilter1 = 0;
    sf->pfilter2 = 0;

    return sf;
}

void laik_rangefilter_free(Laik_RangeFilter* sf)
{
    free(sf->pfilter1);
    free(sf->pfilter2);
    free(sf);
}

// helper for laik_rangefilter_set_myfilter
static
bool tidfilter(Laik_RangeFilter* sf, int task, const Laik_Range* s)
{
    (void) s; // unused parameter of filter signature

    assert(sf->filter_tid >= 0); // only to be called with "my" filter active
    return (sf->filter_tid == task);
}

// set filter to only keep ranges for own process when adding ranges
void laik_rangefilter_set_myfilter(Laik_RangeFilter* sf, Laik_Group* g)
{
    assert(sf->filter_func == 0); // no filter set yet
    sf->filter_tid = g->myid;
    sf->filter_func = tidfilter;
}


// helper for laik_rangefilter_set_idxfilter

// check if [from;to[ intersects ranges given in par
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
    Laik_TaskRange_Gen* ts = par->ts;
    unsigned int off1 = 0, off2 = par->len;
    while(off1 < off2) {
        unsigned int mid = (off1 + off2) / 2;
        laik_log(1,"  filter check at %d: [%lld;%lld[",
                 mid,
                 (long long) ts[mid].range.from.i[0],
                 (long long) ts[mid].range.to.i[0]);

        if (from >= ts[mid].range.to.i[0]) {
            laik_log(1,"    larger, check against %d: [%lld;%lld[",
                     mid+1,
                     (long long) ts[mid+1].range.from.i[0],
                     (long long) ts[mid+1].range.to.i[0]);
            if (to <= ts[mid+1].range.from.i[0]) {
                laik_log(1,"    no intersection!");
                return false;
            }
            off1 = mid;
            continue;
        }
        if (to <= ts[mid].range.from.i[0]) {
            laik_log(1,"    smaller, check against %d: [%lld;%lld[",
                     mid-1,
                     (long long) ts[mid-1].range.from.i[0],
                     (long long) ts[mid-1].range.to.i[0]);
            if (from >= ts[mid-1].range.to.i[0]) {
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
bool idxfilter(Laik_RangeFilter* sf, int task, const Laik_Range* s)
{
    (void) task; // unused parameter of filter signature

    int64_t from = s->from.i[0];
    int64_t to = s->to.i[0];

    if (sf->pfilter1 && idxfilter_check(from, to, sf->pfilter1)) return true;
    if (sf->pfilter2 && idxfilter_check(from, to, sf->pfilter2)) return true;
    return false;
}

// add filter to only keep ranges intersecting with ranges in <list> for task <tid>
void laik_rangefilter_add_idxfilter(Laik_RangeFilter* sf, Laik_RangeList* list, int tid)
{
    assert(list);
    assert(list->off != 0);
    assert(list->space->dims == 1); // TODO: only for 1d for now

    assert((tid >= 0) && (tid < (int) list->tid_count));
    // no own ranges?
    unsigned mycount = list->off[tid+1] - list->off[tid];
    if (mycount == 0) return;

    Laik_TaskRange_Gen* ts1 = list->trange + list->off[tid];
    Laik_TaskRange_Gen* ts2 = list->trange + list->off[tid+1]-1;

    PFilterPar* par = malloc(sizeof(PFilterPar));
    par->len = mycount;
    par->from = ts1->range.from.i[0];
    par->to   = ts2->range.to.i[0];
    par->ts = ts1;

    if      (sf->pfilter1 == 0) sf->pfilter1 = par;
    else if (sf->pfilter2 == 0) sf->pfilter2 = par;
    else assert(0);

    laik_log(1,"Set pfilter to intersection with %d ranges between [%lld;%lld[",
             par->len, (long long) par->from, (long long) par->to);

    // install filter function
    sf->filter_func = idxfilter;
}




/**
 * A partitioning is a set of ranges (= consecutive ranges) into a space,
 * with each range assigned to a process from a LAIK process group.
 *
 * Partitionings are created by
 * - running a partitioner algorithm, or by
 * - reading a pre-created and stored, serialized version from memory
 * Both can be done in a distributed way. Each process of the parallel
 * application may provide only part of the full partitioning, eventually
 * followed by a global synchronization (this is not needed if every process
 * provides the full partitioning).
 *
 * Application processes iteratively ask for the ranges assigned to them.
 * Ranges are returned in a given normalized order.
 *
 * Partitioner algorithms can be offline and online: an online partitioner
 * is called whenever the application asks for the next range. In this case,
 * the partitioner is responsible for providing the ranges in an adequate
 * order, and coordinating the different called partitioner functions in the
 * parallel processes (e.g. by implementing a global range queue).
 *
 * In contrast, an offline partitioner algorithm can provide arbitrarily
 * overlapping ranges in any arbitrary order. Further, it can group
 * ranges by tagging them with group IDs. It is up to the application to make
 * use of such groups. After running the offline partitioner algorithm,
 * LAIK ensures a normalized ordering by optionally splitting/merging ranges
 * to remove overlaps and sorting them, grouping the ranges if requested.
 *
 * A deterministic offline partitioner algorithm must ensure that it returns
 * the same partitioning, given the same input (space, process group,
 * base partitioning). LAIK may call such partitioners multiple times,
 * especially in different processes to avoid communication, but also in
 * the same process to only store ranges currently needed.
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

    // no ranges stored yet
    p->rangeList = 0;

    p->other = other;

    return p;
}




/**
 * Create an empty, invalid partitioning on a given space for a given
 * process group.
 *
 * To make the partitioning valid, either a partitioning algorithm has to be
 * run, filling the partitioning with ranges, or a partitioner algorithm
 * is set to be run whenever partitioning ranges are to be consumed (online).
 *
 * Different internal formats are used depending on how a partitioner adds
 * indexes. E.g. adding single indexes are managed differently than ranges.
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
    RangeList_Entry* e = p->rangeList;
    while(e) {
        laik_rangelist_free(e->ranges);
        e = e->next;
    }
    free(p);
}


// ranges from a partitioner run without filter
Laik_RangeList* laik_partitioning_allranges(Laik_Partitioning* p)
{
    RangeList_Entry* e = p->rangeList;
    while(e) {
        if (e->info == LAIK_RI_FULL)
            return e->ranges;
        e = e->next;
    }
    return 0;
}

// ranges from a partitioner run including own ranges
Laik_RangeList* laik_partitioning_myranges(Laik_Partitioning* p)
{
    RangeList_Entry* e = p->rangeList;
    while(e) {
        if (e->info == LAIK_RI_FULL) {
            // a full run also includes own ranges
            return e->ranges;
        }
        if ((e->info == LAIK_RI_SINGLETASK) &&
            (e->filter_tid == p->group->myid)) {
            return e->ranges;
        }

        e = e->next;
    }
    return 0;
}

// ranges from a run with intersection of own ranges in <p> and <p2>
Laik_RangeList* laik_partitioning_interranges(Laik_Partitioning* p,
                                               Laik_Partitioning* p2)
{
    // it is a programmer error if <p> and <p2> work on different spaces
    if (p->space != p2->space) {
        laik_panic("Requesting intersection between different spaces");
        exit(1); // not actually needed, laik_panic never returns
    }

    RangeList_Entry* e = p->rangeList;
    while(e) {
        if (e->info == LAIK_RI_FULL) {
            // a full run also includes wanted ones
            return e->ranges;
        }
        if ((e->info == LAIK_RI_INTERSECT) &&
            (e->other == p2)) {
            return e->ranges;
        }

        e = e->next;
    }
    return 0;
}


// store a range list in partitioning, returning created entry
static
RangeList_Entry* laik_partitioning_add_ranges(Laik_Partitioning* p, Laik_RangeList* list)
{
    assert(p != 0);
    assert(list != 0);

    RangeList_Entry* e = malloc(sizeof(RangeList_Entry));
    if (e == 0) {
        laik_panic("Out of memory allocating Laik_Partitioning object");
        exit(1); // not actually needed, laik_panic never returns
    }

    e->ranges = list;
    e->info = LAIK_RI_UNKNOWN;
    e->next = p->rangeList;
    p->rangeList = e;

    return e;
}

// internal: run partitioner given for partitioning, using given filter
// and add resulting range list to partitioning
static
RangeList_Entry* laik_partitioning_run(Laik_Partitioning* p, Laik_RangeFilter* sf)
{
    assert(p->partitioner != 0);

    Laik_PartitionerParams params;
    params.space       = p->space;
    params.group       = p->group;
    params.partitioner = p->partitioner;
    params.other       = p->other;

    Laik_RangeList* list;
    list = laik_run_partitioner(&params, sf);

    if (laik_log_begin(2)) {
        laik_log_append("run partitioner '%s' for '%s' (group %d, space '%s'): %d ranges",
                        p->partitioner->name, p->name,
                        p->group->gid, p->space->name, list->count);
        if (sf) {
            laik_log_append("\n  using ");
            laik_log_RangeFilter(sf);
        }
        laik_log_flush(0);
    }

    return laik_partitioning_add_ranges(p, list);
}


// run the partitioner specified for the partitioning, keeping all ranges
void laik_partitioning_store_allranges(Laik_Partitioning* p)
{
    RangeList_Entry* e = laik_partitioning_run(p, 0);
    e->info = LAIK_RI_FULL;
}


// run the partitioner specified for the partitioning, keeping only ranges of this task
void laik_partitioning_store_myranges(Laik_Partitioning* p)
{
    if (p->group->myid < 0) return;

    Laik_RangeFilter* sf = laik_rangefilter_new();
    laik_rangefilter_set_myfilter(sf, p->group);

    RangeList_Entry* e = laik_partitioning_run(p, sf);
    laik_rangefilter_free(sf);

    e->info = LAIK_RI_SINGLETASK;
    e->filter_tid = p->group->myid;
}

// run the partitioner specified for the partitioning, keeping ranges
// intersecting with own ranges from <p2> (and own ranges in <p>).
// The resulting ranges are required for the transition calculation
// from <p> to <p2> in calcAddRections().
// Using these ranges instead of all ranges from a partitioner run
// can significantly reduce memory consumption for range storage
void laik_partitioning_store_intersectranges(Laik_Partitioning* p,
                                             Laik_Partitioning* p2)
{
    Laik_RangeFilter* sf;
    Laik_RangeList *list, *list2;

    // it is a programmer error if <p> and <p2> work on different spaces
    // (different groups are Ok, as the installed filters works on indexes)
    if (p->space != p2->space) {
        laik_panic("Requesting intersection filter on different spaces");
        exit(1); // not actually needed, laik_panic never returns
    }

    sf = laik_rangefilter_new();
    list = laik_partitioning_myranges(p);
    list2 = laik_partitioning_myranges(p2);
    if ((list == 0) || (list2 == 0)) {
        // partitioner for own ranges not run yet: but we need them!
        laik_panic("Request for intersection without base ranges");
        exit(1); // not actually needed, laik_panic never returns
    }
    laik_rangefilter_add_idxfilter(sf, list, p->group->myid);
    if (p2 != p) // no need to add same ranges twice to filter
        laik_rangefilter_add_idxfilter(sf, list2, p2->group->myid);

    RangeList_Entry* e = laik_partitioning_run(p, sf);
    laik_rangefilter_free(sf);

    e->info = LAIK_RI_INTERSECT;
    e->other = p2;
}


// public: return the space a partitioning is used for
Laik_Space* laik_partitioning_get_space(Laik_Partitioning* p)
{
    return p->space;
}

// public: return the process group a partitioning is used for
// ranges are associated to processes using their rank in the process group
Laik_Group* laik_partitioning_get_group(Laik_Partitioning* p)
{
    return p->group;
}

// public/partitioner API: total number of ranges in this partitioning
// only allowed for offline partitioners, may be expensive
int laik_partitioning_rangecount(Laik_Partitioning* p)
{
    Laik_RangeList* list = laik_partitioning_allranges(p);
    assert(list != 0); // TODO: API user error
    return laik_rangelist_rangecount(list);
}

// partitioner API: return a range from an existing partitioning
// useful to implement partitioners deriving their partitions from ranges
// of another partitioning, or for incremental partitioners (returning
// a new, slightly changed version of a partitiong e.g. for load balancing)
Laik_TaskRange* laik_partitioning_get_taskrange(Laik_Partitioning* p, int n)
{
    static Laik_TaskRange ts;

    Laik_RangeList* list = laik_partitioning_allranges(p);
    assert(list != 0); // TODO: API user error

    if (n >= (int) list->count) return 0;
    ts.list = list;
    ts.no = n;
    return &ts;
}



// public functions to check for assumptions an application may have
// for a partitioning as generated by an partitioner. Especially, this
// allows to check for correct implementation of self-written partitioners

// public: does this cover the full space with one range for each process?
// such a partitioning is generated by the "laik_All" partitioner.
// (requires normalized order: task ranges are sorted according to processes)
bool laik_partitioning_isAll(Laik_Partitioning* p)
{
    // no filter allowed
    Laik_RangeList* list = laik_partitioning_allranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_isAll(list);
}

// public:
// does this cover the full space with one range in exactly one process?
// Such a partitioning is generated by the "laik_Master" partitioner
// Return -1 if no, else process rank
int laik_partitioning_isSingle(Laik_Partitioning* p)
{
    // no filter allowed
    Laik_RangeList* list = laik_partitioning_allranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_isSingle(list);
}

// do the ranges of this partitioning cover the full space?
bool laik_partitioning_coversSpace(Laik_Partitioning* p)
{
    // no filter allowed
    Laik_RangeList* list = laik_partitioning_allranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_coversSpace(list);
}

// public: are the borders of two partitionings equal?
bool laik_partitioning_isEqual(Laik_Partitioning* p1, Laik_Partitioning* p2)
{
    // no filters allowed
    Laik_RangeList* sa1 = laik_partitioning_allranges(p1);
    assert(sa1 != 0); // TODO: API user error
    Laik_RangeList* sa2 = laik_partitioning_allranges(p2);
    assert(sa2 != 0); // TODO: API user error

    return laik_rangelist_isEqual(sa1, sa2);
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
    laik_partitioning_store_allranges(p);
    return p;
}



// migrate partitioning borders to new group without changing borders
// - added tasks get empty partitions
// - removed tasks must have empty partitiongs
void laik_partitioning_migrate(Laik_Partitioning* p, Laik_Group* newg)
{
    Laik_Group* oldg = p->group;
    if (oldg == newg) return;

    int* fromOld; // mapping of IDs from old group to new group

    if (newg->parent == oldg) {
        // new group is child of old
        fromOld = newg->fromParent;
    }
    else if (newg->parent2 == oldg) {
        // new group is child of old
        fromOld = newg->fromParent2;
    }
    else if (oldg->parent == newg) {
        // new group is parent of old
        fromOld = oldg->toParent;
    }
    else if (oldg->parent2 == newg) {
        // new group is parent of old
        fromOld = oldg->toParent2;
    }
    else {
        // other cases not supported
        assert(0);
    }

    RangeList_Entry* e = p->rangeList;
    while(e) {
        if (e->info == LAIK_RI_SINGLETASK) {
            assert(e->filter_tid < oldg->size);
            e->filter_tid = fromOld[e->filter_tid];
        }
        laik_rangelist_migrate(e->ranges, fromOld, (unsigned int) newg->size);
        e = e->next;
    }

    p->group = newg;
}

// get number of ranges for own process
int laik_my_rangecount(Laik_Partitioning* p)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group
    assert(myid < p->group->size);

    Laik_RangeList* list = laik_partitioning_myranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_tidrangecount(list, myid);
}

// get number of mappings for this task
int laik_my_mapcount(Laik_Partitioning* p)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this process is not part of the process group
    assert(myid < p->group->size);

    Laik_RangeList* list = laik_partitioning_myranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_tidmapcount(list, myid);
}

// get number of ranges within a given mapping for this task
int laik_my_maprangecount(Laik_Partitioning* p, int mapNo)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    Laik_RangeList* list = laik_partitioning_myranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_tidmaprangecount(list, myid, mapNo);
}

// get range number <n> from ranges for own process
Laik_TaskRange* laik_my_range(Laik_Partitioning* p, int n)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this task is not part of task group

    Laik_RangeList* list = laik_partitioning_myranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_tidrange(list, myid, n);
}

// get range number <n> within mapping <mapNo> from the ranges for own process
Laik_TaskRange* laik_my_maprange(Laik_Partitioning* p, int mapNo, int n)
{
    int myid = p->group->myid;
    if (myid < 0) return 0; // this process is not part of process group

    Laik_RangeList* list = laik_partitioning_allranges(p);
    if (list == 0)
        list = laik_partitioning_myranges(p);
    assert(list != 0); // TODO: API user error

    return laik_rangelist_tidmaprange(list, myid, mapNo, n);
}

// get borders of range number <n> from the 1d ranges for this task
Laik_TaskRange* laik_my_range_1d(Laik_Partitioning* p, int n,
                                 int64_t* from, int64_t* to)
{
    assert(p->space->dims == 1);
    Laik_TaskRange* ts = laik_my_range(p, n);
    const Laik_Range* s = ts ? laik_taskrange_get_range(ts) : 0;
    if (from) *from = s ? s->from.i[0] : 0;
    if (to)   *to   = s ? s->to.i[0] : 0;
    return ts;
}

// get borders of range number <n> from the 2d ranges for this task
Laik_TaskRange* laik_my_range_2d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2)
{
    assert(p->space->dims == 2);
    Laik_TaskRange* ts = laik_my_range(p, n);
    const Laik_Range* s = ts ? laik_taskrange_get_range(ts) : 0;
    if (x1) *x1 = s ? s->from.i[0] : 0;
    if (x2) *x2 = s ? s->to.i[0] : 0;
    if (y1) *y1 = s ? s->from.i[1] : 0;
    if (y2) *y2 = s ? s->to.i[1] : 0;

    return ts;
}

// get borders of range number <n> from the 3d ranges for this task
Laik_TaskRange* laik_my_range_3d(Laik_Partitioning* p, int n,
                                 int64_t* x1, int64_t* x2,
                                 int64_t* y1, int64_t* y2,
                                 int64_t* z1, int64_t* z2)
{
    assert(p->space->dims == 3);
    Laik_TaskRange* ts = laik_my_range(p, n);
    const Laik_Range* s = ts ? laik_taskrange_get_range(ts) : 0;
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
