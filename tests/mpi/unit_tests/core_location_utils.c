//
// Created by Vincent Bode on 23/05/2019.
//
#include "laik.h"
#include <stdio.h>
#include <assert.h>
#include <laik-internal.h>

void test_laik_group_get_location(Laik_Instance* instance) {


    Laik_Group *world = laik_world(instance);
    int worldSize = laik_size(world);
    if(worldSize != 4) {
        printf("Error: Test running with world size %i, please run it with world size 4.\n", worldSize);
        assert(false);
    }


    //Right now, the group should map to itself
    Laik_Group* g0 = laik_clone_group(laik_world(instance));
    assert(0 == laik_location_get_world_offset(g0, 0));
    assert(1 == laik_location_get_world_offset(g0, 1));
    assert(2 == laik_location_get_world_offset(g0, 2));
    assert(3 == laik_location_get_world_offset(g0, 3));


    // Create some shrinked groups
    int eliminate1[] =  {1};
    Laik_Group* g1 = laik_new_shrinked_group(world, 1, eliminate1);

    assert(laik_location_get_world_offset(g1, 0) == 0);
    assert(laik_location_get_world_offset(g1, 1) == 2);
    assert(laik_location_get_world_offset(g1, 2) == 3);

    // Shrink the shrinked group
    Laik_Group* g2 = laik_new_shrinked_group(g1, 1, eliminate1);
    assert(laik_location_get_world_offset(g2, 0) == 0);
    assert(laik_location_get_world_offset(g2, 1) == 3);

}

void test_laik_location_data(Laik_Instance* instance) {
    Laik_Group* world = laik_world(instance);

    laik_location_synchronize_data(instance, world);

    for(int i = 0; i < world->size; i++) {
        printf("Identifier: " + laik_location_get(world, i));
    }
}

int main(int argc, char* argv[]) {
    Laik_Instance* instance = laik_init(&argc, &argv);

    test_laik_group_get_location(instance);

    test_laik_location_data(instance);

    laik_finalize(instance);

    return 0;
}
