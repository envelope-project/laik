/*
 * This file is part of the LAIK library.
 * Copyright (c) 2020 Josef Weidendorfer <Josef.Weidendorfer@gmx.de>
 *
 * LAIK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 3 or later.
 *
 * LAIK is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LAIK_BACKEND_TCP2_H
#define LAIK_BACKEND_TCP2_H

#include "laik.h" // for Laik_Instance

/**
 * Create a LAIK instance for the TCP2 backend
 * 
 * The following command line options are accepted:
 * --laik-conf=XXX          config file
 * --laik-host=XXX,YYY,...  hostnames for this LAIK instance 
 * --laik-port=XXX          base port on each host
 * --laik-tag=XXX           tag for this instance
 * --laik-procs=N           number of processes to start
 * --laik-help              show help for options
 * 
 * Defaults (may be overwritten by setting in config file):
 * - "laik.conf" for config file
 * - "localhost" for hostnames
 * - 7777 for port
 * - own binary name for tag
 * - 1 for number of processes to start, ie. master alone
 * 
 * The process who is started on host XXX and able to catch the
 * port for listing becomes the master for this LAIK instance.
 * All others try to connect to master and register as process
 * wanting to join.
 * If tag does not match, port number is incremented and retried.
 * 
 * Note that a registration/tag test may take some time, as the
 * backend can only check when LAIK gets called by the application.
 */
Laik_Instance* laik_init_tcp2(int* argc, char*** argv);

#endif // LAIK_BACKEND_TCP2_H
