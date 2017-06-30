/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 */

#include "laik-internal.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// counter for space ID, just for debugging
static int space_id = 0;

// counter for partitioning ID, just for debugging
static int part_id = 0;

// helpers

void setIndex(Laik_Index* i, uint64_t i1, uint64_t i2, uint64_t i3)
{
    i->i[0] = i1;
    i->i[1] = i2;
    i->i[2] = i3;
}

static
int getSpaceStr(char* s, Laik_Space* spc)
{
    switch(spc->dims) {
    case 1:
        return sprintf(s, "[0-%lu]", spc->size[0]-1);
    case 2:
        return sprintf(s, "[0-%lu/0-%lu]",
                       spc->size[0]-1, spc->size[1]-1);
    case 3:
        return sprintf(s, "[0-%lu/0-%lu/0-%lu]",
                       spc->size[0]-1, spc->size[1]-1, spc->size[2]-1);
    }
    return 0;
}


int laik_getIndexStr(char* s, int dims, Laik_Index* idx, bool minus1)
{
    uint64_t i1 = idx->i[0];
    uint64_t i2 = idx->i[1];
    uint64_t i3 = idx->i[2];
    if (minus1) {
        i1--;
        i2--;
        i3--;
    }

    switch(dims) {
    case 1:
        return sprintf(s, "%lu", i1);
    case 2:
        return sprintf(s, "%lu/%lu", i1, i2);
    case 3:
        return sprintf(s, "%lu/%lu/%lu", i1, i2, i3);
    }
    return 0;
}

// is the given slice empty?
bool laik_slice_isEmpty(int dims, Laik_Slice* slc)
{
    if (slc->from.i[0] >= slc->to.i[0])
        return true;

    if (dims>1) {
        if (slc->from.i[1] >= slc->to.i[1])
            return true;

        if (dims>2) {
            if (slc->from.i[2] >= slc->to.i[2])
                return true;
        }
    }
    return false;
}


// returns false if intersection of ranges is empty
static
bool intersectRange(uint64_t from1, uint64_t to1, uint64_t from2, uint64_t to2,
                    uint64_t* resFrom, uint64_t* resTo)
{
    if (from1 >= to2) return false;
    if (from2 >= to1) return false;
    *resFrom = (from1 > from2) ? from1 : from2;
    *resTo = (to1 > to2) ? to2 : to1;
    return true;
}

// get the intersection of 2 slices; return 0 if intersection is empty
Laik_Slice* laik_slice_intersect(int dims, Laik_Slice* s1, Laik_Slice* s2)
{
    static Laik_Slice s;

    if (!intersectRange(s1->from.i[0], s1->to.i[0],
                        s2->from.i[0], s2->to.i[0],
                        &(s.from.i[0]), &(s.to.i[0])) ) return 0;
    if (dims>1) {
        if (!intersectRange(s1->from.i[1], s1->to.i[1],
                            s2->from.i[1], s2->to.i[1],
                            &(s.from.i[1]), &(s.to.i[1])) ) return 0;
        if (dims>2) {
            if (!intersectRange(s1->from.i[2], s1->to.i[2],
                                s2->from.i[2], s2->to.i[2],
                                &(s.from.i[2]), &(s.to.i[2])) ) return 0;
        }
    }
    return &s;
}

static
void laik_slice_sub(int dims, Laik_Slice* s, Laik_Index* from)
{
    s->from.i[0] -= from->i[0];
    s->to.i[0] -= from->i[0];
    if (dims > 1) {
        s->from.i[1] -= from->i[1];
        s->to.i[1] -= from->i[1];
        if (dims > 2) {
            s->from.i[2] -= from->i[2];
            s->to.i[2] -= from->i[2];
        }
    }
}

static
Laik_Slice* sliceFromSpace(Laik_Space* s)
{
    static Laik_Slice slc;

    slc.from.i[0] = 0;
    slc.from.i[1] = 0;
    slc.from.i[2] = 0;
    slc.to.i[0] = s->size[0];
    slc.to.i[1] = s->size[1];
    slc.to.i[2] = s->size[2];

    return &slc;
}

static
int getSliceStr(char* s, int dims, Laik_Slice* slc)
{
    if (laik_slice_isEmpty(dims, slc))
        return sprintf(s, "(empty)");

    int off;
    off  = sprintf(s, "[");
    off += laik_getIndexStr(s+off, dims, &(slc->from), false);
    off += sprintf(s+off, "-");
    off += laik_getIndexStr(s+off, dims, &(slc->to), true);
    off += sprintf(s+off, "]");
    return off;
}


static
int getPartitioningTypeStr(char* s, Laik_PartitionType type)
{
    switch(type) {
    case LAIK_PT_All:    return sprintf(s, "all");
    case LAIK_PT_Block:  return sprintf(s, "block");
    case LAIK_PT_Master: return sprintf(s, "master");
    case LAIK_PT_Copy:   return sprintf(s, "copy");
    default: assert(0);
    }
    return 0;
}

static
int getReductionStr(char* s, Laik_ReductionOperation op)
{
    switch(op) {
    case LAIK_RO_None: return sprintf(s, "none");
    case LAIK_RO_Sum:  return sprintf(s, "sum");
    default: assert(0);
    }
    return 0;
}


static
int getDataFlowStr(char* s, Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_None:                return sprintf(s, "none");
    case LAIK_DF_CopyIn_NoOut:        return sprintf(s, "copyin");
    case LAIK_DF_NoIn_CopyOut:        return sprintf(s, "copyout");
    case LAIK_DF_CopyIn_CopyOut:      return sprintf(s, "copyinout");
    case LAIK_DF_NoIn_SumReduceOut:   return sprintf(s, "sumout");
    case LAIK_DF_InitIn_SumReduceOut: return sprintf(s, "init-sumout");
    default: assert(0);
    }
    return 0;
}


static
int getTransitionStr(char* s, Laik_Transition* t)
{
    int off = 0;

    if (t->localCount>0) {
        off += sprintf(s+off, "  local: ");
        for(int i=0; i<t->localCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getSliceStr(s+off, t->dims, &(t->local[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->initCount>0) {
        off += sprintf(s+off, "  init: ");
        for(int i=0; i<t->initCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getReductionStr(s+off, t->init[i].redOp);
            off += getSliceStr(s+off, t->dims, &(t->init[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->sendCount>0) {
        off += sprintf(s+off, "  send: ");
        for(int i=0; i<t->sendCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getSliceStr(s+off, t->dims, &(t->send[i].slc));
            off += sprintf(s+off, " => T%d", t->send[i].toTask);
        }
        off += sprintf(s+off, "\n");
    }

    if (t->recvCount>0) {
        off += sprintf(s+off, "  recv: ");
        for(int i=0; i<t->recvCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += sprintf(s+off, "T%d => ", t->recv[i].fromTask);
            off += getSliceStr(s+off, t->dims, &(t->recv[i].slc));
        }
        off += sprintf(s+off, "\n");
    }

    if (t->redCount>0) {
        off += sprintf(s+off, "  reduction: ");
        for(int i=0; i<t->redCount; i++) {
            if (i>0) off += sprintf(s+off, ", ");
            off += getReductionStr(s+off, t->red[i].redOp);
            off += getSliceStr(s+off, t->dims, &(t->red[i].slc));
            off += sprintf(s+off, " => %s (%d)",
                           (t->red[i].rootTask == -1) ? "all":"master",
                           t->red[i].rootTask);
        }
        off += sprintf(s+off, "\n");
    }

    if (off == 0) s[0] = 0;
    return off;
}

// is this a reduction?
bool laik_is_reduction(Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_NoIn_SumReduceOut:
    case LAIK_DF_InitIn_SumReduceOut:
        return true;
    default:
        break;
    }
    return false;
}

// return the reduction operation from data flow behavior
Laik_ReductionOperation laik_get_reduction(Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_NoIn_SumReduceOut:
    case LAIK_DF_InitIn_SumReduceOut:
        return LAIK_RO_Sum;
    default:
        break;
    }
    return LAIK_RO_None;
}

// do we need to copy values in?
bool laik_do_copyin(Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_CopyIn_NoOut:
    case LAIK_DF_CopyIn_CopyOut:
        return true;
    default:
        break;
    }
    return false;
}

// do we need to copy values out?
bool laik_do_copyout(Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_NoIn_CopyOut:
    case LAIK_DF_CopyIn_CopyOut:
        return true;
    default:
        break;
    }
    return false;
}

// do we need to init values?
bool laik_do_init(Laik_DataFlow flow)
{
    switch(flow) {
    case LAIK_DF_InitIn_SumReduceOut:
        return true;
    default:
        break;
    }
    return false;
}


// create a new index space object (initially invalid)
Laik_Space* laik_new_space(Laik_Instance* i)
{
    Laik_Space* space = (Laik_Space*) malloc(sizeof(Laik_Space));

    space->id = space_id++;
    space->name = strdup("space-0     ");
    sprintf(space->name, "space-%d", space->id);

    space->inst = i;
    space->dims = 0; // invalid
    space->first_partitioning = 0;

    // append this space to list of spaces used by LAIK instance
    space->next = i->firstspace;
    i->firstspace = space;

    return space;
}

// create a new index space object with an initial size
Laik_Space* laik_new_space_1d(Laik_Instance* i, uint64_t s1)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 1;
    space->size[0] = s1;

    if (laik_logshown(1)) {
        char s[100];
        getSpaceStr(s, space);
        laik_log(1, "new 1d space '%s': %s\n", space->name, s);
    }

    return space;
}

Laik_Space* laik_new_space_2d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 2;
    space->size[0] = s1;
    space->size[1] = s2;

    if (laik_logshown(1)) {
        char s[100];
        getSpaceStr(s, space);
        laik_log(1, "new 2d space '%s': %s\n", space->name, s);
    }

    return space;
}

Laik_Space* laik_new_space_3d(Laik_Instance* i,
                              uint64_t s1, uint64_t s2, uint64_t s3)
{
    Laik_Space* space = laik_new_space(i);
    space->dims = 3;
    space->size[0] = s1;
    space->size[1] = s2;
    space->size[2] = s3;

    if (laik_logshown(1)) {
        char s[100];
        getSpaceStr(s, space);
        laik_log(1, "new 3d space '%s': %s\n", space->name, s);
    }

    return space;
}

// free a space with all resources depending on it (e.g. paritionings)
void laik_free_space(Laik_Space* s)
{
    free(s->name);

    // TODO
}

// set a space a name, for debug output
void laik_set_space_name(Laik_Space* s, char* n)
{
    s->name = strdup(n);
}

// change the size of an index space, eventually triggering a repartitiong
void laik_change_space_1d(Laik_Space* s, uint64_t s1)
{
    assert(s->dims == 1);
    if (s->size[0] == s1) return;

    s->size[0] = s1;

    // TODO: notify partitionings about space change
}

void laik_change_space_2d(Laik_Space* s,
                          uint64_t s1, uint64_t s2)
{
    assert(0); // TODO
}

void laik_change_space_3d(Laik_Space* s,
                          uint64_t s1, uint64_t s2, uint64_t s3)
{
    assert(0); // TODO
}


// create a new partitioning on a space
Laik_Partitioning* laik_new_partitioning(Laik_Space* s)
{
    Laik_Partitioning* p;
    p = (Laik_Partitioning*) malloc(sizeof(Laik_Partitioning));

    p->id = part_id++;
    p->name = strdup("partng-0     ");
    sprintf(p->name, "partng-%d", p->id);

    p->group = laik_world(s->inst);
    p->space = s;
    p->pdim = 0;
    p->next = s->first_partitioning;
    s->first_partitioning = p;

    p->flow = LAIK_DF_Invalid;
    p->type = LAIK_PT_None;
    p->copyIn = false;
    p->copyOut = false;
    p->redOp = LAIK_RO_None;

    p->partitioner = 0;
    p->base = 0;

    p->bordersValid = false;
    p->borderOff = 0;
    p->borders = 0;

    return p;
}

static
void set_flow(Laik_Partitioning* p, Laik_DataFlow flow)
{
    p->flow = flow;
    p->copyIn = laik_do_copyin(flow);
    p->copyOut = laik_do_copyout(flow);
    p->redOp = laik_get_reduction(flow);
}

Laik_Partitioning*
laik_new_base_partitioning(Laik_Space* space,
                           Laik_PartitionType pt,
                           Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(space);
    p->type = pt;
    set_flow(p, flow);

    if (laik_logshown(1)) {
        char s[100];
        getPartitioningTypeStr(s, p->type);
        getDataFlowStr(s+50, p->flow);
        laik_log(1, "new partitioning '%s': type %s, data flow %s, group %d\n",
                 p->name, s, s+50, p->group->gid);
    }

    return p;
}

Laik_Partitioner* laik_get_partitioner(Laik_Partitioning* p)
{
    if (!p->partitioner) {
        switch(p->type) {
        case LAIK_PT_Block:
            p->partitioner = laik_newBlockPartitioner(p);
            break;
        }
    }
    return p->partitioner;
}

void laik_set_partitioner(Laik_Partitioning* p, Laik_Partitioner* pr)
{
    assert(pr->type != LAIK_PT_None);
    p->type = pr->type;
    p->partitioner = pr;
}



// for multiple-dimensional spaces, set dimension to partition (default is 0)
void laik_set_partitioning_dimension(Laik_Partitioning* p, int d)
{
    assert((d >= 0) && (d < p->space->dims));
    p->pdim = d;
}


// create a new partitioning based on another one on the same space
Laik_Partitioning*
laik_new_coupled_partitioning(Laik_Partitioning* base,
                              Laik_PartitionType pt,
                              Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(base->space);
    p->type = pt;
    p->base = base;
    set_flow(p, flow);

    return p;
}

// create a new partitioning based on another one on a different space
// this also needs to know which dimensions should be coupled
Laik_Partitioning*
laik_new_spacecoupled_partitioning(Laik_Partitioning* base,
                                   Laik_Space* s, int from, int to,
                                   Laik_PartitionType pt,
                                   Laik_DataFlow flow)
{
    Laik_Partitioning* p;
    p = laik_new_partitioning(p->space);
    p->type = pt;
    p->base = base;
    set_flow(p, flow);

    assert(0); // TODO

    return p;
}

// free a partitioning with related resources
void laik_free_partitioning(Laik_Partitioning* p)
{
    // FIXME: we need some kind of reference counting/GC here

    //free(p->name);
    // TODO
}

// get number of slices of this task
int laik_my_slicecount(Laik_Partitioning* p)
{
    laik_update_partitioning(p);

    int myid = p->group->myid;
    return p->borderOff[myid+1] - p->borderOff[myid];
}

// get slice number <n> from the slices of this task
Laik_Slice* laik_my_slice(Laik_Partitioning* p, int n)
{
    static Laik_Slice s;

    laik_update_partitioning(p);

    int myid = p->group->myid;
    int o = p->borderOff[myid] + n;
    if (o >= p->borderOff[myid+1]) {
        // slice <n> is invalid
        return 0;
    }
    s = p->borders[o];
    return &s;
}


// give a partitioning a name, for debug output
void laik_set_partitioning_name(Laik_Partitioning* p, char* n)
{
    p->name = strdup(n);
}



// make sure partitioning borders are up to date
// returns true on changes (if borders had to be updated)
bool laik_update_partitioning(Laik_Partitioning* p)
{
    Laik_Slice* baseBorders = 0;
    Laik_Space* s = p->space;
    int pdim = p->pdim;
    int basepdim;

    if (p->base) {
        if (laik_update_partitioning(p->base))
            p->bordersValid = false;

        baseBorders = p->base->borders;
        basepdim = p->base->pdim;
        // sizes of coupled dimensions should be equal
        assert(s->size[pdim] == p->base->space->size[basepdim]);
    }

    if (p->bordersValid)
        return false;

    int count = p->group->size;
    if (!p->borders) {
        // initialize borderOffsets: each task has one slice
        p->borderOff = (int*) malloc((count+1) * sizeof(int));
        for(int i = 0; i <= count; i++)
            p->borderOff[i] = i;

        p->borders = (Laik_Slice*) malloc(count * sizeof(Laik_Slice));
    }

    // init to all space indexes first
    for(int task = 0; task < count; task++) {
        Laik_Slice* b = &(p->borders[task]);
        setIndex(&(b->from), 0, 0, 0);
        setIndex(&(b->to), s->size[0], s->size[1], s->size[2]);
    }

    // may trigger creation of partitioner object
    Laik_Partitioner* pr = laik_get_partitioner(p);
    if (pr)
        (pr->run)(pr);
    else {
        switch(p->type) {
        case LAIK_PT_All:
            // init was fine, nothing to do
            break;

        case LAIK_PT_Master:
            // set partitions for non-masters to empty
            for(int task = 1; task < count; task++) {
                Laik_Slice* b = &(p->borders[task]);
                setIndex(&(b->to), 0, 0, 0);
            }
            break;

        case LAIK_PT_Copy:
            assert(baseBorders);
            for(int task = 0; task < count; task++) {
                Laik_Slice* b = &(p->borders[task]);

                b->from.i[pdim] = baseBorders[task].from.i[basepdim];
                b->to.i[pdim] = baseBorders[task].to.i[basepdim];
            }
            break;

        default:
            assert(0); // TODO
            break;
        }
    }

    p->bordersValid = true;

    if (laik_logshown(1)) {
        char str[1000];
        int off;
        off = sprintf(str, "partitioning '%s' (group %d) updated: ",
                      p->name, p->group->gid);
        for(int task = 0; task < count; task++) {
            if (task>0)
                off += sprintf(str+off, ", ");
            off += sprintf(str+off, "%d:", task);
            off += getSliceStr(str+off, p->space->dims, &(p->borders[task]));
        }
        laik_log(1, "%s\n", str);
    }

    return true;
}



// append a partitioning to a partioning group whose consistency should
// be enforced at the same point in time
void laik_append_partitioning(Laik_PartGroup* g, Laik_Partitioning* p)
{
    assert(0); // TODO
}

// Calculate communication required for transitioning between partitionings
Laik_Transition* laik_calc_transitionP(Laik_Partitioning* from,
                                       Laik_Partitioning* to)
{
    Laik_Slice* slc;
    Laik_Transition* t;

    t = (Laik_Transition*) malloc(sizeof(Laik_Transition));
    t->localCount = 0;
    t->initCount = 0;
    t->sendCount = 0;
    t->recvCount = 0;
    t->redCount = 0;


    // either one of <from> and <to> has to be valid; same space, same group
    Laik_Space* space = 0;
    Laik_Group* group = 0;

    // make sure requested data flow is consistent
    if (from == 0) {
        // start: we come from nothing, go to initial partitioning
        assert(to != 0);
        assert(!laik_do_copyin(to->flow));

        space = to->space;
        group = to->group;
    }
    else if (to == 0) {
        // end: go to nothing
        assert(from != 0);
        assert(!laik_do_copyout(from->flow));

        space = from->space;
        group = from->group;
    }
    else {
        // to and from set
        if (laik_do_copyin(to->flow)) {
            // values must come from something
            assert(laik_do_copyout(from->flow) ||
                   laik_is_reduction(from->flow));
        }
        assert(from->space == to->space);
        assert(from->group == to->group);
        space = from->space;
        group = from->group;
    }

    int dims = space->dims;
    int myid = group->inst->myid;
    int count = group->size;
    t->dims = dims;

    // init values as next phase does a reduction?
    if ((to != 0) && laik_is_reduction(to->flow)) {

        for(int o = to->borderOff[myid]; o < to->borderOff[myid+1]; o++) {
            if (laik_slice_isEmpty(dims, &(to->borders[o]))) continue;
            assert(t->initCount < TRANSSLICES_MAX);
            assert(to->redOp != LAIK_RO_None);
            struct initTOp* op = &(t->init[t->initCount]);
            op->slc = to->borders[o];
            op->sliceNo = o - to->borderOff[myid];
            op->redOp = to->redOp;
            t->initCount++;
        }
    }

    if ((from != 0) && (to != 0)) {

        // determine local slices to keep
        // (may need local copy if from/to mappings are different).
        // reductions are not handled here, but by backend
        if (laik_do_copyout(from->flow) && laik_do_copyin(to->flow)) {
            for(int o1 = from->borderOff[myid]; o1 < from->borderOff[myid+1]; o1++) {
                for(int o2 = to->borderOff[myid]; o2 < to->borderOff[myid+1]; o2++) {
                    slc = laik_slice_intersect(dims,
                                               &(from->borders[o1]),
                                               &(to->borders[o2]));
                    if (slc == 0) continue;

                    assert(t->localCount < TRANSSLICES_MAX);
                    struct localTOp* op = &(t->local[t->localCount]);
                    op->slc = *slc;
                    op->fromSliceNo = o1 - from->borderOff[myid];
                    op->toSliceNo = o2 - to->borderOff[myid];

                    t->localCount++;
                }
            }
        }

        // something to reduce?
        if (laik_is_reduction(from->flow)) {
            // reductions always should involve everyone
            assert(from->type == LAIK_PT_All);
            if (laik_do_copyin(to->flow)) {
                assert(t->redCount < TRANSSLICES_MAX);
                assert((to->type == LAIK_PT_Master) ||
                       (to->type == LAIK_PT_All));

                struct redTOp* op = &(t->red[t->redCount]);
                op->slc = *sliceFromSpace(from->space); // complete space
                op->redOp = from->redOp;
                op->rootTask = (to->type == LAIK_PT_All) ? -1 : 0;

                t->redCount++;
            }
        }

        // something to send?
        if (laik_do_copyout(from->flow)) {
            for(int o = from->borderOff[myid]; o < from->borderOff[myid+1]; o++) {
                if (laik_slice_isEmpty(dims, &(from->borders[o]))) continue;
                for(int task = 0; task < count; task++) {
                    if (task == myid) continue;

                    slc = laik_slice_intersect(dims,
                                               &(from->borders[o]),
                                               &(to->borders[task]));
                    if (slc == 0) continue;

                    assert(t->sendCount < TRANSSLICES_MAX);
                    struct sendTOp* op = &(t->send[t->sendCount]);
                    op->slc = *slc;
                    op->sliceNo = o - from->borderOff[myid];
                    op->toTask = task;
                    t->sendCount++;
                }
            }
        }

        // something to receive not coming from a reduction?
        if (!laik_is_reduction(from->flow) && laik_do_copyin(to->flow)) {
            for(int o = to->borderOff[myid]; o < to->borderOff[myid+1]; o++) {
                if (laik_slice_isEmpty(dims, &(to->borders[o]))) continue;
                for(int task = 0; task < count; task++) {
                    if (task == myid) continue;

                    slc = laik_slice_intersect(dims,
                                               &(to->borders[o]),
                                               &(from->borders[task]));
                    if (slc == 0) continue;

                    assert(t->recvCount < TRANSSLICES_MAX);
                    struct recvTOp* op = &(t->recv[t->recvCount]);
                    op->slc = *slc;
                    op->sliceNo = o - to->borderOff[myid];
                    op->fromTask = task;
                    t->recvCount++;
                }
            }
        }
    }

    if (laik_logshown(1)) {
        char s[1000];
        int len = getTransitionStr(s, t);
        if (len == 0)
            laik_log(1, "transition %s => %s: (nothing)\n",
                     from ? from->name : "(none)", to ? to->name : "(none)");
        else
            laik_log(1, "transition %s => %s:\n%s",
                     from ? from->name : "(none)", to ? to->name : "(none)",
                     s);
    }

    return t;
}

// Calculate communication for transitioning between partitioning groups
Laik_Transition* laik_calc_transitionG(Laik_PartGroup* from,
                                       Laik_PartGroup* to)
{
    Laik_Transition* t;

    assert(0); // TODO
}

// enforce consistency for the partitioning group, depending on previous
void laik_enforce_consistency(Laik_Instance* i, Laik_PartGroup* g)
{
    assert(0); // TODO
}

// set a weight for each participating task in a partitioning, to be
//  used when a repartitioning is requested
void laik_set_partition_weights(Laik_Partitioning* p, int* w)
{
    assert(0); // TODO
}


// change an existing base partitioning
void laik_repartition(Laik_Partitioning* p, Laik_PartitionType pt)
{
    assert(0); // TODO
}


// couple different LAIK instances via spaces:
// one partition of calling task in outer space is mapped to inner space
void laik_couple_nested(Laik_Space* outer, Laik_Space* inner)
{
    assert(0); // TODO
}


//----------------------------------
// Predefined Partitioners

// Block partitioner

// forward decl
void runBlockPartitioner(Laik_BlockPartitioner* bp);

Laik_Partitioner* laik_newBlockPartitioner(Laik_Partitioning* p)
{
    Laik_BlockPartitioner* bp;
    bp = (Laik_BlockPartitioner*) malloc(sizeof(Laik_BlockPartitioner));

    bp->base.type = LAIK_PT_Block;
    bp->base.partitioning = p;
    bp->base.run = runBlockPartitioner;

    bp->getIdxW = 0;
    bp->idxUserData = 0;
    bp->getTaskW = 0;
    bp->taskUserData = 0;
}

void laik_set_index_weight(Laik_Partitioning* p, Laik_GetIdxWeight_t f,
                           void* userData)
{
    assert(p->type == LAIK_PT_Block);
    Laik_BlockPartitioner* bp;
    // may create block partitioner object if not existing yet
    bp = (Laik_BlockPartitioner*) laik_get_partitioner(p);

    bp->getIdxW = f;
    bp->idxUserData = userData;

    // borders have to be recalculated
    p->bordersValid = false;
}

void laik_set_task_weight(Laik_Partitioning* p, Laik_GetTaskWeight_t f,
                          void* userData)
{
    assert(p->type == LAIK_PT_Block);
    Laik_BlockPartitioner* bp;
    // may create block partitioner object if not existing yet
    bp = (Laik_BlockPartitioner*) laik_get_partitioner(p);

    bp->getTaskW = f;
    bp->taskUserData = userData;

    // borders have to be recalculated
    p->bordersValid = false;
}

void runBlockPartitioner(Laik_BlockPartitioner* bp)
{
    Laik_Partitioning* p = bp->base.partitioning;
    assert(p->borders != 0);
    assert(p->type == LAIK_PT_Block);

    int count = p->group->size;
    int pdim = p->pdim;
    uint64_t size = p->space->size[pdim];
    if (bp->getIdxW) {
        // element-wise weighting
        // TODO: also task-wise weighting
        Laik_Index idx;
        setIndex(&idx, 0, 0, 0);
        double total = 0.0;
        for(uint64_t i = 0; i < size; i++) {
            idx.i[pdim] = i;
            total += (bp->getIdxW)(&idx, bp->idxUserData);
        }
        double perPart = total / count;
        double w = 0.0;
        int task = 0;
        p->borders[task].from.i[pdim] = 0;
        for(uint64_t i = 0; i < size; i++) {
            idx.i[pdim] = i;
            w += (bp->getIdxW)(&idx, bp->idxUserData);
            if (w >= perPart) {
                w = w - perPart;
                if (task+1 == count) break;
                p->borders[task].to.i[pdim] = i;
                task++;
                p->borders[task].from.i[pdim] = i;
            }
        }
        assert(task+1 == count);
        p->borders[task].to.i[pdim] = size;
        return;
    }

    // use task-wise weighting?
    bool useTW = false;
    double totalTW = 0.0;
    if (bp->getTaskW) {
        for(int task = 0; task < count; task++)
            totalTW += (bp->getTaskW)(task, bp->taskUserData);
        if (totalTW > 0.0)
            useTW = true;
    }

    uint64_t idx = 0, inc;
    for(int task = 0; task < count; task++) {
        Laik_Slice* b = &(p->borders[task]);

        b->from.i[pdim] = idx;
        if (useTW) {
            double f = (bp->getTaskW)(task, bp->taskUserData) / totalTW;
            inc = (uint64_t)(f * size);
        }
        else {
            // equal-sized blocks
            inc = size / count;
        }

        idx += inc;
        // last border always must be <size>
        if ((idx > size) || (task+1 == count))
            idx = size;
        b->to.i[pdim] = idx;
    }
}
