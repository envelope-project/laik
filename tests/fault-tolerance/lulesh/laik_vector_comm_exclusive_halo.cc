#include"laik_vector_comm_exclusive_halo.h"
#include <laik_partitioners.h>
#include <lulesh.h>
#include <limits.h>
#include <type_traits>
#include <string.h>

// ////////////////////////////////////////////////////////////////////////
// implementation of laik_vector with halo partitioning (node partitioning)
// ////////////////////////////////////////////////////////////////////////
template <typename T>
laik_vector_comm_exclusive_halo<T>::laik_vector_comm_exclusive_halo(Laik_Instance *inst,
                                   Laik_Group *world,
                                      Laik_Space* indexSpace,
                                      Laik_Partitioning *p1, Laik_Partitioning *p2, Laik_Transition* t1, Laik_Transition* t2, Laik_ReductionOperation operation):laik_vector<T>(inst,world, indexSpace, p1, p2, t1, t2, operation){}
template <typename T>
void laik_vector_comm_exclusive_halo<T>::resize(int count){

    this->size = count;

    if (std::is_same <T, double>::value) {
        this -> data = laik_new_data(this->indexSpace, laik_Double );

    }
    else if (std::is_same <T, int>::value){
        this -> data = laik_new_data(this->indexSpace, laik_Int64 );
    }

    // use the reservation API to precalculate the pointers
    Laik_Reservation* reservation = laik_reservation_new(this->data);
    laik_reservation_add(reservation, this->p2);
    laik_reservation_add(reservation, this->p1);

    laik_reservation_alloc(reservation);
    laik_data_use_reservation(this->data, reservation);

    this->as1 = laik_calc_actions(this->data, this->t1, reservation, reservation);
    this->as2 = laik_calc_actions(this->data, this->t2, reservation, reservation);

    // go through the slices to just allocate the memory
    uint64_t cnt;
    T* base;

    //laik_exec_transition(data, toExclusive);
    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, this->reduction_operation);
    int nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; ++n)
    {
       laik_map_def(this->data, n, (void **)&base, &cnt);
    }
    //laik_exec_transition(this->data, toHalo);
    laik_switchto_partitioning(this->data, this->p2, LAIK_DF_Preserve, this->reduction_operation);

    this -> state = 0;
    this -> count = cnt;

    this -> precalculate_base_pointers();
}

template <typename T>
T* laik_vector_comm_exclusive_halo<T>::calc_pointer(int idx, int state, int b, int f, int d, int u, int l, int r){
    uint64_t cnt;
    T* base;

    int i=0;
    int j=0;
    int k=0;
    int index=0;
    int slice=-1;
    int side =-1;
    int numElem=this->count*this->count*this->count;

    Index_t ghostIdx[6] ;  // offsets to ghost locations

    for (Index_t i=0; i<6; ++i) {
      ghostIdx[i] = INT_MAX;
    }

    Int_t pidx = numElem ;
    if (b) {
      ghostIdx[0] = pidx ;
      pidx += this->count*this->count ;
    }
    if (f) {
      ghostIdx[1] = pidx ;
      pidx += this->count*this->count ;
    }
    if (d) {
      ghostIdx[2] = pidx ;
      pidx += this->count*this->count ;
    }
    if (u) {
      ghostIdx[3] = pidx ;
      pidx += this->count*this->count ;
    }
    if (l) {
      ghostIdx[4] = pidx ;
      pidx += this->count*this->count ;
    }
    if (r) {
      ghostIdx[5] = pidx ;
    }

    for (int i = 0; i < 6; ++i) {
        if  (idx>=ghostIdx[i]){
            side =i;
        }
    }
    // tested OK
    if (state) {
        i = idx % this->count;
        slice = idx/this->count;
    }
    else{
        // tested OK
        if (idx < numElem) {
            i = idx%this->count;
            j = (idx/this->count)%this->count;
            k = idx/(this->count*this->count);
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }

        // back
        // tested OK
        else if (side==0) {
            index =idx-ghostIdx[side];
            i = index%this->count;
            j = index/this->count;
            k = -1;
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }
        // front
        // tested OK
        else if (side==1) {
            index =idx-ghostIdx[side];
            i = index%this->count;
            j = index/this->count;
            k = this->count;
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }
        //down
        //tested OK
        else if (side==2) {
            index =idx-ghostIdx[side];
            i = index%this->count;
            j = -1;
            k = index/this->count;
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }
        //up
        //tested OK
        else if (side==3) {
            index =idx-ghostIdx[side];
            i = index%this->count;
            j = this->count;
            k = index/this->count;
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }
        //left
        //tested OK
        else if (side==4) {
            index =idx-ghostIdx[side];
            i = -1;
            j = index%this->count;
            k = index/this->count;
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }
        //right
        //tested OK
        else if (side==5) {
            index =idx-ghostIdx[side];
            i = this->count;
            j = index%this->count;
            k = index/this->count;
            slice = (this->count+d+u)*(k+b)+(j+d);
            i += l;
        }
    }

    if (slice>-1) {
        laik_map_def(this->data, slice, (void **)&base, &cnt);
        //laik_log(Laik_LogLevel(2),"state: %d, idx: %d, pointer: %x", state, idx, base+i);
        return base+i;
    }

    return nullptr; // error
}

template <typename T>
void laik_vector_comm_exclusive_halo<T>::precalculate_base_pointers(){
    int b, f, d, u, l, r;
    init_config_params(this->world, b , f, d, u, l, r);

    int numElems = this->count*this->count*this->count;

    if (this->pointer_cache != nullptr)   free (this->pointer_cache);

    this->pointer_cache= (T**) malloc (numElems * sizeof(T*));
    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, this->reduction_operation);

    for (int i = 0; i < numElems; ++i) {
        this->pointer_cache [i] = calc_pointer(i,1, b, f, d, u, l, r);
    }
    // test
    /*
    for (int i = 0; i < numElems; ++i) {
        laik_log((Laik_LogLevel)2,"exclusive pointers: %d %x\n", i, exclusive_pointers [i]);
    }
    */

    int numElemsTotal = numElems + (b+f+d+u+l+r)*this->count*this->count;
    this->pointer_cache= (T**) malloc (numElemsTotal * sizeof(T*));
    laik_switchto_partitioning(this->data, this->p2, LAIK_DF_Preserve, this->reduction_operation);

    for (int i = 0; i < numElemsTotal; ++i) {
        this->pointer_cache [i] = calc_pointer(i,0, b, f, d, u, l, r);
    }
    // test
    /*
    for (int i = 0; i < numElemsTotal; ++i) {
        laik_log((Laik_LogLevel)2,"halo pointers: %d %x\n", i, halo_pointers [i]);
    }
    */

    //laik_log((Laik_LogLevel)2,"debug for the reservation api");
}

template <typename T>
void laik_vector_comm_exclusive_halo<T>::switch_to_p1(){
    laik_exec_actions(this->as1);
    //laik_exec_transition(this->data,t1);
    //laik_switchto_phase(this->data, p1, LAIK_DF_CopyOut);
    this->state=1;
}

template <typename T>
void laik_vector_comm_exclusive_halo<T>::switch_to_p2(){
    laik_exec_actions(this->as2);
    //laik_exec_transition(this->data,t2);
    //laik_switchto_phase(this->data, p2, LAIK_DF_CopyIn);
    this->state=0;
}

template <typename T>
void laik_vector_comm_exclusive_halo<T>::migrate(Laik_Group* new_group, Laik_Partitioning* p_new_1, Laik_Partitioning* p_new_2, Laik_Transition* t_new_1, Laik_Transition* t_new_2){
    uint64_t cnt;
    int* base;
    //int slice = 0;

    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, LAIK_RO_None);

    // use the reservation API to precalculate the pointers
    Laik_Reservation* reservation = laik_reservation_new(this->data);
    laik_reservation_add(reservation, p_new_2);
    laik_reservation_add(reservation, p_new_1);
    laik_reservation_alloc(reservation);
    laik_data_use_reservation(this->data, reservation);

    laik_switchto_partitioning(this->data, p_new_1, LAIK_DF_Preserve, LAIK_RO_None);

    if (laik_myid(new_group)< 0) {
        return;
    }

    this->as1 = laik_calc_actions(this->data, t_new_1, reservation, reservation);
    this->as2 = laik_calc_actions(this->data, t_new_2, reservation, reservation);

    this -> p1=p_new_1;
    this -> p2=p_new_2;
    this -> t1=t_new_1;
    this -> t2=t_new_2;
    this -> world = new_group;
    if (laik_myid(this->world)<0)
        return ;

    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, LAIK_RO_None);
    int nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; n++)
    {
        laik_map_def(this->data, n, (void **)&base, &cnt);
    }
    laik_switchto_partitioning(this->data, this->p2, LAIK_DF_Preserve, LAIK_RO_None);

    this -> state = 0;
    this -> count = cnt;

    this -> precalculate_base_pointers();
}


template class laik_vector_comm_exclusive_halo<double>;


