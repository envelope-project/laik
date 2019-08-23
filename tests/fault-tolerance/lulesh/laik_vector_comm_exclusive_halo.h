#ifndef LAIK_VECTOR_COMM_EXCLUSIVE_HALO_H
#define LAIK_VECTOR_COMM_EXCLUSIVE_HALO_H

#include"laik_vector.h"

template <typename T>
class laik_vector_comm_exclusive_halo:public laik_vector<T>
{
public:
    laik_vector_comm_exclusive_halo(Laik_Instance* inst, Laik_Group* world, Laik_Space* indexSpace, Laik_Partitioning *p1, Laik_Partitioning *p2, Laik_Transition* t1, Laik_Transition* t2, Laik_ReductionOperation operation = LAIK_RO_None);
    inline T& operator [](int idx) override;
    T* calc_pointer(int idx, int state, int b, int f, int d, int u, int l, int r);
    void precalculate_base_pointers() override;
    void resize(int count) override;
    void switch_to_p1() override;
    void switch_to_p2() override;
    void migrate(Laik_Group* new_group, Laik_Partitioning* p_new_1, Laik_Partitioning* p_new_2, Laik_Transition* t_new_1, Laik_Transition* t_new_2,
                 bool suppressSwitchToP1) override;
};

template <typename T>
inline
T& laik_vector_comm_exclusive_halo<T>::operator [](int idx){
    return *(this -> pointer_cache[idx]);
}

#endif // LAIK_VECTOR_COMM_EXCLUSIVE_HALO_H
