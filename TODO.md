
* POSIX: close all FDs in the child, like SDL does. Reproc does this too. Behind a knob.


Maybe later:

* Test what happens if parent dies before child on Windows and on POSIX. Do we need to configure that?

* A knob to reset signal handlers in the child?

  It's not a problem, since the custom handlers are reset to default on spawn, but how each signal is handled (ignored or not, etc?) is not reset.

  It seems we should use `posix_spawnattr_setsigdefault` for this.

* Polling.

  Windows vs POSIX api is different: POSIX checks which pipes are ready, while Windows IO Completion Ports try to read/write asynchronously and report when done.

  Reproc is weird: they use sockets instead of pipes on Windows, to replicate posix-style poll.

  Reproc also creates an extra socket per process so that it can track when it exits by polling on that socket. We should be able to do this with a pipe./

* Escaping strings:

  * To debug print POSIX command lines in a way runnable from shell.

  * To print names of env variables in error messages on encoding failures.
