#include "laik.h"
#include <stdio.h>
#include "laik-backend-shmem.h"

int main(int argc, char* argv[])
{
    Laik_Instance* instance = laik_init_shmem(&argc, &argv);
    Laik_Group *world = laik_world(instance);
    int size = laik_size(world);
    int myid = laik_myid(world);

    printf("Hello from process %d (from %d)\n", myid, size);

    laik_finalize(instance);
    return 0;
}
