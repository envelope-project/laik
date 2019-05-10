#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "./include/laik-internal.h"
#include "./include/laik/ext.h"
#include "include/laik/agent.h"


Laik_Instance* inst;

int tmp = 0;


#define check(a,b) do {if ((a) != (b)) return -1;} while(0)
#define UNUSED(x) (void)(x)

#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#else
#define DEBUG_PRINT(...) do{ } while ( false )
#endif


int main(int argc, char* argv[])
{
	char top_name[512];
	char* name = "hostNodeName";
	snprintf(top_name, sizeof(top_name) - 1, "fast/migfra/%s/task", name);

	//(1) Laik_Instance* inst;
	inst = laik_init (&argc, &argv);
	DEBUG_PRINT("laik init done. \n");


	//(2) laik_ext_init(inst);
	laik_ext_init(inst);
    printf("blob ext_init \n");

	//(3) laik_ext_loadagent     or    laik_ext_load_agent_from_file
	char *path = "./external/file/libfileagent.so";
	laik_ext_load_agent_from_file( inst, path, 0, 0);
	printf("laik_ext_load_agent done ! \n");

	//(4) laik_get_failed
	node_uid_t failed_ranks;
	int* dummy = & tmp;

	laik_get_failed( inst, dummy, &failed_ranks, 10);
	printf("get_failed (Final) num_failed: %d failed: %s \n", *dummy, failed_ranks.uid);

	//(5) laik_ext_cleanup
	//laik_ext_cleanup(inst);
	//(6) laik finalize

	char* myloc = laik_mylocation(inst);
	int* listofranks;
	size_t szranks;
	inst->backend->get_rank_by_nodes(inst, &myloc, 1, &listofranks, &szranks);

	for(size_t i=0; i<szranks; i++){
		printf("Failing ranks %d\n", listofranks[i]);
	}

	free(myloc);
	free(listofranks);
	laik_finalize(inst);

	return 0;
}
