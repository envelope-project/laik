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

#define LAST_WILL_TOPIC "last_will"
#define COM_INTF_VER 1
#define MAX_SUBSCRIBED_TOPIC 32

typedef void (*FP_MSG_CB) (const void* msg, int msglen);
typedef void (*FP_MQTT_CB) (struct mosquitto* mosq, void* pData, const struct mosquitto_message* msg);

typedef struct tag_com com_backend_t;
typedef enum tag_com_type com_type_t;

typedef int (*FP_SEND) (
		char* 	channel,
		char* 	buffer,
		int		length,
		com_backend_t* backend
);

typedef int (*FP_RECV) (
		char*	channel,
		char*	buffer,
		int* 	length,
		com_backend_t* backend
);

typedef void (*FP_COM_INIT) (
		com_type_t	type,
		char* 		addr,
		int			port
		);

enum tag_com_type{
	COM_MQTT 	= 1,
	COM_TCP 	= 2,
	COM_UDP 	= 3,
	COM_SOCKET 	= 4,
	COM_PIPE 	= 5,
	COM_FILE 	= 6
};

struct tag_com{
	int 		version;		/* Com Interface Version */
	void* 		pData;			/* Struct for com backend */
	int			isConnected;	/* Connection static */
	char*		addr;			/* Addres of other side */
	int			port;			/* Port */
	com_type_t 	type;			/* Connector type */

	void*		com_entity;

	FP_SEND 	send;			/* Default Synchronous Send function */
	FP_RECV 	recv;			/* Default Syncrhonous Recv function */
};


typedef struct tag_mosquitto_backend{
	char* topic;
	FP_MSG_CB callback;
}mqtt_cb_list_t;

typedef struct tag_msg_handler_data{
	mqtt_cb_list_t** callbacks;
	int count;;
}mqtt_msg_handler_data_t;


com_backend_t* init_com(
	com_type_t type,
	char* 		addr,
	int 		port
);


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
	char* topic,
	char* buffer,
	int   length,
	com_backend_t* com
);


#endif /* INC_BACKEND_MOSQUITTO_MQTTCLIENT_C_ */
