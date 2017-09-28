
#include "interface/agent.h"

void papi_init(void);
float gettime(void);
void papi_add_counter(int e);
void papi_add_default_counters(void);
void measurement_start(void);
void measurement_stop(void);
void get_counters(int* count, counter_kvp_t* data);

