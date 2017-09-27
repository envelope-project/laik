/*
 * mqttclient.c
 *
 *  Created on: Mar 30, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#ifndef INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_
#define INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_

#include <mosquitto.h>

typedef void (*usr_cb_onMessage) (int msg_len, const char* msg);

void start_mosquitto(
	const char*, 
	int,
	const char*,
	const char*, 
	const char*
);

void stop_mosquitto(
	void
);

void register_callback(
	usr_cb_onMessage callback
);

#endif /* INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_ */
