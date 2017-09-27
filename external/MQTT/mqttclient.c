/*
 * mqttclient.c
 *
 *  Created on: Mar 30, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include "mqttclient.h"

static char* mosq_topic = NULL;
static volatile int mosquitto_run = 1;
static struct mosquitto* mosq;
static usr_cb_onMessage usr_cb;

void signal_handler(int sig){
	(void)sig;
	mosquitto_run = 0;
	free(mosq_topic);
}

void message_decode(
	struct mosquitto * mosq,
	void* pData, 
	const struct mosquitto_message* message
){
	bool match;
	(void) mosq;
	(void) pData;
	mosquitto_topic_matches_sub(mosq_topic, message->topic, &match);

	if(match){
		usr_cb(message->payloadlen, message->payload);
	}
}

void connect_callback(
	struct mosquitto* mosq,
	void* pData, 
	int result
){
	//Logging
	(void) mosq;
	(void) pData;
	(void) result;
}

void register_callback(
	usr_cb_onMessage callback
){
	usr_cb = callback;
}

void start_mosquitto(
	const char* ip, 
	int port,
	const char* username, 
	const char* password,
	const char* topic
){
	char cid[256];
	int rc = 0;
	char hostname[64];

	assert(ip);
	assert(port);
	assert(topic);

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	memset(cid, 0x0, 256);
	gethostname(hostname, 64);
	sprintf(cid, "%s:%d", hostname, getpid());

	mosq_topic = (char*) malloc (sizeof(char) * (strlen(topic)+1));
	
	mosquitto_lib_init();
	mosq = mosquitto_new(cid, 1, 0);

	if(mosq){
		mosquitto_connect_callback_set(mosq, connect_callback);
		mosquitto_message_callback_set(mosq, message_decode);
		
		if(password && username){
			mosquitto_username_pw_set(mosq, username, password);
		}

		rc = mosquitto_connect(mosq, ip, port, 60);
		mosquitto_subscribe(mosq, NULL, mosq_topic, 0);

		while(mosquitto_run){
			rc = mosquitto_loop(mosq, -1, 1);
			if(mosquitto_run && rc){
				sleep(100);
			}
		}

		mosquitto_disconnect (mosq);
		mosquitto_destroy (mosq);
		mosquitto_lib_cleanup();
	}
}

void stop_mosquitto(
	void
){
	mosquitto_run = 0;
}

