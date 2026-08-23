#pragma once

#include <cassert>
#include <map>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32

#else
#  include <cstring> // For `std::strerror()`.
#  include <spawn.h>
#  include <sys/types.h> // For `pid_t`.
#  include <sys/wait.h> // For `waitpid()`.
#  include <unistd.h>
#  if __APPLE__
#    include <crt_externs.h> // For `_NSGetEnviron()`.
#  endif
#endif

// Changes here relative to SDL:
// * Fix bug: https://github.com/libsdl-org/SDL/issues/16188
// * Use return values instead of `errno` in a few places. But it seems in glibc those functions do set errno, even though it's not documented in the manual, so I'm not sure this ever matters.
// * Don't bother with android-specific code to obtain extra env variables from the application manifest, whatever that is.

namespace em::Proc
{
    #ifndef _WIN32
    namespace detail
    {
        #ifndef __APPLE__
        extern "C" char **environ;
        #endif

        [[nodiscard]] inline const char *const *GetEnviron()
        {
            #ifdef __APPLE__
            // Use `*_NSGetEnviron()` instead of `environ`.
            // `environ` does kinda work on Macs, but `man environ` on Macs says that it doesn't work in shared libraries and in bundles (whatever those are), but this function works.
            return *_NSGetEnviron();
            #else
            return environ;
            #endif
        }
    }
    #endif

    // A list of environment variables.
    using EnvMap = std::map<std::string, std::string, std::less<>>;


    // Take a snapshot of the current environment variables.
    // It might be a good idea to only do this once and save the result somewhere.
    [[nodiscard]] static EnvMap Current()
    {
        EnvMap ret;

        #ifdef _WIN32
        TODO
        #else
        const char *const *e = detail::GetEnviron();
        // SDL silently ignores `e` being null, so we do too.
        if (e)
        {
            while (*e)
            {
                std::string_view view = *e++;
                // SDL silently ignores the missing `=`, so we do too. It shouldn't be normally possible.
                // Even `putenv` is said to have a special case for the missing `=` on glibc, which causes it to unsert that variable: https://linux.die.net/man/3/putenv
                if (auto pos = view.find('='); pos != std::string_view::npos)
                {
                    auto [iter, is_new] = ret.try_emplace(std::string(view.substr(0, pos)));
                    if (is_new)
                        iter->second = view.substr(pos + 1);
                }
            }
        }
        #endif

        return ret;
    }

    class Params
    {
      public:
        // Adds a command line argument. The first of those is the process to run.
        // This is mutually exclusive with `RawCmdline()`, mixing them will erase the previous command line.
        Params &Arg(std::string arg)
        {
            argv_raw = nullptr;
            argv_vec.push_back(std::move(arg));
            return *this;
        }

        // A faster alternative to `Arg()`.
        // Sets the entire cmdline as `argv`. There must be a null pointer at the end of this array. `argc` is determined by counting non-null pointers.
        // This doesn't copy the array! You must ensure it outlives this `Params` and `BakedParams`.
        // This is mutually exclusive with `Arg()`, mixing them will erase the previous command line.
        Params &RawCmdline(const char *const *argv)
        {
            argv_raw = argv;
            argv_vec = {};
            return *this;
        }


        // If specified, this acts as the executable path, replacing that specified by the first `Arg()` or by `RawCmdline()`.
        // But the first `Arg()` or the first element of `RawCmdline()` is still passed as `argv[0]` to the new process. (TODO WINDOWS - still supported there?)
        Params &ExePath(std::string path)
        {
            exe_path_raw = nullptr;
            exe_path_str = std::move(path);
            return *this;
        }
        // This version doesn't copy the string! You must ensure it outlives this `Params` and `BakedParams`.
        Params &ExePathRaw(const char *path)
        {
            exe_path_raw = path;
            exe_path_str = {};
            return *this;
        }


        // Sets all environment variables for the new process. Anything not listed here will be propagated.
        // The default behavior is to copy the existing variables right when starting the process (not even when baking the parameters).
        Params &Env(EnvMap env)
        {
            use_raw_env = false;
            env_raw = nullptr;
            env_map = std::move(env);
            return *this;
        }
        // This version doesn't copy the array! You must ensure it outlives this `Params` and `BakedParams`.
        // This is mutually exclusive with `Env()`, mixing them will erase the previous environment.
        // Passing null to this restores the default behavior of obtaining the environment when starting the process.
        Params &EnvRaw(const char *const *env)
        {
            use_raw_env = true;
            env_raw = env;
            env_map = {};
            return *this;
        }


        // Only has effect on POSIX, not on Windows.
        // Detaches the new process from this one, among other things ensuring that the current terminal doesn't get attached to the new process if this one dies before it.
        //
        // This is also called "double forking" of "daemonizing" the new process, see this for more details: https://stackoverflow.com/q/881388/2752075
        // In turn you can no longer get the process exit code.
        // You can still wait for its completion since we know its PID, but enabling this theoretically makes it not reliable anymore, since something could've reused it, I think?
        Params &Background(bool enable = true)
        {
            #ifdef _WIN32
            (void)enable;
            #else
            background = enable;
            #endif
            return *this;
        }

      private:
        // If argv is specified using `SetRawCmdline()`, this is it.
        const char *const *argv_raw = nullptr;
        // If argv is specified using `AddArg()`, this is it.
        std::vector<std::string> argv_vec;

        const char *exe_path_raw = nullptr;
        std::string exe_path_str;

        bool use_raw_env = true; // We need a flag because `env_raw == nullptr` is valid too (and means obtaining the env variables right when starting the process).
        const char *const *env_raw = nullptr;
        EnvMap env_map;

        #ifndef _WIN32
        bool background = false;
        #endif

        friend class BakedParams;
    };

    class BakedParams
    {
      public:
        // Creates a null instance.
        BakedParams() {}
        BakedParams(Params &&params)
            : BakedParams() // Run the destructor on throw.
        {
            // Check that some form of command line is provided.
            if (!params.argv_raw && params.argv_vec.empty())
            {
                state.error = "No command line specified.";
                return;
            }
            // Bake `argv`.
            if (params.argv_raw)
            {
                state.argv = params.argv_raw;
            }
            else
            {
                // If the command line is not specified as a raw `argv`, assemble `argv` ourselves.
                state.argv_storage = std::move(params.argv_vec);
                std::size_t argc = state.argv_storage.size();
                // A direct resize should be faster than reserve?
                state.argv_ptrs_storage.resize(argc + 1); // +1 for the terminating null pointer.
                for (std::size_t i = 0; i < argc; i++)
                    state.argv_ptrs_storage[i] = state.argv_storage[i].c_str();
                state.argv = state.argv_ptrs_storage.data();
            }

            // Bake `exe_path`.
            if (params.exe_path_raw)
            {
                state.exe_path = params.exe_path_raw;
            }
            else if (!params.exe_path_str.empty())
            {
                state.exe_path_storage = std::move(params.exe_path_str);
                state.exe_path = state.exe_path_storage.c_str();
            }
            else
            {
                state.exe_path = state.argv[0];
            }

            // Bake environment variables.
            if (params.use_raw_env)
            {
                state.env = params.env_raw;
            }
            else
            {
                std::size_t count = params.env_map.size();

                state.env_storage.reserve(count);
                for (const auto &elem : params.env_map)
                    state.env_storage.push_back(elem.first + '=' + elem.second);

                // A direct resize should be faster than reserve?
                state.env_ptrs_storage.resize(count + 1); // +1 for the terminating null pointer.
                for (std::size_t i = 0; i < count; i++)
                    state.env_ptrs_storage[i] = state.env_storage[i].c_str();

                state.env = state.env_ptrs_storage.data();
            }

            #ifdef _WIN32

            #else

            if (int spawn_res = posix_spawnattr_init(&state.spawn_attr))
            {
                // No useful messages for us to emit here, so just write the number.
                // The manual doesn't mention this setting `errno`, so we use the return value instead. At least for `posix_spawn`, glibc sets the errno anyway, even though the manual doesn't say so, but for this function I can't check, because it never fails in glibc.
                state.error = "`posix_spawnattr_init` failed: " + std::to_string(spawn_res);
                return;
            }
            state.spawn_attr_alive = true;

            if (int spawn_res = posix_spawn_file_actions_init(&state.spawn_fa))
            {
                // No useful messages for us to emit here, so just write the number.
                // The manual doesn't mention this setting `errno`, so we use the return value instead. At least for `posix_spawn`, glibc sets the errno anyway, even though the manual doesn't say so, but for this function I can't check, because it never fails in glibc.
                state.error = "`posix_spawn_file_actions_init` failed: " + std::to_string(spawn_res);
                return;
            }
            state.spawn_fa_alive = true;
            #endif
        }

        // Non-movable for now, this is simpler to implement.
        BakedParams(const BakedParams &) = delete;
        BakedParams &operator=(const BakedParams &) = delete;

        ~BakedParams()
        {
            #ifdef _WIN32

            #else
            if (state.spawn_attr_alive)
                posix_spawnattr_destroy(&state.spawn_attr);
            if (state.spawn_fa_alive)
                posix_spawn_file_actions_destroy(&state.spawn_fa);
            #endif
        }

        // Returns true on a non-null instance.
        [[nodiscard]] explicit operator bool() const
        {
            #ifdef _WIN32

            #else
            return state.spawn_attr_alive && state.spawn_fa_alive;
            #endif
        }

      private:
        struct State
        {
            // This struct isn't strictly necessary anymore. Leaving it in case we decide to make this movable later.

            // If this is non-empty, the object is in an error state.
            // When we assign to this, we don't immediately destroy the resources we already created.
            // This is easier to implement, and also lets the user check for errors faster.
            std::string error;


            const char *const *argv = nullptr; // Non-owning.
            std::vector<std::string> argv_storage;
            std::vector<const char *> argv_ptrs_storage;

            const char *exe_path = nullptr; // Non-owning.
            std::string exe_path_storage; // This right there is why `BakedParams` is not movable.

            const char *const *env = nullptr; // Non-owning.
            std::vector<std::string> env_storage;
            std::vector<const char *> env_ptrs_storage;

            #ifdef _WIN32

            #else
            bool background = false;

            bool spawn_attr_alive = false;
            posix_spawnattr_t spawn_attr{};
            bool spawn_fa_alive = false;
            posix_spawn_file_actions_t spawn_fa{};
            #endif
        };
        State state;

        friend class Process;
    };

    class Process
    {
      public:
        Process() {}

        Process(const BakedParams &params)
            : Process() // Run the destructor on throw.
        {
            #ifdef _WIN32

            #else
            if (!params.state.error.empty())
            {
                state.error = params.state.error;
                return;
            }

            if (!params)
            {
                state.error = "Trying to create a process from a null params struct.";
                return;
            }

            // This is almost directly copied from SDL:

            // Note the `const_cast` here and on `argv` below. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
            //   and they figured it would be more convenient. They don't actually modify those strings.
            char *const *env_ptr = const_cast<char *const *>(params.state.env ? params.state.env : detail::GetEnviron());

            if (params.state.background)
            {
                #ifdef __APPLE__ // SDL says:  Apple has vfork marked as deprecated and (as of macOS 10.12) is almost identical to calling fork() anyhow.
                const pid_t pid = fork();
                const char *forkname = "fork";
                #else
                // `vfork` makes us share memory with the parent (unless implemented as `fork`), which means we must be extra careful to not touch anything. See manual: https://linux.die.net/man/3/vfork
                const pid_t pid = vfork();
                const char *forkname = "vfork";
                #endif
                switch (pid)
                {
                  case -1:
                    // Forking failed.
                    state.error = std::string("`") + forkname + "` failed: " + std::strerror(errno);
                    return;

                  case 0:
                    // Forking successful, we're in the new process.

                    // SDL says this detaches us from the original terminal. This creates a new "session" and makes this process its owner: https://linux.die.net/man/2/setsid
                    setsid();

                    // If we `vfork()`ed, it's theoretically unsafe to touch any memory of the parent process, and we touch `state.pid` here.
                    // But the worst that can happen (if `vfork` is implemented as `fork`) is that `state.pid` doesn't propagate to the parent and remains zero there.

                    // Note the use of `_exit()` as opposed to `std::exit()`.
                    // `vfork` manual says we must use this specific function.

                    // The manual doesn't mention `posix_spawnp` setting `errno`. It still does at least in glibc, but it's more correct to use the return value.

                    // Note the `const_cast` here and on `env_ptr` above. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
                    //   and they figured it would be more convenient. They don't actually modify those strings.

                    if (int error = posix_spawnp(&state.pid, params.state.exe_path, &params.state.spawn_fa, &params.state.spawn_attr, const_cast<char **>(params.state.argv), env_ptr))
                        _exit(error);
                    else
                        _exit(0);

                  default:
                    // Check the exit code of the direct child process. Also must call it to clean it up, otherwise it remains as a zombie.
                    // Note `pid` here, not `state.pid`.
                    // `waitpid` can wait for some other things than process termination, but it seems all of that is opt-in via the flags parameter, and by default it only waits for termination.
                    int status = -1;
                    if (waitpid(pid, &status, 0) < 0)
                    {
                        state.error = std::string("`waitpid` failed: ") + std::strerror(errno);
                        return;
                    }
                    if (WIFEXITED(status)) // Did the process exit normally? Regardless of the exit code.
                    {
                        if (int exit_code = WEXITSTATUS(status))
                        {
                            // We use the exit code to propagate the error code from `posix_spawnp` above.
                            state.error = std::string("`posix_spawnp` failed: ") + std::strerror(exit_code);
                            return;
                        }
                    }
                    else
                    {
                        state.error = "Forked process exited abnormally. Status integer: " + std::to_string(status);
                    }
                    break;
                }
            }
            else
            {
                // Not a background process, just call `posix_spawnp()` normally.

                // The manual doesn't mention `posix_spawnp` setting `errno`. It still does at least in glibc, but it's more correct to use the return value.

                // Note the `const_cast` here and on `env_ptr` above. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
                //   and they figured it would be more convenient. They don't actually modify those strings.

                if (int error = posix_spawnp(&state.pid, params.state.exe_path, &params.state.spawn_fa, &params.state.spawn_attr, const_cast<char **>(params.state.argv), env_ptr))
                {
                    state.error = std::string("`posix_spawnp` failed: ") + std::strerror(error);
                    return;
                }
            }

            assert(state.pid);
            #endif
        }

        // This is move-only.
        Process(Process &&other) noexcept : state(std::move(other.state)) {other.state = {};}
        Process &operator=(Process other) noexcept {std::swap(state, other.state); return *this;}

        // The default behavior is to wait for the process (if `IsBackground() == false`).
        // We have to wait to clean up the process, otherwise it remains as a "zombie", because we never consumed its exit status.
        ~Process()
        {
            #error need to implement waiting for the process to finish, probably in a separate function
        }

        // Returns false if this is a null instance that never held a process.
        [[nodiscard]] explicit operator bool() const {return state.pid;}

        // This is zero for null processes.
        [[nodiscard]] pid_t Pid() const {return state.pid;}

        // Returns true if this is a background process. See `Params::Background()` for more details.
        [[nodiscard]] bool IsBackground() const
        {
            #ifdef _WIN32
            return false;
            #else
            return state.is_background;
            #endif
        }

      private:
        struct State
        {
            std::string error;

            pid_t pid = 0;
            #ifndef _WIN32
            bool is_background = false;
            #endif
        };
        State state;
    };
}
