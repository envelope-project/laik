#include <stdio.h>
#include "laik.h"

int main(int argc, char* argv[])
{
    Laik_Instance* inst = laik_init (&argc, &argv);
    Laik_Group* world = laik_world(inst);
    int myid = laik_myid(world);

    Laik_KVStore* kvs = laik_kvs_new("test", inst);

    // set some values, then sync
    char data[100], key[100];
    laik_kvs_set(kvs, "v1", 2, "1"); // include '\0' terminator into data size
    if (myid == 0) laik_kvs_set(kvs, "v2", 2, "2");
    sprintf(data, "from %d", myid);
    // this should trigger a "update inconsistency panic"
    // laik_kvs_set(kvs, "d", s+1, data);
    sprintf(key,  "d-%d", myid);
    laik_kvs_sets(kvs, key, data);

    laik_kvs_sync(kvs);

    // every process creates own entries of what he sees
    unsigned int n = laik_kvs_count(kvs);
    for(unsigned int i = 0; i < n; i++) {
        Laik_KVS_Entry* e = laik_kvs_getn(kvs, i);
        sprintf(key, "T%d-%s", myid, laik_kvs_key(e));
        laik_kvs_set(kvs, key, laik_kvs_size(e), laik_kvs_data(e,0));
    }

    laik_kvs_sync(kvs);

    if (myid == 0) {
        unsigned int n = laik_kvs_count(kvs);
        printf("Entries: %d\n", n);
        for(unsigned int i = 0; i < n; i++) {
            unsigned int size;
            Laik_KVS_Entry* e = laik_kvs_getn(kvs, i);
            char* d = laik_kvs_data(e, &size);
            printf(" [%2d] Key '%s': '%s' (len %d)\n",
                   i, laik_kvs_key(e), d, size);
        }
    }

    laik_finalize(inst);
}
