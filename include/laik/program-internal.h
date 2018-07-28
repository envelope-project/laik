/*
 * This file is part of the LAIK library.
 * Copyright (c) 2017 Dai Yang, I10/TUM
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LAIK_PROGRAM_INTERNAL
#define LAIK_PROGRAM_INTERNAL

struct tag_laik_program_control
{
    int cur_phase;
    const char* cur_phase_name;
    // user controlled structure
    void* pData;
    // current iteration number iterations
    int cur_iteration;

    // allow automatic re-launches if set
    int argc;    // 0: invalid
    char** argv;
};

#endif // LAIK_PROGRAM_INTERNAL
