//
// Created by Vincent Bode on 23/05/2019.
//

#include "laik-internal.h"

#include <stdio.h>
#include <assert.h>

void test_laik_group_get_location(Laik_Instance* instance) {


    Laik_Group *world = laik_world(instance);
    int worldSize = laik_size(world);
    if(worldSize != 4) {
        printf("Error: Test running with world size %i, please run it with world size 4.\n", worldSize);
        assert(false);
    }

    //Right now, the group should map to itself
    Laik_Group* g0 = laik_clone_group(laik_world(instance));
    assert(0 == laik_group_locationid(g0, 0));
    assert(1 == laik_group_locationid(g0, 1));
    assert(2 == laik_group_locationid(g0, 2));
    assert(3 == laik_group_locationid(g0, 3));

    // Create some shrinked groups
    int eliminate1[] =  {1};
    Laik_Group* g1 = laik_new_shrinked_group(world, 1, eliminate1);

    assert(laik_group_locationid(g1, 0) == 0);
    assert(laik_group_locationid(g1, 1) == 2);
    assert(laik_group_locationid(g1, 2) == 3);

    // Shrink the shrinked group
    Laik_Group* g2 = laik_new_shrinked_group(g1, 1, eliminate1);
    assert(laik_group_locationid(g2, 0) == 0);
    assert(laik_group_locationid(g2, 1) == 3);

}

void test_laik_location_data(Laik_Instance* instance) {
    Laik_Group* world = laik_world(instance);

    printf("Testing identifiers\n%i my location: %s\n", world->myid, laik_mylocation(instance));

    laik_location_synchronize_data(instance, world);

    for(int i = 0; i < world->size; i++) {
        printf("%i identifier %i: %s\n", world->myid, i, laik_group_location(world, i));
    }
}

int main(int argc, char* argv[]) {
    Laik_Instance* instance = laik_init(&argc, &argv);

    test_laik_group_get_location(instance);

    test_laik_location_data(instance);

    laik_finalize(instance);

    return 0;
}
