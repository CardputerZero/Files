#pragma once

#include "models/command.hpp"
#include <filesystem>
#include <memory>

namespace files::internal {

struct DebMetadata {
    std::string name;
    std::string version;
    std::string architecture;
    std::string description;
    std::string preDepends;
    std::string depends;
    bool hasInstallScripts = false;
};

struct PreparedDeb {
    explicit PreparedDeb(std::filesystem::path directory);
    ~PreparedDeb();
    PreparedDeb(const PreparedDeb&) = delete;
    PreparedDeb& operator=(const PreparedDeb&) = delete;
    const std::filesystem::path path;
    DebMetadata metadata;
    std::string sha256;
    std::string aptPlanSha256;
    std::string planError;
};

struct DebInspection {
    std::unique_ptr<PreparedDeb> package;
    std::string text;
    bool installable = false;
    std::string plan;
    std::string problem;
    bool storeHelperPresent = false;
    std::string nativeArchitecture;
};

enum class DebInstallPhase { Preparing, Authorizing, Verifying, Downloading, Installing, Unpacking, Configuring, Finishing };
using DebInstallProgress = std::function<void(DebInstallPhase)>;

enum class DebInstallState { Installed, PasswordRequired, AuthenticationFailed, Failed };

struct DebInstallResult {
    DebInstallState state = DebInstallState::Failed;
    std::string message;
};

DebMetadata parseDebInfo(const std::string& output);
DebInspection inspectDeb(const std::filesystem::path& source, uint64_t mountId = 0,
                         const CommandOutput& metadataReady = {});
DebInstallResult installDeb(const PreparedDeb& package, std::string& password, const CommandOutput& log,
                            const DebInstallProgress& progress = {});
std::string cleanCommandText(std::string text);

}  // namespace files::internal
