#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace files::internal {

struct CommandResult {
    int exitCode = -1;
    std::string output;
    bool outputTruncated = false;
    bool timedOut = false;

    explicit operator bool() const { return exitCode == 0; }
};

using CommandOutput = std::function<void(const std::string&)>;
CommandResult runCommand(const std::vector<std::string>& arguments, const CommandOutput& onOutput = {},
                         const std::string& input = {}, bool captureStdout = true, int stdinFd = -1);
CommandResult runTimedCommand(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout,
                              const CommandOutput& onOutput = {});
void clearSecret(std::string& value);

}  // namespace files::internal
