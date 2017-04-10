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
#include <unistd.h>

#include "mqttclient.h"
#include "config.h"

static
void msg_handler(
	struct mosquitto * mosq,
	void * pData,
	const struct mosquitto_message * msg
);

static
char hostname[64];

int mqtt_init(
	char* 			client_id,
	char* 			address,
	int				port,
	int				keep_alive,
	com_backend_t* 	com
){
	int ret;
	int i;
	struct mosquitto *mosq;
	mqtt_cb_list_t** list;
	mqtt_msg_handler_data_t* pData;

	assert(com);

	gethostname(hostname, 64);
	if(hostname[64] != '\0'){
		hostname[64] = '\0';
	}

	/* create handler list */
	list = (mqtt_cb_list_t**) malloc
			(sizeof(mqtt_cb_list_t*) * MAX_SUBSCRIBED_TOPIC);

	for(i=0; i<MAX_SUBSCRIBED_TOPIC; i++){
		list[i] = (mqtt_cb_list_t*) malloc (sizeof(mqtt_cb_list_t));
	}
	pData = (mqtt_msg_handler_data_t*) malloc
			(sizeof(mqtt_msg_handler_data_t));
	pData->count = 0;
	pData->callbacks = list;

	/* Initialize Library */
	mosquitto_lib_init();

	/* create a new instance */
	mosq = mosquitto_new (client_id, 1, pData);
	if(!mosq){
		return -1;
	}

	/* set message handler callback */
	mosquitto_message_callback_set(mosq, msg_handler);

	/* set last will to current hostname */
	ret = mosquitto_will_set(mosq, LAST_WILL_TOPIC, strlen(hostname), hostname, 0, 0);

	/* set connection data */
	if(!address){
		address = "localhost";
	}

	if(!port){
		port = 1883;
	}

	if(!keep_alive){
		keep_alive = 60;
	}

	// TODO: Set UsrName und Password

	/* connect to broker */
	ret = mosquitto_connect(mosq, address, port, keep_alive);
	if(ret!=MOSQ_ERR_SUCCESS)
	{
		return -1;
	}
	/* set reconnection parameters */
	mosquitto_reconnect_delay_set(mosq, 1, 30, 0);

	/* start mosquitto client thread */
	ret = mosquitto_loop_start(mosq);
	if(ret!=MOSQ_ERR_SUCCESS)
	{
		return -1;
	}

	/* generate the communication handle */
	com->type = COM_MQTT;
	com->pData = pData;
	com->isConnected = 1;
	com->addr = address;
	com->port = port;
	com->recv = NULL;
	com->send = mqtt_publish;
	com->com_entity = mosq;

	return 0;
}

int mqtt_subscribe(
	com_backend_t* com,
	int num_topics,
	char ** topics,
	FP_MSG_CB* usr_callback
){

	int i = 0, j = 0;
	int ret;
	struct mosquitto* mosq;
	mqtt_msg_handler_data_t* pData;
	mqtt_cb_list_t** callbacks;
	char* temp;

	assert(com->type == COM_MQTT);
	assert(usr_callback);
	assert(topics);

	mosq = (struct mosquitto*) com->com_entity;
	pData = com->pData;
	callbacks = pData->callbacks;

	for(; i<num_topics; i++)
	{
		/* subscribe topic */
		ret = mosquitto_subscribe(mosq, NULL, topics[i], 2);
		if(ret != MOSQ_ERR_SUCCESS)
		{
			return -1;
		}

		/* copy topic name */
		temp = (char*) malloc (strlen(topics[i]) + 1);
		memcpy(temp, topics[i], strlen(topics[i] + 1));

		/* save user callback */
		j = pData->count + i;
		callbacks[j]->topic = temp;
		callbacks[j]->callback = usr_callback[i];
		pData->count += 1;
	}

	return 0;
}


static
void msg_handler(
	struct mosquitto * mosq,
	void * pData,
	const struct mosquitto_message* msg
){
	int i;
	mqtt_msg_handler_data_t* hnd;
	mqtt_cb_list_t** callbacks;
	mqtt_cb_list_t* current;

	hnd = (mqtt_msg_handler_data_t*) pData;
	callbacks = hnd->callbacks;

	/* find out the corresponding topic callback and handle it */
	for(i=0; i<hnd->count; i++)
	{
		current = callbacks[i];
		if(strcmp(msg->topic, current->topic))
		{
			current->callback(msg->payload, msg->payloadlen);
			break;
		}
	}
}

void mqtt_cleanup(
	com_backend_t* com
){
	mqtt_msg_handler_data_t* pData;
	mqtt_cb_list_t** callbacks;
	int i;

	assert(com);
	assert(com->type == COM_MQTT);

	pData = com->pData;
	callbacks = pData->callbacks;

	/* close connections */
	mosquitto_loop_stop((struct mosquitto*) com->com_entity, 1);
	mosquitto_disconnect((struct mosquitto*) com->com_entity);
	mosquitto_destroy((struct mosquitto*) com->com_entity);
	mosquitto_lib_cleanup();

	/* clean callbacks */
	for(i=0; i<pData->count; i++){
		free(callbacks[i]->topic);
	}
	for(i=0; i<MAX_SUBSCRIBED_TOPIC; i++){
		free(callbacks[i]);
	}

	free(callbacks);
	free(pData);

}


int mqtt_publish(
	const char* topic,
	const char* buffer,
	int   length,
	com_backend_t* com
){

	struct mosquitto* mosq;
	int ret;

	assert(com->type == COM_MQTT);
	mosq = (struct mosquitto*) com->com_entity;

	ret = mosquitto_publish(mosq, NULL, topic, length, buffer, 0, 0);

	if(ret != MOSQ_ERR_SUCCESS){
		return -1;
	}

	return 0;
}
