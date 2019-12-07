#ifndef LAIK_PART
#define LAIK_PART

/*laik headers*/
extern "C"{
#include "laik.h"
#include "laik-backend-mpi.h"
}

// this file includes the partitioner algorithms that is used to port lulesh
// code to use laik

void runExclusivePartitioner(Laik_Partitioner* pr, Laik_Partitioning* p, Laik_Partitioning* oldP);
Laik_Partitioner* exclusive_partitioner();
void runOverlapingPartitioner(Laik_Partitioner* pr, Laik_Partitioning* p, Laik_Partitioning* oldP);
Laik_Partitioner* overlaping_partitioner(int &depth);
void runOverlapingReductionPartitioner(Laik_Partitioner* pr, Laik_Partitioning* p, Laik_Partitioning* oldP);
Laik_Partitioner* overlaping_reduction_partitioner(int &depth);

#endif // LAIK_PART
