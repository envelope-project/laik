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
#include <assert.h>
#include <iostream>
#include <inttypes.h>

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
        laik_get_map_1d(data, s, (void**) &base, &count);
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
    // copy the data back into the stl vectors
//    int nSlices = laik_my_slicecount(this->p1);
    int nSlices = laik_my_slicecount(laik_data_get_partitioning(this->data));
    for (int n = 0; n < nSlices; n++) {
        assert(this->data != NULL);
        assert(laik_data_get_partitioning(this->data) != nullptr);
        assert(laik_my_slicecount(laik_data_get_partitioning(this->data)) == nSlices);
        laik_get_map_1d(this->data, n, (void **) &base, &cnt);
        uint64_t elemOffset = n * cnt;
        laik_log(LAIK_LL_Debug, "Copy LAIK data to vector: vector (capacity) %zu data %" PRIu64
        " offset %" PRIu64 " length %" PRIu64, data_vector.capacity(), cnt,
                 elemOffset, cnt);
        assert(elemOffset >= 0 && elemOffset + cnt <= data_vector.capacity());
        memcpy(&data_vector[0] + elemOffset, base, cnt * sizeof(T));
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
        laik_get_map_1d(this->data, n, (void **) &base, &cnt);
//        laik_log(LAIK_LL_Info, "Copy vector to LAIK data: vector (size) %lu data %lu", data_vector.size(), cnt);
        assert(n * cnt >= 0 && n * cnt + cnt <= data_vector.capacity());
        memcpy(base, &data_vector[0] + n * cnt, cnt * sizeof(T));
        //std::copy( base, base + cnt, data_vector.begin() + n*count );
    }
}

template <typename T>
void laik_vector<T>::resizeVector(std::vector<T> &data_vector) {// resize vector
    uint64_t cnt;
    T* base;
    assert(laik_my_mapcount(laik_data_get_partitioning(this->data)) == 1);
    laik_get_map_1d(this->data, 0, (void **)&base, &cnt);
    int s = cnt*cnt*cnt;
    data_vector.resize(s);
}

template <typename T>
void laik_vector<T>::resizeVectorToLaikData(std::vector<T> &data_vector) {// resize vector
    uint64_t cnt = 0;
    for (int i = 0; i < laik_my_slicecount(laik_data_get_partitioning(data)); ++i) {
        cnt += laik_slice_size(laik_taskslice_get_slice(laik_my_slice(laik_data_get_partitioning(data), i)));
    }
    laik_log(LAIK_LL_Info, "Resizing vector from %zu to %" PRIu64, data_vector.capacity(), cnt);
    data_vector.resize(cnt);
}

template <typename T>
void laik_vector<T>::prepareMigration(bool suppressDataSwitchToP1) {
    if(!suppressDataSwitchToP1) {
        laik_switchto_partitioning(this->data, this->p1, LAIK_DF_None, LAIK_RO_None);
    }
}

#ifdef FAULT_TOLERANCE
template<typename T>
Laik_Checkpoint * laik_vector<T>::checkpoint(int redundancyCount, int rotationDistance) {
//    std::cout << "Creating checkpoint of " << laik_my_slicecount(laik_data_get_partitioning(data)) << " slices." << std::endl;
    return laik_checkpoint_create(data, laik_Master, redundancyCount, rotationDistance,
                                  laik_data_get_group(data), LAIK_RO_Min);
}

template <typename T>
void laik_vector<T>::restore(Laik_Checkpoint *checkpoint, Laik_Group *newGroup) {
    // Set partitioning to backup partitioning so that it can be migrated later
    assert(checkpoint->data != nullptr && laik_data_get_partitioning(checkpoint->data) != nullptr);
    Laik_Partitioning* newPartitioning = laik_new_partitioning(laik_Master, newGroup, indexSpace, nullptr);
    laik_switchto_partitioning(data, newPartitioning, LAIK_DF_None, LAIK_RO_None);
//    laik_partitioning_migrate(laik_data_get_partitioning(checkpoint->data), newGroup);
    laik_checkpoint_restore(checkpoint, data);
}

#endif

template class laik_vector<double>;

