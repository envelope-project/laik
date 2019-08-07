#ifndef LAIK_VECTOR_REPART_EXCLUSIVE_H
#define LAIK_VECTOR_REPART_EXCLUSIVE_H

#include"laik_vector.h"

template <typename T>
class laik_vector_repart_exclusive:public laik_vector<T>
{
public:
    laik_vector_repart_exclusive(Laik_Instance* inst, Laik_Group* world, Laik_Space* indexSpace, Laik_Partitioning *p1, Laik_Partitioning *p2, Laik_Transition* t1, Laik_Transition* t2, Laik_ReductionOperation operation = LAIK_RO_None);
    inline T& operator [](int idx) override;
    T* calc_pointer(int idx, int state);
    void precalculate_base_pointers() override;
    void resize(int count) override;
    void switch_to_p1() override;
    void switch_to_p2() override;
    void migrate(Laik_Group* new_group, Laik_Partitioning* p_new_1, Laik_Partitioning* p_new_2, Laik_Transition* t_new_1, Laik_Transition* t_new_2) override;

private:
    std::vector<T> data_vector;
};

template <typename T>
inline
T& laik_vector_repart_exclusive<T>::operator [](int idx){
    return this -> data_vector[idx];
}

#endif // LAIK_VECTOR_REPART_EXCLUSIVE_H
