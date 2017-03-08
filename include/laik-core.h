/* 
 * This file is part of the LAIK parallel container library.
 * Copyright (c) 2017 Josef Weidendorfer
 * 
 * LAIK is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU Lesser General Public License as   
 * published by the Free Software Foundation, version 3.
 *
 * LAIK is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LAIK_CORE_H_
#define _LAIK_CORE_H_

// configuration for a LAIK instance (there may be multiple)
typedef struct _Laik_Instance Laik_Instance;

// LAIK error struct
typedef struct _Laik_Error Laik_Error;


/*********************************************************************/
/* Core LAIK API
 *********************************************************************/


/**
 * Return number of LAIK tasks available (within this instance)
 */
int laik_size(Laik_Instance*);

/**
 * Return rank of calling LAIK task (within this instance)
 */
int laik_myid(Laik_Instance*);

/**
 * Shut down communication and free resources of this instance
 */
void laik_finalize(Laik_Instance*);


#endif // _LAIK_CORE_H_
