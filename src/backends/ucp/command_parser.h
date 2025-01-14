//*********************************************************************************
#pragma once

//*********************************************************************************
#include <stdlib.h>

//*********************************************************************************
typedef struct _ResizeCommand
{
    size_t number_to_add;    // specifies number of newcomers (upper limit)
    size_t number_to_remove; // specifies the size of ranks_to_remove
    size_t *ranks_to_remove; // list of lids that are expected to be removed
} ResizeCommand;

//*********************************************************************************
/*
    Returns NULL if there was an error during parsing
 */
ResizeCommand *parse_resize_commands(void);

//*********************************************************************************
/*
    Frees entire ResizeCommand Struct
    Can also handle NULL pointers
 */
void free_resize_commands(ResizeCommand *resize_commands);

//*********************************************************************************
