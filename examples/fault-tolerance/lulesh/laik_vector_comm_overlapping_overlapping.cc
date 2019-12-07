#include"laik_vector_comm_overlapping_overlapping.h"
#include "laik_partitioners.h"
#include "lulesh.h"
#include <limits.h>
#include <type_traits>
#include <string.h>


// ///////////////////////////////////////////////////////////
// implementation of laik_vector with overlapping partitioning
// (node partitioning)
// ///////////////////////////////////////////////////////////

template <typename T>
laik_vector_comm_overlapping_overlapping<T>::laik_vector_comm_overlapping_overlapping(Laik_Instance *inst,
                                                 Laik_Group*world,
                                                    Laik_Space* indexSpace,
                                                   Laik_Partitioning *p1, Laik_Partitioning *p2,
                                                    Laik_Transition* t1, Laik_Transition* t2,
                                                    Laik_ReductionOperation operation):laik_vector<T>(inst,world, indexSpace, p1, p2, t1,t2, operation){}
template <typename T>
void laik_vector_comm_overlapping_overlapping<T>::resize(int count){

    this->size = count;

    if (std::is_same <T, double>::value) {
        this->data = laik_new_data(this->indexSpace, laik_Double );

    }
    else if (std::is_same <T, int>::value){
        this->data = laik_new_data(this->indexSpace, laik_Int64 );
    }
    // use the reservation API to precalculate the pointers

    Laik_Reservation* reservation = laik_reservation_new(this->data);
    laik_reservation_add(reservation, this->p1);

    laik_reservation_alloc(reservation);
    laik_data_use_reservation(this->data, reservation);

    // precalculate the transition object

    this->as1 = laik_calc_actions(this->data, this->t1, reservation, reservation);
    this->as2 = laik_calc_actions(this->data, this->t2, reservation, reservation);

    // go through the slices to just allocate the memory
    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, this->reduction_operation );
    Laik_TaskSlice* ts = laik_my_slice(this->p1, 0);
    const Laik_Slice* s = laik_taskslice_get_slice(ts);
    this -> count = laik_slice_size(s);
    this -> state = 0;
    this -> precalculate_base_pointers();
}

/*
double& laik_vector_comm_overlapping_overlapping::operator [](int idx){
    return *(this -> overlapping_pointers[idx]);
}
*/

template <typename T>
T* laik_vector_comm_overlapping_overlapping<T>::calc_pointer(int idx){
    uint64_t cnt;
    T* base;

    int i = (idx % (this->count*this->count) ) % this->count;
    int l_idx = i;
    int slice = idx/this->count;
    laik_get_map_1d(this->data, slice, (void **)&base, &cnt);

    return base+l_idx;
}

template <typename T>
void laik_vector_comm_overlapping_overlapping<T>::precalculate_base_pointers(){    

    if (this->pointer_cache != nullptr)   free (this->pointer_cache);

    this->pointer_cache = (T**) malloc (this->size * sizeof(T*));

    for (int i = 0; i < this->size; ++i) {
        this->pointer_cache [i] = calc_pointer(i);
    }
}

template <typename T>
void laik_vector_comm_overlapping_overlapping<T>::switch_to_p1(){
    laik_exec_actions(this->as1);
    //laik_exec_transition(data,t1);
    //laik_switchto_phase(data, p1, Laik_DataFlow
    //                    ( LAIK_DF_ReduceOut | LAIK_DF_Sum ) );
}

template <typename T>
void laik_vector_comm_overlapping_overlapping<T>::switch_to_p2(){
    laik_exec_actions(this->as2);
    //laik_exec_transition(data,t2);
    //laik_switchto_phase(data, p1, LAIK_DF_CopyIn);
}

template <typename T>
void laik_vector_comm_overlapping_overlapping<T>::migrate(Laik_Group *new_group, Laik_Partitioning *p_new_1,
                                                          Laik_Partitioning *p_new_2, Laik_Transition *t_new_1,
                                                          Laik_Transition *t_new_2, bool suppressSwitchToP1) {
    uint64_t cnt;
    int* base;
    //int slice = 0;

    this->prepareMigration(suppressSwitchToP1);

    Laik_Reservation* reservation = laik_reservation_new(this->data);
    laik_reservation_add(reservation, p_new_1);
    laik_reservation_alloc(reservation);
    laik_data_use_reservation(this->data, reservation);

    laik_switchto_partitioning(this->data, p_new_1, LAIK_DF_Preserve, LAIK_RO_Min);

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

    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, LAIK_RO_Min );
    int nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; n++)
    {
        laik_get_map_1d(this->data, n, (void **)&base, &cnt);
    }
    laik_switchto_partitioning(this->data, this->p2, LAIK_DF_Preserve, LAIK_RO_Min);

    this -> count = cnt;
    this -> state = 0;

    this -> precalculate_base_pointers();
}


template class laik_vector_comm_overlapping_overlapping<double>;
