# Debugging

## Logging

### Logging output from the application

You can use the logging functionality of LAIK to print out debug information:

```C
    laik_log(<level>, <formatstring>, ...)
```

The formatstring and optional parameters work exactly as in printf().
Defined levels are

```C
    LAIK_LL_Debug   = 1,
    LAIK_LL_Info    = 2,
    LAIK_LL_Warning = 3, // prefix with "Warning"
    LAIK_LL_Error   = 4, // prefix with "Error"
    LAIK_LL_Panic   = 5  // terminate
```

### Logging output

Each message will be prefixed in a way to enable the logging to be
piped through the UNIX 'sort' command to get stable output also
with arbitrary interleaving of simultaneously running LAIK tasks.
Example (showing LAIK debug output):

```
    == LAIK-0000.00 T 0/2 0001.01                 MPI backend initialized
    == LAIK-0000.00 T 1/2 0001.01                 MPI backend initialized
    == LAIK-0001.00 T 0/2 0001.01 init            new 1d space 'space-0'
    == LAIK-0001.00 T 0/2 0002.01 init            new 1d data 'data-0'
    == LAIK-0001.00 T 1/2 0001.01 init            new 1d space 'space-0': [0-999999]
    == LAIK-0001.00 T 1/2 0002.01 init            new 1d data 'data-0'
    == LAIK-0002.00 T 1/2 0001.01 element-wise    new partitioning 'partng-2'
    ...
```

The prefix uses phases and iteration counters (specified by applications via
`laik_set_phase` and `laik_set_iteration`), and it has the following format:

```
    == LAIK-<phasectr>.<iter> T<task>/<tasks> <phasemsgctr>.<line> <pname>
```

With
* phasectr    : a counter incremented on every phase change
* iter        : iteration counter set by application
* task        : task rank in this LAIK instance
* phasemsgctr : log message counter, reset at each phase change
* pname       : phase name set by application

A Phases and iteration counters enable applications to give


### Logging Control

At runtime, all log messages with a level equal or larger than a given
logging level will be printed. The default logging level is the specific
level 0, which deactivates any logging. The logging level can set with
the environment variable LAIK_LOG. E.g. use

```
    LAIK_LOG=2 ./mylaikprogram
```

to log all messages with a level equal to or larger than LAIK_LL_Info.
The logging level can be set (and overwrites the environemt variable)
in the application with

```
    laik_set_loglevel(<level>);
```

By default, each LAIK task will output logging messages.
A filter specification in LAIK_LOG can limit the output to a given
LAIK task or range of LAIK tasks:

```
    LAIK_LOG=2:0 ./mylaikprogram
```
Only output logging from task 0.

```
    LAIK_LOG=2:1-2 ./mylaikprogram
```
Only output logging from task 1 and task 2.
