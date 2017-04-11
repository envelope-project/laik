/*
 * laik_mqtt.c
 *
 *  Created on: Apr 10, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#include <stdio.h>
#include "laik_intf.h"

#define LAIK_MQTT_CLIENT

#ifdef LAIK_MQTT_CLIENT

int laik_handler (
	LaikExtMsg* list
){
	int i;

	if(!list){
		return -1;
	}

	printf("Failing Nodes: \n");
	for(i=0; i<list->n_failing_nodes; i++){
		printf("%s\n", list->failing_nodes[i]);
	}

	printf("Spare Nodes: \n");
	for(i=0; i<list->n_spare_nodes; i++){
		printf("%s\n", list->spare_nodes[i]);
	}
	return 0;
}



int main(
	int argc,
	char** argv
){

	char exit;
	init_ext_com(&laik_handler, NULL,
			"localhost", 1883, 60, NULL, NULL);

	while (1){
		exit = getchar();
		if(exit == 'q'){
			cleanup_ext_com();
			break;
		}else{
			printf("Enter q to exit \n");
		}
	}
}

#endif
