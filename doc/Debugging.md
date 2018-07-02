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
    == LAIK-0000-T00 0002.01  0:00.000 | MPI backend initialized (host:1234)
    == LAIK-0000-T01 0002.01  0:00.000 | MPI backend initialized (host:1235)
    == LAIK-0001-T00 0001.01  0:00.000 | Enter phase 'init'
    == LAIK-0001-T00 0002.01  0:00.000 | new 1d space 'space-0': [0;1000000[
    == LAIK-0001-T00 0003.01  0:00.000 | new data 'data-0':
    .. LAIK-0001-T00 0003.02  0:00.000 |  type 'double', space 'space-0'
```

The prefix uses an log counter (which gets incremented on phase/iteration
changes specified by applications via `laik_set_iteration`/`laik_set_phase`),
and it has the following format:

```
    == LAIK-<logctr>-T<task> <itermsgctr>.<line> <wtime>
```

With
* logctr : counter incremented at iteration/phase borders
* task   : task rank in this LAIK instance
* msgctr : log message counter, reset at each logctr change
* line   : a line counter if a log message consists of multiple lines
* wtime  : wall clock time since LAIK instance initialization

Instead of `==`, points are used on follow-up lines of one message to
better spot the border to the next message (or allowing grepping to next
message).


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
