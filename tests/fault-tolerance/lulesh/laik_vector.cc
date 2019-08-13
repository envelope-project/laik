#include "laik_vector.h"
#include "laik_vector_comm_exclusive_halo.h"
#include "laik_vector_comm_overlapping_overlapping.h"
#include "laik_vector_repart_exclusive.h"
#include "laik_vector_repart_overlapping.h"

#include "laik_partitioners.h"
#include "lulesh.h"
#include <limits.h>
#include <type_traits>
#include <string.h>

template <typename T>
laik_vector<T>::laik_vector(Laik_Instance* inst, Laik_Group* world, Laik_Space* indexSpace, Laik_Partitioning *p1, Laik_Partitioning *p2, Laik_Transition* t1, Laik_Transition* t2, Laik_ReductionOperation operation):reduction_operation(operation){
    this -> inst = inst;
    this -> world = world;
    this-> indexSpace = indexSpace;
    this -> p1 = p1;
    this -> p2 = p2;
    this -> t1 = t1;
    this -> t2 = t2;
    this -> pointer_cache = nullptr;

    //this -> init_config_params(world);
    this -> state = 0;
}

template <typename T>
void laik_vector<T>::test_print(){
    T *base;
    uint64_t count;
    int nSlices = laik_my_slicecount(p1);
    for (int s = 0; s < nSlices; s++)
    {
        laik_map_def(data, s, (void**) &base, &count);
        for (uint64_t i = 0; i < count; i++)
        {
            laik_log(Laik_LogLevel(2),"%f\n", base[i]);
        }
        laik_log(Laik_LogLevel(2),"\n");
    }
}

template <typename T>
void laik_vector<T>::clear(){}

template<typename T>
void laik_vector<T>::copyLaikDataToVector(std::vector<T> &data_vector) {
    uint64_t cnt;
    T *base;
    // copy the data back into the stl vecotrs
    int nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; n++) {
        laik_map_def(this->data, n, (void **) &base, &cnt);
        memcpy(&data_vector[0] + n * cnt, base, cnt * sizeof(T));
        //std::copy(data_vector.begin() + n*count ,data_vector.begin() + (n+1)*count-1 , base);
    }
}

template<typename T>
void laik_vector<T>::copyVectorToLaikData(std::vector<T> &data_vector) {
    uint64_t cnt;
    T *base;
    // copy the data from stl vector into the laik container
    int nSlices = laik_my_slicecount(this->p1);
    for (int n = 0; n < nSlices; n++) {
        laik_map_def(this->data, n, (void **) &base, &cnt);
        memcpy(base, &data_vector[0] + n * cnt, cnt * sizeof(T));
        //std::copy( base, base + cnt, data_vector.begin() + n*count );
    }
}

#ifdef FAULT_TOLERANCE
template<typename T>
Laik_Checkpoint * laik_vector<T>::checkpoint() {
    return laik_checkpoint_create(inst, indexSpace, data, laik_Master, 1, 1, laik_data_get_group(data), LAIK_RO_Min);
}

template <typename T>
void laik_vector<T>::restore(Laik_Checkpoint *checkpoint) {
    laik_checkpoint_restore(inst, checkpoint, indexSpace, data);
}
#endif

template class laik_vector<double>;

