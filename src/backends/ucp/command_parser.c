//*********************************************************************************
#include "command_parser.h"
#include "laik-internal.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>

//*********************************************************************************
static const char *RESIZE_COMMANDS_FILE_PATH = "/home/ubuntu/BachelorThesis/laik/src/backends/ucp/resize_commands.txt";

/// TODO: this is not required for TCP since it can just POLL the socket, but maybe useful for other connection setup frameworks
// Specifies how many newcomers to expect as upper limit
static const char *COMMAND_ADD = "ADD";
// Specifies ONE rank that needs to be removed
/// TODO: Process ID instead of rank
static const char *COMMAND_REMOVE = "REM";

// total command length per line should not exceed 32 byte
static const size_t MAX_LINE_LENGTH = 32;

// Command length is expected to be 4 Bytes long within the code (including '\0' Byte)
static const size_t COMMAND_LENGTH = 4;

/* _Static_assert(COMMAND_LENGTH == sizeof(COMMAND_ADD), "ADD Command has incorrect length");
_Static_assert(COMMAND_LENGTH == sizeof(COMMAND_REMOVE), "REM Command has incorrect length"); */

//*********************************************************************************
// Error handling from https://www.techonthenet.com/c_language/standard_library_functions/stdlib_h/strtol.php
static size_t string_to_long_unsigned(const char *string)
{
    /// TODO: eptr handling != '\0'
    char *eptr;
    long result;

    result = strtol(string, &eptr, 10);

    /* If the result is 0, test for an error */
    if (result == 0)
    {
        /* If a conversion error occurred, display a message and exit */
        if (errno == EINVAL)
        {
            laik_log(LAIK_LL_Debug, "Conversion error occurred: %d\n", errno);
        }
    }
    /* If the result is equal to LONG_MIN or LONG_MAX, test for a range error */
    else if (result == LONG_MIN || result == LONG_MAX)
    {
        /* If the value provided was out of range, display a warning message */
        if (errno == ERANGE)
        {
            laik_log(LAIK_LL_Debug, "The value provided was out of range\n");
        }

        result = 0;
    }
    else if (result < 0)
    {
        // negative numbers do not make sense here
        laik_panic("Negative argument is not valid!");
    }

    return (size_t)result;
}

//*********************************************************************************
static inline void parse_add_argument(ResizeCommand *resize_command, const char *line)
{
    // +1 for the ':' after the command name
    char argument_string[MAX_LINE_LENGTH - COMMAND_LENGTH];
    memset(argument_string, '\0', sizeof(argument_string));

    size_t i = COMMAND_LENGTH;
    while (line[i] != '\n' && line[i] != '\0')
    {
        argument_string[i - COMMAND_LENGTH] = line[i];
        ++i;
    }

    /// TODO: is this safe enough?
    size_t argument = string_to_long_unsigned(argument_string);

    laik_log(LAIK_LL_Debug, "Parsed increase command argument <%lu> from string <%s> in line %s", argument, argument_string, line);

    // resize command struct is zero initialized
    resize_command->number_to_add += argument;
}

//*********************************************************************************
static inline void parse_remove_argument(ResizeCommand *resize_command, const char *line)
{
    // +1 for the ':' after the command name
    char argument_string[MAX_LINE_LENGTH - COMMAND_LENGTH];
    memset(argument_string, '\0', sizeof(argument_string));

    size_t i = COMMAND_LENGTH;
    while (line[i] != '\n' && line[i] != '\0')
    {
        argument_string[i - COMMAND_LENGTH] = line[i];
        ++i;
    }

    /// TODO: is this safe enough?
    size_t argument = string_to_long_unsigned(argument_string);

    // for now removing the master is not supported
    assert(argument > 0);

    laik_log(LAIK_LL_Debug, "Parsed remove command argument <%lu> from string <%s> in line %s", argument, argument_string, line);
    // resize command struct is zero initialized
    resize_command->number_to_remove++;

    if (resize_command->ranks_to_remove == NULL)
    {
        resize_command->ranks_to_remove = malloc(sizeof(size_t));
    }
    else
    {
        /// TODO: realloc steps could be bigger for better performance
        resize_command->ranks_to_remove = realloc(resize_command->ranks_to_remove, resize_command->number_to_remove);
    }

    if (resize_command->ranks_to_remove == NULL)
    {
        laik_panic("Not enough memory for ranks to remove array within ParseCommand struct");
    }
    else
    {
        resize_command->ranks_to_remove[resize_command->number_to_remove - 1] = argument;
    }
}

//*********************************************************************************
ResizeCommand *parse_resize_commands(void)
{
    FILE *commands_file = fopen(RESIZE_COMMANDS_FILE_PATH, "r");

    if (commands_file != NULL)
    {
        laik_log(LAIK_LL_Debug, "Resize Commands file was found. Starting to parse...");
        ResizeCommand *resize_command = calloc(sizeof(ResizeCommand), sizeof(char));

        if (resize_command != NULL)
        {
            char line[MAX_LINE_LENGTH];

            while (fgets(line, sizeof(line), commands_file) != NULL)
            {
                char command[COMMAND_LENGTH];
                // ensures null terminated string
                memset(command, '\0', sizeof(command));
                strncpy(command, line, sizeof(command) - 1);

                if (strcmp(command, COMMAND_ADD) == 0)
                {
                    parse_add_argument(resize_command, line);
                }
                else if (strcmp(command, COMMAND_REMOVE) == 0)
                {
                    parse_remove_argument(resize_command, line);
                }
                // more commands can be added here
                else
                {
                    laik_log(LAIK_LL_Debug, "Command <%s> is not supported\n", command);
                }
            }

            fclose(commands_file);
            return resize_command;
        }
        else
        {
            fclose(commands_file);
            laik_log(LAIK_LL_Error, "Not enough memory for ParseCommand struct");
        }
    }
    else
    {
        laik_log(LAIK_LL_Debug, "Resize Commands file was not found. Nothing to be done!");
    }

    return NULL;
}

//*********************************************************************************
void free_resize_commands(ResizeCommand *resize_commands)
{
    if (resize_commands != NULL)
    {
        if (resize_commands->ranks_to_remove != NULL)
        {
            free(resize_commands->ranks_to_remove);
        }
        free(resize_commands);
    }
}

//*********************************************************************************