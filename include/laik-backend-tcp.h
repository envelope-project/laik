#pragma once

#include "laik.h" // for Laik_Instance

Laik_Instance* laik_init_tcp (int* argc, char*** argv);

void laik_tcp_set_errors(int newStatusFlag, void* newErrorTrace);
void laik_tcp_clear_errors();
