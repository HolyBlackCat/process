Stdout_Null
Stdout_Inherit
Stdout_Attach
Stdout_ToStderr

Stderr_Null
Stderr_Inherit
Stderr_Attach
Stderr_ToStdout

Stderr_Null
Stderr_Inherit
Stderr_Attach




                  allows new?    when existing:
CREATE_ALWAYS         yes           truncate
CREATE_NEW            yes           error
OPEN_ALWAYS           yes           keep
OPEN_EXISTING         no            keep
TRUNCATE_EXISTING     no            truncate


O_CREAT | O_TRUNC     yes           truncate
O_CREAT | O_EXCL      yes           error
O_CREAT               yes           keep
0                     no            keep
O_TRUNC               no            truncate




Pipes:

* On POSIX:
  * Stream IDs: `STD{OUT,ERR,IN}_FILENO`
  * Merging stdout and stderr by redirecting one of them directly to another, no pipe.
  * Recommend ignoring `SIGPIPE`.
  * Document not ignoring `SIGCHLD` because that would destroy every child handle before we can `waitpid` it. A custom handler is in theory possible, if you make sure to not destroy the handle.
  * Add `FD_CLOEXEC` to the pipe just in case.
  * An option to set `O_NONBLOCK` to our end of the pipe.

* On Windows:
  * Can make pipes nonblocking with `PIPE_NOWAIT`.
  * Must enable pipe inheritance with `HANDLE_FLAG_INHERIT`
  * Must use a named pipe if at least one end is async
  * `PIPE_REJECT_REMOTE_CLIENTS` for named pipes?
  * For named pipes, create the read end with `CreateNamedPipe` and the write end with `CreateFile`.
    It seems to work both ways, but if someone wants to break our app, they could try to connect to the pipe themselves after `CreateNamedPipe` but before I do `CreateFile`, and it's apparently harder to do if `CreateNamedPipe` creates the read end, since the permissions to connect to the write end are harder to come by, at least if Claude is to be trusted.
    Use inline relaxed atomci as a counter, and also add the address of the atomic to the name in case there are multiple dlls.
  * `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` to only inherit certain handles.
  * `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS` to only signal IOCP if the op was delayed.


* POSIX: close all FDs in the child, like SDL does. Reproc does this too. Behind a knob.


* Windows: a flag to hide the console


* Async/polling:

    MakePipe needs async variants

    OpenFile needs async variants

Compilation tests:
    with and without EM_PROC_HAVE_PIPE2
    with and without EM_PROC_CAN_DETECT_CORE_DUMPS


Maybe later:

* Test what happens if parent dies before child on Windows and on POSIX. Do we need to configure that?

* A maybe-owning type to pass the executable path./

* Polling.

  Windows vs POSIX api is different: POSIX checks which pipes are ready, while Windows IO Completion Ports try to read/write asynchronously and report when done.

  Reproc is weird: they use sockets instead of pipes on Windows, to replicate posix-style poll.

  Reproc also creates an extra socket per process so that it can track when it exits by polling on that socket. We should be able to do this with a pipe./

* Escaping strings:

  * To debug print POSIX command lines in a way runnable from shell.

  * To print names of env variables in error messages on encoding failures.
