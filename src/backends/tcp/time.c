#include "time.h"
#include <glib.h>  // for g_get_monotonic_time, g_usleep, G_TIME_SPAN_SECOND

void laik_tcp_sleep (double seconds) {
    g_usleep (seconds * G_TIME_SPAN_SECOND);
}

double laik_tcp_time (void) {
    return (double) g_get_monotonic_time () / (double) G_TIME_SPAN_SECOND;
}
