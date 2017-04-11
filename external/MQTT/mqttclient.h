/*
 * mqttclient.c
 *
 *  Created on: Mar 30, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#ifndef INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_
#define INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_

#include "lauSim_com_intf.h"
#include <mosquitto.h>

#define LAST_WILL_TOPIC "last_will"

#define MAX_SUBSCRIBED_TOPIC 32

typedef void (*FP_MSG_CB) (const void* msg, int msglen);
typedef void (*FP_MQTT_CB) (struct mosquitto* mosq, void* pData, const struct mosquitto_message* msg);


typedef struct tag_mosquitto_backend{
	char* topic;
	FP_MSG_CB callback;
}mqtt_cb_list_t;

typedef struct tag_msg_handler_data{
	mqtt_cb_list_t** callbacks;
	int count;;
}mqtt_msg_handler_data_t;

int mqtt_init(
	char* 			client_id,
	char* 			address,
	int				port,
	int				keep_alive,
	com_backend_t* 	com
);

int mqtt_subscribe(
	com_backend_t* com,
	int num_topics,
	char ** topics,
	FP_MSG_CB* usr_callback
);

void mqtt_cleanup(
	com_backend_t* com
);

int mqtt_publish(
	const char* topic,
	const char* buffer,
	int   length,
	com_backend_t* com
);


#endif /* INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_ */
