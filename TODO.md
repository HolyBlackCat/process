* POSIX: Temporarily disable signals when vforking
* POSIX: use `posix_spawnattr_setsigdefault` and `posix_spawnattr_setsigmask` to reset signal handling in child to the default. SDL doesn't do that, but reproc does something similar (but without spawn). I guess we should do it.
* POSIX: close all FDs in the child, like SDL does. Reproc does this too.

* Document that on Windows, specifying a custom executable path disables PATH and PATHEXT search
* Document the batch escaping safety issues on windows.


Maybe later:

* Test what happens if parent dies before child on Windows and on POSIX. Do we need to configure that?

* Polling.

  Windows vs POSIX api is different: POSIX checks which pipes are ready, while Windows IO Completion Ports try to read/write asynchronously and report when done.

  Reproc is weird: they use sockets instead of pipes on Windows, to replicate posix-style poll.

  Reproc also creates an extra socket per process so that it can track when it exits by polling on that socket. We should be able to do this with a pipe./

* Escaping strings:

  * To debug print POSIX command lines in a way runnable from shell.

  * To print names of env variables in error messages on encoding failures.
