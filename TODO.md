


Pipes:

* On POSIX:
  * Recommend ignoring `SIGPIPE`. (sigaction?)
  * Document not ignoring `SIGCHLD` because that would destroy every child handle before we can `waitpid` it. A custom handler is in theory possible, if you make sure to not destroy the handle.

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


* Windows: a flag to hide the console


* Async/polling:

    MakePipe needs async variants

    OpenFile needs async variants

Compilation tests:
    with and without EM_PROC_HAVE_PIPE2
    with and without EM_PROC_CAN_DETECT_CORE_DUMPS

Tests:
    environment
    chdir
    no_append vs default behavior on different systems
    merging output streams: check both stdout and stderr

* Test builds without exceptions.

* Async waiting for process termination: jobs (with IOCP) on Windows, fdpid_open on Linux if supported and enabled, dummy pipes on POSIX otherwise


Maybe later:

* Test what happens if parent dies before child on Windows and on POSIX. Do we need to configure that?

* Polling.

  Windows vs POSIX api is different: POSIX checks which pipes are ready, while Windows IO Completion Ports try to read/write asynchronously and report when done.

  Reproc is weird: they use sockets instead of pipes on Windows, to replicate posix-style poll.

  Reproc also creates an extra socket per process so that it can track when it exits by polling on that socket. We should be able to do this with a pipe./

* Escaping strings:

  * To debug print POSIX command lines in a way runnable from shell.

  * To print names of env variables in error messages on encoding failures.

* `posix_spawn_file_actions_addopen` instead of having the user open the files for redirects. This requires putting all file parameters in structs to store in the redirect `variant`s.
