#include"laik_vector_repart_exclusive.h"
#include <laik_partitioners.h>
#include <lulesh.h>
#include <limits.h>
#include <type_traits>
#include <string.h>

// ////////////////////////////////////////////////////////////////////////
// implementation of laik_vector with exclusive partitioning (elem partitioning)
// for repartitioning of exclusive data structs
// ////////////////////////////////////////////////////////////////////////
template <typename T>
laik_vector_repart_exclusive<T>::laik_vector_repart_exclusive(Laik_Instance *inst,
                                   Laik_Group *world,
                                      Laik_Space* indexSpace,
                                      Laik_Partitioning *p1, Laik_Partitioning *p2, Laik_Transition* t1, Laik_Transition* t2, Laik_ReductionOperation operation):laik_vector<T>(inst,world, indexSpace, p1, p2, t1, t2, operation){}
template <typename T>
void laik_vector_repart_exclusive<T>::resize(int count){

    int s =  count / laik_size(this->world);
    data_vector.resize(s);

    this -> size = count;

    if (std::is_same <T, double>::value) {
        this->data = laik_new_data(this->indexSpace, laik_Double );

    }
    else if (std::is_same <T, int>::value){
        this->data = laik_new_data(this->indexSpace, laik_Int64 );
    }

    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, this->reduction_operation);
    uint64_t cnt;
    T* base;
    int nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; ++n)
    {
       laik_map_def(this->data, n, (void **)&base, &cnt);
    }
    this -> count = cnt;
}

template <typename T>
T* laik_vector_repart_exclusive<T>::calc_pointer(int idx, int state){
    return nullptr;
}

template <typename T>
void laik_vector_repart_exclusive<T>::precalculate_base_pointers(){

}

template <typename T>
void laik_vector_repart_exclusive<T>::switch_to_p1(){

}

template <typename T>
void laik_vector_repart_exclusive<T>::switch_to_p2(){

}

template <typename T>
void laik_vector_repart_exclusive<T>::migrate(Laik_Group* new_group, Laik_Partitioning* p_new_1, Laik_Partitioning* p_new_2, Laik_Transition* t_new_1, Laik_Transition* t_new_2){
    uint64_t cnt;
    T* base;
    int nSlices;

    this -> state = 0;

    laik_switchto_partitioning(this->data, this->p1, LAIK_DF_Preserve, LAIK_RO_None);
    // copy the data from stl vector into the laik container
    nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; n++)
    {
        laik_map_def(this->data, n, (void **)&base, &cnt);
        memcpy(base, &data_vector[0] + n*cnt, cnt*sizeof(T));
        //std::copy( base, base + cnt, data_vector.begin() + n*count );
    }

    // perform switches for communication

    laik_switchto_partitioning(this->data, p_new_1, LAIK_DF_Preserve, LAIK_RO_None);

    this -> world = new_group;
    if (laik_myid(this->world)<0)
        return ;

    this -> p1=p_new_1;
    this -> p2=p_new_2;
    this -> t1=t_new_1;
    this -> t2=t_new_2;

    // resize vector
    laik_map_def(this->data, 0, (void **)&base, &cnt);
    int s = cnt*cnt*cnt;
    data_vector.resize(s);

    // copy the data back into the stl vecotrs
    nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; n++)
    {
        laik_map_def(this->data, n, (void **)&base, &cnt);
        memcpy(&data_vector[0] + n*cnt, base, cnt*sizeof(T));
        //std::copy(data_vector.begin() + n*count ,data_vector.begin() + (n+1)*count-1 , base);
    }
}

template class laik_vector_repart_exclusive<double>;
