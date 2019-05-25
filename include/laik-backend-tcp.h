#pragma once

#include "laik.h" // for Laik_Instance

Laik_Instance* laik_init_tcp (int* argc, char*** argv);

typedef void (*LaikTCPErrorHandler)(void*);
void laik_tcp_set_error_handler(LaikTCPErrorHandler newErrorHandler);
