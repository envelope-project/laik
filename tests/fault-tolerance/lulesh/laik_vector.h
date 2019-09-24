#ifndef LAIK_VECTOR
#define LAIK_VECTOR

extern "C"{
#include "laik.h"
#include "laik-backend-mpi.h"
}
#include<vector>


/**
 * @brief laik_vector: abstract class
 * abstraction for laik data container,
 * 2 partintionings and transitions and
 * communication action sequences to switch
 * between the partitionings.
 * laik_vector encapsulates Laik_Data and allows
 * access to data using local indices.
 */
template <typename T>
class laik_vector
{

public:
    /**
     * @brief laik_vector constructor
     * @param inst laik context which is already initialized
     * @param world laik process group on which the partitionings are defined
     * @param indexSpace index space to partition
     * @param p1 first paritioning
     * @param p2 second partitoining
     * @param t1 first transition
     * @param t2 second transisition
     * @param operation reduction operation for switching between p1 and p2
     */
    laik_vector(Laik_Instance* inst, Laik_Group* world, Laik_Space* indexSpace,
 Laik_Partitioning *p1, Laik_Partitioning *p2, Laik_Transition* t1, Laik_Transition* t2, Laik_ReductionOperation operation = LAIK_RO_None);
    
    // virtual member functions to be implemented in conrete laik_vectors
    /**
     * @brief resize public method to initialize laik_containers
     * @param count local size of 1d index space
     */
    virtual void resize(int count) = 0;

    /**
     * @brief operator [] de-referencing operator
     * @param idx local index to be referenced in the Laik_Data
     * @return the idx-th element in the continer
     */
    inline virtual T& operator [](int idx) = 0;

    /**
     * @brief switch_to_p1 method to switch to p1
     */
    virtual void switch_to_p1() = 0;

    /**
     * @brief switch_to_p2 method to switch to p2
     */
    virtual void switch_to_p2() = 0;

    /**
     * @brief migrate method to migrate to new process groups
     * @param new_group target process group
     * @param p_new_1 first partitioning on the new_group
     * @param p_new_2 second partitioning on the new_group
     * @param t_new_1 transition to p_new_1
     * @param t_new_2 transition to p_new_2
     */
    virtual void
    migrate(Laik_Group *new_group, Laik_Partitioning *p_new_1, Laik_Partitioning *p_new_2, Laik_Transition *t_new_1,
            Laik_Transition *t_new_2, bool suppressSwitchToP1) = 0;

    /**
     * @brief clearing laik_vectors
     */
    void clear();

    void copyLaikDataToVector(std::vector<T> &data_vector);
    void copyVectorToLaikData(std::vector<T> &data_vector);

    void resizeVector(std::vector<T>&);
    void resizeVectorToLaikData(std::vector<T>&);

#ifdef FAULT_TOLERANCE
    virtual Laik_Checkpoint *checkpoint(int redundancyCount, int rotationDistance);
    virtual void restore(Laik_Checkpoint *checkpoint, Laik_Group *newGroup);
#endif

protected:
    // members from laik
    Laik_Instance* inst;                            // laik context
    Laik_Group* world;                              // working process group
    Laik_ReductionOperation reduction_operation;    // reduction operation when switching (could be used for switching to p1 or p2)
    Laik_Space* indexSpace;                         // indexSpace index space to partition
    Laik_Partitioning *p1;                          // p1 first paritioning
    Laik_Partitioning *p2;                          // p2 second paritioning
    Laik_Transition *t1;                            // t1 first transition  (toW)
    Laik_Transition *t2;                            // t2 second transition (toR)
    Laik_ActionSeq* as1;                            // action sequence corresponding to t1 (asW)
    Laik_ActionSeq* as2;                            // action sequence corresponding to t1 (asR)
    Laik_Data* data;                                // laik data container


    T **pointer_cache;                              // base pointers of Laik_Slices
    
    int size;                                       // limit of accessible local indices

    int state;                                      // state variable to indicate which partitioning is active; p1:1, p2:0

    int count;                                      // helper value for counting, e.g., size of slices

    // internal member methods
    // vitrual to be implemented in each concrete laik_vector
    /**
     * @brief precalculate_base_pointers
     * method to precalculate the base pointers of each Laik_Slice in the partitionings
     * this is used to calculate the base pointers only once and the precalculated
     * pointers are valid for as long as the partitionings are not changed.
     */
    virtual void precalculate_base_pointers() = 0;

    /**
     * @brief test_print printing laik_vector for debug
     */
    void test_print();

    void prepareMigration(bool suppressDataSwitchToP1);
};

#endif // LAIK_VECTOR
