// Test for KVS syncing of spaces

#include "laik-internal.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

void print_spaces(Laik_Instance* i, int step)
{
    Laik_Space *s;
    int myid = laik_myid(laik_world(i));

    s = laik_spacestore_get(i, "1d-space");
    assert(s && (s->dims == 1));
    assert(s->s.to.i[0] == ((step == 1) ? 100 : 1000));
    printf("T%d: 1d-space: %" PRId64 " - %" PRId64 "\n", myid,
           s->s.from.i[0], s->s.to.i[0]);

    s = laik_spacestore_get(i, "2d-space");
    assert(s && (s->dims == 2));
    assert(s->s.to.i[0] == ((step == 1) ? 200 : 2000));
    printf("T%d: 2d-space: %" PRId64 " - %" PRId64
           " / %" PRId64 " - %" PRId64 "\n", myid,
           s->s.from.i[0], s->s.to.i[0], s->s.from.i[1], s->s.to.i[1]);

    s = laik_spacestore_get(i, "3d-space");
    assert(s && (s->dims == 3));
    assert(s->s.to.i[0] == 400);
    printf("T%d: 3d-space: %" PRId64 " - %" PRId64
           " / %" PRId64 " - %" PRId64
           " / %" PRId64 " - %" PRId64 "\n", myid,
           s->s.from.i[0], s->s.to.i[0],
           s->s.from.i[1], s->s.to.i[1],
           s->s.from.i[2], s->s.to.i[2]);
}

int main(int argc, char* argv[])
{
    Laik_Instance* i = laik_init(&argc, &argv);
    Laik_Space *s;

    if (laik_myid(laik_world(i)) == 0) {
        s = laik_new_space_1d(i, 100);
        laik_set_space_name(s, "1d-space");
        laik_spacestore_set(s);

        s = laik_new_space_2d(i, 200, 300);
        laik_set_space_name(s, "2d-space");
        laik_spacestore_set(s);
    }
    s = laik_new_space_3d(i, 400, 500, 600);
    laik_set_space_name(s, "3d-space");
    laik_spacestore_set(s);

    laik_sync_spaces(i);
    print_spaces(i, 1);

    s = laik_spacestore_get(i, "1d-space");
    laik_change_space_1d(s, -100, 1000);
    if (laik_myid(laik_world(i)) == 0) {
        s = laik_spacestore_get(i, "2d-space");
        laik_change_space_2d(s, -200, 2000, -300, 3000);
    }

    laik_sync_spaces(i);
    print_spaces(i, 2);

    laik_finalize(i);
    return 0;
}
