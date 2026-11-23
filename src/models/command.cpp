#include "models/command.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace files::internal {

void clearSecret(std::string& value)
{
    volatile char* bytes = value.empty() ? nullptr : &value[0];
    for (size_t i = 0; i < value.size(); ++i) {
        bytes[i] = 0;
    }
    value.clear();
}

namespace {

struct CommandFds {
    int output[2] = {-1, -1};
    int input[2] = {-1, -1};
    ~CommandFds()
    {
        for (int fd : {output[0], output[1], input[0], input[1]}) {
            if (fd >= 0) {
                close(fd);
            }
        }
    }
};

CommandResult runCommandImpl(const std::vector<std::string>& arguments, const CommandOutput& onOutput,
                             const std::string& input, bool captureStdout, int stdinFd,
                             std::chrono::milliseconds timeout)
{
    if (arguments.empty() || (!input.empty() && stdinFd >= 0)) {
        return {-1, "Invalid command arguments"};
    }
    CommandFds fds;
#if defined(__linux__)
    const int pipeResult = pipe2(fds.output, O_CLOEXEC);
    constexpr int socketType = SOCK_STREAM | SOCK_CLOEXEC;
#else
    const int pipeResult = pipe(fds.output);
    constexpr int socketType = SOCK_STREAM;
#endif
    if (pipeResult < 0 || (!input.empty() && socketpair(AF_UNIX, socketType, 0, fds.input) < 0) ||
        fcntl(fds.output[0], F_SETFL, O_NONBLOCK) < 0) {
        return {-1, std::strerror(errno)};
    }
#if defined(__APPLE__)
    if (fds.input[0] >= 0) {
        const int enabled = 1;
        if (setsockopt(fds.input[0], SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
            return {-1, std::strerror(errno)};
        }
    }
#endif

    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) {
        return {-1, std::strerror(error)};
    }
    if (stdinFd >= 0) {
        error = posix_spawn_file_actions_adddup2(&actions, stdinFd, STDIN_FILENO);
    } else if (input.empty()) {
        error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    } else {
        error = posix_spawn_file_actions_adddup2(&actions, fds.input[1], STDIN_FILENO);
    }
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, fds.output[1], STDERR_FILENO);
    }
    if (!error) {
        error = captureStdout ? posix_spawn_file_actions_adddup2(&actions, fds.output[1], STDOUT_FILENO)
                              : posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    for (int fd : {fds.output[0], fds.output[1], fds.input[0], fds.input[1]}) {
        if (fd >= 0 && !error) {
            error = posix_spawn_file_actions_addclose(&actions, fd);
        }
    }

    std::vector<char*> argv;
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    char locale[] = "LC_ALL=C";
    std::vector<char*> env{locale};
    for (char** entry = environ; *entry; ++entry) {
        if (std::strncmp(*entry, "LC_ALL=", 7) != 0) {
            env.push_back(*entry);
        }
    }
    env.push_back(nullptr);

    using Clock = std::chrono::steady_clock;
    const bool timed = timeout.count() > 0;
    const auto deadline = Clock::now() + timeout;
    posix_spawnattr_t attributes;
    bool attributesInitialized = false;
    if (!error && timed) {
        error = posix_spawnattr_init(&attributes);
        attributesInitialized = error == 0;
        if (!error) {
            error = posix_spawnattr_setpgroup(&attributes, 0);
        }
        if (!error) {
            error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        }
    }
    pid_t child = -1;
    if (!error) {
        error = posix_spawn(&child, argv.front(), &actions, timed ? &attributes : nullptr, argv.data(), env.data());
    }
    if (attributesInitialized) {
        posix_spawnattr_destroy(&attributes);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        return {-1, std::strerror(error)};
    }

    close(fds.output[1]);
    fds.output[1] = -1;
    if (fds.input[0] >= 0) {
        close(fds.input[1]);
        fds.input[1] = -1;
        size_t sent = 0;
        while (sent < input.size()) {
#if defined(__APPLE__)
            constexpr int flags = 0;
#else
            constexpr int flags = MSG_NOSIGNAL;
#endif
            const ssize_t count = send(fds.input[0], input.data() + sent, input.size() - sent, flags);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
            sent += static_cast<size_t>(count);
        }
        close(fds.input[0]);
        fds.input[0] = -1;
    }

    constexpr size_t outputLimit = 128 * 1024;
    constexpr auto drainLimit = std::chrono::milliseconds(150);
    constexpr auto termGrace = std::chrono::seconds(2);
    CommandResult result;
    char buffer[4096];
    bool eof = false;
    int readError = 0;
    int status = 0;
    pid_t waited = 0;
    Clock::time_point drainUntil;
    Clock::time_point killAt;
    while (!eof || waited == 0) {
        const auto now = Clock::now();
        if (waited == 0 && (!result.timedOut || now >= killAt)) {
            const bool forceExit = result.timedOut;
            if (forceExit) {
                // Reap the leader only after signalling the group, even if it exited on SIGTERM.
                kill(-child, SIGKILL);
            }
            do {
                waited = waitpid(child, &status, forceExit || (eof && !timed) ? 0 : WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited < 0) {
                result.output += "\n" + std::string(std::strerror(errno));
                return result;
            }
            if (waited == child) {
                drainUntil = Clock::now() + drainLimit;
            } else if (timed && now >= deadline) {
                kill(-child, SIGTERM);
                result.timedOut = true;
                killAt = now + termGrace;
            }
        }
        if (waited == child && (eof || Clock::now() >= drainUntil)) {
            break;
        }
        pollfd output{fds.output[0], POLLIN, 0};
        const int ready = poll(&output, eof ? 0 : 1, 25);
        if (ready < 0 && errno != EINTR) {
            readError = errno;
            eof = true;
        }
        if (ready <= 0) {
            continue;
        }
        const ssize_t count = read(fds.output[0], buffer, sizeof(buffer));
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (count <= 0) {
            readError = count < 0 ? errno : 0;
            eof = true;
            continue;
        }
        std::string chunk(buffer, static_cast<size_t>(count));
        if (onOutput) {
            onOutput(chunk);
        }
        result.output += chunk;
        if (result.output.size() > outputLimit) {
            result.output.erase(0, result.output.size() - outputLimit);
            result.outputTruncated = true;
        }
    }
    if (readError) {
        result.output += "\n" + std::string(std::strerror(readError));
    }
    if (result.timedOut) {
        result.exitCode = 124;
    } else if (WIFEXITED(status) && !readError) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    }
    return result;
}

}  // namespace

CommandResult runCommand(const std::vector<std::string>& arguments, const CommandOutput& onOutput,
                         const std::string& input, bool captureStdout, int stdinFd)
{
    return runCommandImpl(arguments, onOutput, input, captureStdout, stdinFd, {});
}

CommandResult runTimedCommand(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout,
                              const CommandOutput& onOutput)
{
    if (timeout.count() <= 0) {
        return {-1, "Command timeout must be positive"};
    }
    return runCommandImpl(arguments, onOutput, {}, true, -1, timeout);
}

}  // namespace files::internal
