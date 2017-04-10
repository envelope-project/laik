/*
 * config.h
 *
 *  Created on: Mar 29, 2017
 *      Author: yang
 */

#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

#define DEBUG 1

#ifdef DEBUG
#define HOSTNAME     "localhost"
#define CLIENTID    "lauSim"
#define TOPIC       "envelope/"
#define PAYLOAD     "Hello World!"
#define KEEPALIVE	60
#define PORT		1883
#define QOS         1
#define TIMEOUT     10000L
#endif


#endif /* INC_CONFIG_H_ */
