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

#include "laik_intf.h"
#include "mqttclient.h"
#include "lauSim_com_intf.h"

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
	char hostname[64];
	char* clientid;
	char* topic[1] = {NODE_STATUS_TOPIC};
	FP_MSG_CB callbacks[1] = {msg_cb};


	assert(fp_backend);
	laik_fp = fp_backend;
	laik_cleanup = cleanup;

	memset(&com, 0x0, sizeof(com_backend_t));

	/* according to posix 1.0 */
	gethostname(hostname, 64);
	if(hostname[64] != '\0'){
		hostname[64] = '\0';
	}

	/* generate a new client id */
	clientid = (char*) malloc (strlen(hostname) + sizeof("LAIK_") + 1);
	sprintf(clientid, "LAIKpart_%s", hostname);

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

	assert(msglen);
	assert(msg);

	laikmsg = laik_ext_msg__unpack(NULL, msglen, (unsigned char*) msg);
	if(!laikmsg){
		fprintf(stderr, "Omitted 1 MQTT Msg, Cannot unpack. Size: %d", msglen);
	}
	laik_fp(laikmsg);

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
