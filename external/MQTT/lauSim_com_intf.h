/*
 * lauSim_com_intf.h
 *
 *  Created on: Mar 30, 2017
 *      Author: Dai Yang
 *	(C) 2017 Technische Universitaet Muenchen, LRR
 */

#ifndef INC_LAUSIM_COM_INTF_H_
#define INC_LAUSIM_COM_INTF_H_

#define COM_INTF_VER 1

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

com_backend_t* init_com(
	com_type_t type,
	char* 		addr,
	int 		port
);

#endif /* INC_LAUSIM_COM_INTF_H_ */
