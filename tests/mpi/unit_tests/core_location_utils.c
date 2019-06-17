//
// Created by Vincent Bode on 23/05/2019.
//
#include "laik.h"
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
    assert(0 == laik_group_location(g0, 0));
    assert(1 == laik_group_location(g0, 1));
    assert(2 == laik_group_location(g0, 2));
    assert(3 == laik_group_location(g0, 3));


    // Create some shrinked groups
    int eliminate1[] =  {1};
    Laik_Group* g1 = laik_new_shrinked_group(world, 1, eliminate1);

    assert(laik_group_location(g1, 0) == 0);
    assert(laik_group_location(g1, 1) == 2);
    assert(laik_group_location(g1, 2) == 3);

    // Shrink the shrinked group
    Laik_Group* g2 = laik_new_shrinked_group(g1, 1, eliminate1);
    assert(laik_group_location(g2, 0) == 0);
    assert(laik_group_location(g2, 1) == 3);

}

void test_laik_location_data(Laik_Instance* instance) {
    Laik_Group* world = laik_world(instance);
}

int main(int argc, char* argv[]) {
    Laik_Instance* instance = laik_init(&argc, &argv);

    test_laik_group_get_location(instance);

    laik_finalize(instance);

    return 0;
}
