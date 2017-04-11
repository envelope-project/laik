/*
 * laik_intf.h
 *
 *  Created on: Apr 7, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#ifndef INC_BACKEND_MOSQUITTO_LAIK_INTF_H_
#define INC_BACKEND_MOSQUITTO_LAIK_INTF_H_

#include "proto/laik_ext.pb-c.h"

#define NODE_STATUS_TOPIC "envelope/status"
#define MAX_LAIK_MSG_SIZE 2048

typedef int (*LAIK_EXT_FAIL) (LaikExtMsg* list);
typedef int (*LAIK_EXT_CLEANUP) ();

void init_ext_com(
	LAIK_EXT_FAIL fp_backend,
	LAIK_EXT_CLEANUP cleanup,
	char* addr,
	int port,
	int keepalive,
	char* username,
	char* password
);

void cleanup_ext_com(
	void
);




#endif /* INC_BACKEND_MOSQUITTO_LAIK_INTF_H_ */
