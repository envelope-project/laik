
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <dlfcn.h>
#include <yaml.h>

#include "laik.h"
#include "fileagent.h"

static int isInited;

int num_failed;


#ifdef DEBUG
#define DEBUG_PRINT(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( false )
#else
#define DEBUG_PRINT(...) do{ } while ( false )
#endif

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t* mutex = &m;

#define check(a,b) do {if ((a) != (b)) return -1;} while(0)
#define UNUSED(x) (void)(x)


/**
 * @brief  
 * @note   
 * @param  fh: 
 * @param  n_f: 
 * @param  result: 
 * a list to store destinations
 * @retval 
 */
int parse_file(FILE * fh, int* n_f, node_uid_t* failed_node, node_uid_t** destinations ){

	yaml_node_t* root;
	yaml_parser_t parser;
	yaml_node_t* node;
	yaml_document_t doc;

	char topic_buf[512];
	char *failing_host;
	char *cmp;
	int i = 1;
	num_failed = 0;
	//pthread_mutex_lock(&rcv_mutex);

	assert(yaml_parser_initialize(&parser));
	yaml_parser_set_input_file(&parser, fh);
	if (!yaml_parser_load(&parser, &doc)) {
		 DEBUG_PRINT("Yaml Parser could not be loaded ! ");
		 return -1;
	}
	root = yaml_document_get_root_node(&doc);

	if(root == NULL ) DEBUG_PRINT("No root node! \n");
	else DEBUG_PRINT("root node get OK  L:4\n");

	// allocate buffers for the topic
	//FIXME : find a generic size for topic_buf
	node = yaml_document_get_node(&doc, i);
	while(node != NULL) {
		if (!strncmp("Topic",(char*) node->data.scalar.value, strlen("Topic"))) {// find hostname  from the topic
			i++;
			node = yaml_document_get_node(&doc, i);
			//topic_buf = (char*) malloc(sizeof(char)*(strlen((char*)node->data.scalar.value) + 1));
			strncpy(topic_buf, (char*) node->data.scalar.value, 511);
			DEBUG_PRINT(" topic:  %s \n", topic_buf);
			strtok(topic_buf, "/");      	// "fast"
			strtok(NULL, "/");           	// "migfra"
			failing_host= strtok(NULL, "/");// "hostname"
			// find out if this is a task
			cmp = strtok(NULL, "/");	// "task"
			// Need to decode Message - use YAML Parser
			if (cmp == NULL) {
				DEBUG_PRINT("USELESS Message \n");
				break;
			}
		}else if (!strncmp("task", (char*) node->data.scalar.value, strlen("task"))){
			i++;
			node = yaml_document_get_node(&doc, i);
			if (strncmp((char*) node->data.scalar.value,"evacuate node", strlen("evacuate node"))){
				DEBUG_PRINT("FILE DOES NOT CONTAIN AN EVACUATION REQUEST !");
				break;
			}else{printf("Evacuation request received ! \n");}
		}else if(!strncmp("Destinations", (char*)node->data.scalar.value, MAX_UID_LENGTH-1)){
			i=i+2; //skip modes and so one
			node = yaml_document_get_node(&doc, i);
			while(strncmp("Parameter", (char*)node->data.scalar.value, MAX_UID_LENGTH-1)) {
				if (num_failed==0){
					*destinations = (node_uid_t*) calloc(1, sizeof(node_uid_t));
				}else{
					*destinations = (node_uid_t*) realloc(*destinations, (num_failed+1)*sizeof(node_uid_t));
				}
				strncpy(((*destinations)+num_failed)->uid, (char*)(node->data.scalar.value), MAX_UID_LENGTH-1);
				num_failed++;
				i++;
				node = yaml_document_get_node(&doc, i);
			}
		}
		i++;
		node = yaml_document_get_node(&doc, i);
	}
	*n_f = num_failed;

	for(int k = 0 ; k < num_failed; k++ )
	{
		DEBUG_PRINT(" %s   - % d \n",((*result) + k)->uid, k);
		fflush(stdout);
	}

	strncpy(failed_node->uid, failing_host, MAX_UID_LENGTH-1);

	// Free the YAML Parser
	yaml_parser_delete(&parser);
	//close file
	assert(!fclose(fh));

	return 0;
}


void mqtt_getfailed(
    int* n_failed,
	node_uid_t** result
){
	//(1)chek if file exists
	FILE *fh = fopen ("evacuation.yaml", "r");
	if (!fh ){
		 fclose(fh);
		 DEBUG_PRINT("File doesn't exist (no failing nodes or file error)");
		 //(2)return n_failed 0 if doesn't exist
		 *n_failed = 0;

	 }else{
		 // (3)if exists parse file  & (4)fill result and n_failed
		 node_uid_t test;
		 parse_file(fh, n_failed, &test ,result );
		 printf("%s\n", test.uid);
		 DEBUG_PRINT("n_failed: % d \n",*n_failed);
		 for(int k = 0 ; k < *n_failed; k++ )
		{
			DEBUG_PRINT(" Res: %s   - % dtar \n",((*result)+k)->uid, k);
			fflush(stdout);
		}

	 }

}


// returns zero if no faults (no evacuation.yaml file), 1 otherwise
int mqtt_peekfailed(
){
	//(1)chek if file exists
	if (access ("evacuation.yaml", F_OK) ){
		 puts("File doesn't exist (no failing nodes or file error)");
		 //(2)return n_failed 0 if doesn't exist
		 return 0;
	 }else
		 // (3)if exists return 0;
		 return 1;
}

void mqtt_detach(
    void
){
    num_failed = 0;
    //free_backbuffer();

}

Laik_Agent* agent_init(
    int argc, 
    char** argv
){
	DEBUG_PRINT("In agent_init L:3 \n");

	UNUSED(argc);
	UNUSED(argv);

    //(1) Instantiate the agent object that will be added to the agents of Laik
    Laik_Ft_Agent* me = (Laik_Ft_Agent*) calloc (1, sizeof(Laik_Ft_Agent));
    assert(me);
    Laik_Agent* myBase = &(me->base);

    myBase->name = "Fault Tolerant Agent";
    myBase->id = 0x03;
    myBase->isAlive = 1;
    myBase->isInitialized = 1;
    myBase->type = LAIK_AGENT_FT;

    myBase->detach = mqtt_detach;
    //myBase->reset = mqtt_reset;

    me->getfail = mqtt_getfailed; 
    me->peekfail = mqtt_peekfailed;

    /*//(2)create the common storage space to store the list with names f failed nodes
    common_list = (list*) calloc(1,sizeof(list));
    common_list->failed_list = (node_uid_t*) calloc (1, sizeof(node_uid_t));
    //(3) pass the location of the memory to external.c interface
    me->ft_data = common_list; //for external.c interface*/
    isInited = 1;
    
    return (Laik_Agent*) me;

}
