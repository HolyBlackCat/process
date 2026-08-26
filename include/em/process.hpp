#pragma once

#include <cassert>
#include <map>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <variant>
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
// * Better error reporting from starting background processes. See: https://github.com/libsdl-org/SDL/issues/16188
// * Use return values instead of `errno` in a few places. But it seems in glibc those functions do set errno, even though it's not documented in the manual, so I'm not sure this ever matters.
// * Don't bother with android-specific code to obtain extra env variables from the application manifest, whatever that is.
// * Refuse to use `kill(pid, 0)` to wait for background processes. Since PIDs can be recycled, this seems unreliable.
// * Added "have core dump" check when a process stops due to a signal.

namespace em::Proc
{
    namespace detail
    {
        template <typename ...P>
        struct Overload : P... {using P::operator()...;};
        template <typename ...P>
        Overload(P...) -> Overload<P...>; // Keep this for older compilers, just in case.

        #ifndef _WIN32

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
        #endif
    }

    // A list of environment variables.
    using EnvMap = std::map<std::string, std::string, std::less<>>;


    // Take a snapshot of the current environment variables.
    // It might be a good idea to only do this once and save the result somewhere.
    [[nodiscard]] inline EnvMap CurrentEnv()
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

    struct ExitReason
    {
        [[nodiscard]] bool Success() const
        {
            return ExitedWithCode(0);
        }

        [[nodiscard]] bool ExitedWithCode(int code) const
        {
            auto value = std::get_if<Code>(&var);
            return value && value->code == code;
        }

        [[nodiscard]] std::string ToString() const
        {
            return std::visit(detail::Overload{
                [](const Background &) -> std::string {return "Detached background process.";},
                [](const Code &elem)   -> std::string {return "Exited with code " + std::to_string(elem.code) + ".";},
                [](const Signal &elem) -> std::string {std::string ret = "Exited due to signal " + std::to_string(elem.signal) + "."; if (elem.core_dumped) ret += " Core dumped."; return ret;},
                [](const Other &elem)  -> std::string {return "Exited for an unknown reason: " + std::to_string(elem.status) + ".";},
                [](const Error &)      -> std::string {return "Library error.";},
            }, var);
        }


        // Exited normally with code.
        struct Code
        {
            int code = 0;
            friend auto operator<=>(Code, Code) = default; // Don't strictly need those operators on the variant members, but just in case.
        };

        // Terminated by a signal.
        struct Signal
        {
            int signal = 0;
            bool core_dumped = false; // Do we have a core dump?
            friend auto operator<=>(Signal, Signal) = default;
        };

        // Exited for another reason.
        struct Other
        {
            #ifndef _WIN32
            // This value is straight from `waitpid()`.
            int status = 0;
            #endif
            friend auto operator<=>(Other, Other) = default;
        };

        // Don't know if exited or not, this is a background process.
        struct Background
        {
            friend auto operator<=>(Background, Background) = default;
        };

        // Something is wrong with our library. No error message here, check `Process::ErrorMessage()` for more details.
        struct Error
        {
            friend auto operator<=>(Error, Error) = default;
        };

        using Var = std::variant<Background, Code, Signal, Other, Error>; // `Background` is listed first because of the dumb default constructibility checks failing for nested classes with member initializers.
        Var var;

        ExitReason() : var(Code{0}) {} // I guess this is a good default value?
        ExitReason(Var var) : var(std::move(var)) {}
    };

    class Params
    {
      public:
        Params() noexcept
        {
            // Those are dirt cheap to initialize, so no separate constructor.

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
        }

        // Non-movable for now, this is simpler to implement.
        Params(const Params &) = delete;
        Params &operator=(const Params &) = delete;

        ~Params()
        {
            #ifdef _WIN32

            #else
            if (state.spawn_attr_alive)
                posix_spawnattr_destroy(&state.spawn_attr);
            if (state.spawn_fa_alive)
                posix_spawn_file_actions_destroy(&state.spawn_fa);
            #endif
        }

        // Set the command to execute, and its arguments.
        // If `custom_argv0` is specified, it replaces `argv[0]` as the program to execute. The original `argv[0]` is then only passed to the program's `main`.
        Params &Command(std::vector<std::string> argv, std::string custom_argv0 = "")
        {
            bool have_custom_argv0 = !custom_argv0.empty() && !argv.empty(); // Empty argv is invalid anyway, but check for it, because we're going to swap with it.
            if (have_custom_argv0)
                std::swap(argv[0], custom_argv0);

            command.argv_storage = std::move(argv);
            std::size_t argc = command.argv_storage.size();
            // A direct resize should be faster than reserve?
            command.argv_ptrs_storage.resize(argc + 1); // +1 for the terminating null pointer.
            for (std::size_t i = 0; i < argc; i++)
                command.argv_ptrs_storage[i] = command.argv_storage[i].c_str();
            command.argv_ptrs_storage.back() = nullptr; // Zero explicitly in case the vector wasn't empty before.
            command.argv = command.argv_ptrs_storage.data();

            if (have_custom_argv0)
            {
                command.exe_path_storage = std::move(custom_argv0);
                command.exe_path = command.exe_path_storage.c_str();
            }
            else
            {
                command.exe_path_storage = {};
                command.exe_path = nullptr;
            }
            return *this;
        }
        // This version doesn't copy the strings, so make sure they don't dangle.
        // Note: For this version, the meaning of `argv[0]` and the second parameter is inverted relative to the overload above.
        Params &CommandArgv(const char *const *argv, const char *executable = nullptr)
        {
            command = {};
            command.argv = argv;
            command.exe_path = executable;
            return *this;
        }


        // Set the environment variables. This overrides all variables. Use `CurrentEnv()` to get the variables of the current process, if you only want to modify some.
        // If this is not called, the default behavior is to use the variables of the current process, reading them right when starting the new process (not when constructing `Params`).
        Params &Env(EnvMap env_vars)
        {
            std::size_t count = env_vars.size();

            env.storage.reserve(count);
            for (const auto &elem : env_vars)
                env.storage.push_back(elem.first + '=' + elem.second);

            // A direct resize should be faster than reserve?
            env.ptrs_storage.resize(count + 1); // +1 for the terminating null pointer.
            for (std::size_t i = 0; i < count; i++)
                env.ptrs_storage[i] = env.storage[i].c_str();
            env.ptrs_storage.back() = nullptr; // Zero explicitly in case the vector wasn't empty before.

            env.ptr = env.ptrs_storage.data();

            return *this;
        }
        // This version doesn't copy the strings, so make sure they don't dangle.
        // This can't be named `Env()` because then `Env({})` would call this overload.
        Params &EnvPtr(const char *const *env_vars)
        {
            env = {};
            env.ptr = env_vars;
            return *this;
        }


        // Returns true on a non-null instance.
        [[nodiscard]] explicit operator bool() const
        {
            #ifdef _WIN32

            #else
            return state.spawn_attr_alive && state.spawn_fa_alive;
            #endif
        }


        // Some fields are left public just in case, but you don't need to touch them if you assign to them using the setters above.

        struct CommandState
        {
            const char *const *argv = nullptr; // Non-owning.
            std::vector<std::string> argv_storage;
            std::vector<const char *> argv_ptrs_storage;

            const char *exe_path = nullptr; // Non-owning.
            std::string exe_path_storage; // This right there is why `Params` is not movable.
        };
        CommandState command;

        struct EnvState
        {
            const char *const *ptr = nullptr; // Non-owning.
            std::vector<std::string> storage;
            std::vector<const char *> ptrs_storage;
        };
        EnvState env;

      private:
        struct State
        {
            // Having those in a struct isn't strictly necessary anymore. Leaving it in case we decide to make this movable later.

            // If this is non-empty, the object is in an error state.
            // When we assign to this, we don't immediately destroy the resources we already created.
            // This is easier to implement, and also lets the user check for errors faster.
            std::string error;


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

        Process(const Params &params)
            : Process() // Run the destructor on throw.
        {
            // Mark as background process before doing anything else, so that this information is not lost on error.
            if (params.state.background)
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Background{});

            #ifdef _WIN32

            #else
            if (!params.state.error.empty())
            {
                state.error = "Error in parameters: " + params.state.error;
                return;
            }

            if (!params.command.argv)
            {
                state.error = "Null `argv` specified for process.";
                return;
            }

            if (!params)
            {
                state.error = "Trying to create a process from a null params struct. Was it moved from?";
                return;
            }

            // This is almost directly copied from SDL:

            // Note the `const_cast` here and on `argv` below. It's needed because the POSIX API takes `char *const *` instead of `const char *const *`, because the C pointer conversion rules are more strict than the C++ ones,
            //   and they figured it would be more convenient. They don't actually modify those strings.
            char *const *env_ptr = const_cast<char *const *>(params.env.ptr ? params.env.ptr : detail::GetEnviron());
            char *exe_path = const_cast<char *>(params.command.exe_path ? params.command.exe_path : params.command.argv[0]);

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

                    if (int error = posix_spawnp(&state.pid, exe_path, &params.state.spawn_fa, &params.state.spawn_attr, const_cast<char **>(params.command.argv), env_ptr))
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

                if (int error = posix_spawnp(&state.pid, exe_path, &params.state.spawn_fa, &params.state.spawn_attr, const_cast<char **>(params.command.argv), env_ptr))
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
            // Not checking `HasError()`, it shouldn't stop us from calling `waitpid()` to clean up the process.
            // Checking `operator bool` though, since `CheckOrWait()` asserts on that.
            if (*this)
                CheckOrWait(true);
        }

        // Returns false if this is a null instance that never held a process.
        [[nodiscard]] explicit operator bool() const {return state.pid;}

        // Returns true if this instance is an error state, due to the underlying API failing.
        [[nodiscard]] bool HasError() const {return !state.error.empty();}
        // Returns the error message. If `HasError() == false`, then always returns an empty string.
        [[nodiscard]] const std::string ErrorMessage() const {return state.error;}

        // This is zero for null processes.
        [[nodiscard]] pid_t Pid() const {return state.pid;}

        // Returns true if this is a background process. See `Params::Background()` for more details.
        [[nodiscard]] bool IsBackground() const
        {
            #ifdef _WIN32
            return false;
            #error do we need the special case?
            #else
            return state.exit_reason && std::holds_alternative<Proc::ExitReason::Background>(state.exit_reason->var);
            #endif
        }

        // Update the process state. Check `ExitReason()` and `HasError()` after this.
        void UpdateState()
        {
            CheckOrWait(false);
        }

        // Wait until the process exits.
        // Note! This can deadlock if you have pipes open to this process, because you need to be manually poking those pipes.
        void BlockUntilExit()
        {
            CheckOrWait(true);
        }

        // Returns the exit reason of the process, or false if it's not known to be exited.
        [[nodiscard]] const std::optional<Proc::ExitReason> &ExitReason() const
        {
            return state.exit_reason;
        }

      private:
        void CheckOrWait(bool wait)
        {
            // Do nothing when already exited. This rejects background processes too.
            if (state.exit_reason)
                return;

            // Intentionally don't check `HasError()`. If something random has failed, we should still be able to `waitpid()` the process to clean it up.

            // Complain about null instances.
            if (state.pid == 0)
            {
                // Assert instead of writing to `state.error`, this makes more sense to me.
                assert(false && "Attempt to wait for a null instance.");
                return;
            }

            int status = 0;
            int wait_result = waitpid(state.pid, &status, wait ? 0 : WNOHANG);
            // If wait errored...
            // It returns `-1` on error.
            if (wait_result < 0)
            {
                state.error = std::string("`waitpid` failed: ") + std::strerror(errno);
                // Mark the process as exited, I guess.
                // So that nothing gets blocked on the user side, waiting for it to exit.
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Error{});
            }
            // If wait says the process is still running....
            if (wait_result == 0)
            {
                assert(wait); // Should only be possible if `wait == true`.
                return;
            }

            // At this point we know the process has exited, but why?
            if (WIFEXITED(status))
            {
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Code{WEXITSTATUS(status)});
                return;
            }
            if (WIFSIGNALED(status))
            {
                state.exit_reason = Proc::ExitReason(Proc::ExitReason::Signal{
                    WTERMSIG(status),
                    #ifdef WCOREDUMP // Manual says to ifdef this: https://linux.die.net/man/2/waitpid
                    bool(WCOREDUMP(status))
                    #endif
                });
                return;
            }

            // Some unknown reason.
            state.exit_reason = Proc::ExitReason(Proc::ExitReason::Other{status});
        }

        struct State
        {
            // I considered merging this into `ExitReason`, but since I also merged `Background` into that, I'm worried that we'd lose information if the error message replaced that.
            std::string error;

            pid_t pid = 0;

            std::optional<Proc::ExitReason> exit_reason;
        };
        State state;
    };
}
