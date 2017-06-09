/*
 * laik_intf.c
 *
 *  Created on: Apr 10, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <uuid/uuid.h>

#include "laik_intf.h"
#include "mqttclient.h"

static
com_backend_t com;
static
LAIK_EXT_FAIL laik_fp;
static
LAIK_EXT_CLEANUP laik_cleanup = NULL;

void msg_cb (
	const void* msg,
	int msglen
);

void init_ext_com(
	LAIK_EXT_FAIL 		fp_backend,
	LAIK_EXT_CLEANUP 	cleanup,
	char* 				addr,
	int 				port,
	int					keepalive,
	char* 				username,	/* unsupported yet */
	char* 				password	/* unsupported yet */
){
	int ret = 0;
	char* clientid;
	char* topic[1] = {NODE_STATUS_TOPIC};
	FP_MSG_CB callbacks[1] = {msg_cb};
	uuid_t uuid;
	char uuid_str[37];

	/* Unused currently TODO!! */
	(void) username;
	(void) password;

	assert(fp_backend);
	laik_fp = fp_backend;
	laik_cleanup = cleanup;

	/* generate UUID */
	uuid_generate_time_safe(uuid);
	uuid_unparse_lower(uuid, uuid_str);

	memset(&com, 0x0, sizeof(com_backend_t));

	/* generate a new client id */
	clientid = (char*) malloc (37 + sizeof("LAIKpart_") + 1);
	sprintf(clientid, "LAIKpart_%s", uuid);

	/* get a new mqtt instance */
	ret = mqtt_init(clientid, addr, port, keepalive, &com);
	free(clientid);
	assert (!ret);

	mqtt_subscribe(&com, 1, topic, callbacks);

}



void msg_cb (
	const void* msg,
	int msglen
){
	LaikExtMsg* laikmsg;
	int ret = 0xFF;

	assert(msglen);
	assert(msg);

	laikmsg = laik_ext_msg__unpack(NULL, msglen, (unsigned char*) msg);
	if(!laikmsg){
		fprintf(stderr, "Omitted 1 MQTT Msg, Cannot unpack. Size: %d", msglen);
	}

	/* The function pointer must be blocking until the processing is done!!!! */
	ret = laik_fp(laikmsg);
	assert(ret == 0);

	laik_ext_msg__free_unpacked(laikmsg, NULL);
}

void cleanup_ext_com(
	void
){
	if(laik_cleanup){
		laik_cleanup();
	}
	mqtt_cleanup(&com);
}
