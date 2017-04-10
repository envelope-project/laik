/*
 * mqtt_publisher.c
 *
 *  Created on: Apr 10, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#include <stdlib.h>
#include "lauSim_com_intf.h"
#include "mqttclient.h"
#include "proto/laik_ext.pb-c.h"


#define MQTT_PUBLISH

#ifdef MQTT_PUBLISH

#define NODE_STATUS_TOPIC "envelope/status"



int main (
	int argc,
	char** argv
){

	com_backend_t com;
	LaikExtMsg msg = LAIK_EXT_MSG__INIT;
	char* fnodes[2] = {"n01", "n02"};
	char* spnodes[2] = {"n99", "n98"};
	void* buf;
	int len;

	mqtt_init("Foo", "localhost", 1883, 60, &com);

	msg.n_failing_nodes = 2;
	msg.failing_nodes = fnodes;

	msg.n_spare_nodes = 2;
	msg.spare_nodes = spnodes;

	len = laik_ext_msg__get_packed_size(&msg);
	buf = malloc(len);

	laik_ext_msg__pack(&msg, buf);

	com.send(NODE_STATUS_TOPIC, buf, len, &com);

	mqtt_cleanup(&com);

	free(buf);

	return 0;
}

#endif
