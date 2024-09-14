/* This file is part of the LAIK parallel container library.
 * Copyright (c) 2021 Josef Weidendorfer
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * World resize example.
 *
 * Allow resizing after each iteration.
 */

#include "laik.h"
#include <stdio.h>
#include <unistd.h>

const char* usage = "\
Usage: %s [-t] <maxiter>\n\
-t: measure and print how long each resize took\n";

int main(int argc, char* argv[])
{
    int max = 0;
    int timings = 0;

    // Option to measure how long the resize took
    if (argc > 1 && argv[1][0] == '-') {
        switch (argv[1][1]) {
            case 't': timings = 1; break;
            case 'h': printf(usage, argv[0]); return 0;
            default:
              fprintf(stderr, "Unrecognized option -%c\n", argv[1][1]);
              return 1;
        }
        argc--;
        argv++;
    }

    if (argc > 1) max = atoi(argv[1]);
    if (max == 0) max = 10;

    Laik_Instance* inst = laik_init(&argc, &argv);
    int phase = laik_phase(inst);

    double start_time, end_time;
    Laik_Group *world = laik_world(inst);
    while(1) {
        printf("Epoch %d / Phase %d: Hello from process %d of %d\n",
               laik_epoch(inst), phase,
               laik_myid(world), laik_size(world));
        laik_release_group(world);
        if (phase >= max) break;

        sleep(1);
        phase++;

        // allow resize of world and get new world
        start_time = laik_wtime();
        world = laik_allow_world_resize(inst, phase);
        end_time = laik_wtime();

        if (laik_myid(world) < 0) break;
        if (timings) printf("%d: resize took %f msec\n",
                            laik_myid(world), (end_time - start_time) * 1000);
    }

    laik_finalize(inst);
    return 0;
}
