//
// Created by Vincent Bode on 23/05/2019.
//

#include "laik-internal.h"

#include <stdio.h>
#include <assert.h>

void test_laik_group_get_location(Laik_Instance* instance)
{
    Laik_Group *world = laik_world(instance);
    int size = laik_size(world);

    // At LAIK init, process indexes in world and location IDs are identical
    Laik_Group* g0 = laik_clone_group(world);
    for(int i = 0; i < size; i++)
        assert(laik_group_locationid(g0, i) == i);

    if (size > 1) {
        // Create some shrinked groups
        int eliminate1[] =  {1};
        Laik_Group* g1 = laik_new_shrinked_group(world, 1, eliminate1);

        assert(laik_group_locationid(g1, 0) == 0);
        for(int i = 1; i < size - 1; i++)
            assert(laik_group_locationid(g1, i) == i + 1);

        if (size > 2) {
            // Shrink the shrinked group
            Laik_Group* g2 = laik_new_shrinked_group(g1, 1, eliminate1);

            assert(laik_group_locationid(g2, 0) == 0);
            for(int i = 1; i < size - 2; i++)
                assert(laik_group_locationid(g2, i) == i + 2);
        }
    }
}

void test_laik_location_data(Laik_Instance* instance)
{
    Laik_Group* world = laik_world(instance);
    printf("Testing identifiers - world index %i, location '%s'\n",
           world->myid, laik_mylocation(instance));

    laik_sync_location(instance);
    for(int i = 0; i < world->size; i++) {
        printf("at %i: identifier for ID %i is '%s'\n",
               world->myid, i, laik_group_location(world, i));
    }
}

int main(int argc, char* argv[])
{
    Laik_Instance* instance = laik_init(&argc, &argv);

    test_laik_group_get_location(instance);
    test_laik_location_data(instance);

    laik_finalize(instance);
    return 0;
}
