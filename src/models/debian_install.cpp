#include "models/debian_install.hpp"
#include "models/debian_package.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

#if defined(__linux__)
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

namespace files::internal {
namespace fs = std::filesystem;
namespace {

constexpr char verifyingArchive[] = "Verifying the reviewed archive...";
constexpr char preparingTransaction[] = "Preparing the package transaction...";
constexpr char verifyingInstall[] = "Verifying the installed package...";

constexpr char storeHelperExecutable[] = "/usr/share/APPLaunch/bin/M5CardputerZero-AppStore";

bool digestValid(const std::string& digest, size_t size)
{
    return digest.size() == size && digest.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string trimWhitespace(const std::string& text)
{
    const auto first = text.find_first_not_of(" \r\n\t");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}

#if defined(__linux__)
struct ScopedFd {
    int fd = -1;
    ~ScopedFd() { if (fd >= 0) close(fd); }
};

struct RootStage {
    fs::path directory;
    bool preserveForRecovery = false;
    ~RootStage()
    {
        std::error_code error;
        if (!directory.empty() && !preserveForRecovery) {
            fs::remove_all(directory, error);
        }
    }
};

// The worker is a subreaper. Give adopted children time to finish after the
// manager returns, including on failure. A maintainer script may leave a
// running service, which must be free to outlive the transaction.
struct ReapInstallChildren {
    ~ReapInstallChildren()
    {
        using Clock = std::chrono::steady_clock;
        const auto deadline = Clock::now() + std::chrono::seconds(3);
        while (Clock::now() < deadline) {
            const auto child = waitpid(-1, nullptr, WNOHANG);
            if (child < 0 && errno != EINTR) {
                return;
            }
            if (child == 0) {
                std::this_thread::sleep_until(std::min(deadline, Clock::now() + std::chrono::milliseconds(25)));
            }
        }
    }
};

void syncRecoveryFd(int fd, const fs::path& path)
{
    while (fsync(fd) != 0) {
        if (errno != EINTR) {
            throw std::runtime_error("Cannot sync package recovery data at " + path.string() + ": " +
                                     std::strerror(errno));
        }
    }
}

void syncRecoveryDirectory(const fs::path& path)
{
    ScopedFd directory{open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (directory.fd < 0) {
        throw std::runtime_error("Cannot open the package recovery directory: " + path.string());
    }
    syncRecoveryFd(directory.fd, path);
}

void writeAll(int fd, const char* data, size_t size, const char* error)
{
    while (size > 0) {
        const auto count = write(fd, data, size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw std::runtime_error(error + std::string(": ") + std::strerror(count < 0 ? errno : EIO));
        }
        data += count;
        size -= static_cast<size_t>(count);
    }
}

bool storeHelperPresent()
{
    std::error_code error;
    const auto status = fs::symlink_status(storeHelperExecutable, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) {
        return false;
    }
    if (error || !trustedSystemExecutable(storeHelperExecutable)) {
        throw std::runtime_error("The Store helper path is not a trusted system executable.");
    }
    return true;
}

void lockTransaction(ScopedFd& lock)
{
    if (lock.fd < 0) {
        throw std::runtime_error("Cannot open the package transaction lock: " + std::string(std::strerror(errno)));
    }
    if (flock(lock.fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            throw std::runtime_error("Another Files or Store package transaction is running. Retry when it finishes.");
        }
        throw std::runtime_error("Cannot lock the package transaction: " + std::string(std::strerror(errno)));
    }
}

void openStoreLock(ScopedFd& lock, const std::string& configured)
{
    const char* sudoUid = std::getenv("SUDO_UID");
    if (!sudoUid || !*sudoUid || std::strspn(sudoUid, "0123456789") != std::strlen(sudoUid)) {
        throw std::runtime_error("Cannot determine the account used by Store.");
    }
    char* end = nullptr;
    errno = 0;
    const auto number = std::strtoul(sudoUid, &end, 10);
    const auto uid = static_cast<uid_t>(number);
    const auto* user = !errno && *end == '\0' && number == uid ? getpwuid(uid) : nullptr;
    if (!user || !user->pw_dir || !*user->pw_dir) {
        throw std::runtime_error("Cannot determine the Store state directory.");
    }
    const fs::path state = configured.empty() ? fs::path(user->pw_dir) / ".local/share/cardputerzero-appstore"
                                                : fs::path(configured);
    if (!state.is_absolute()) {
        throw std::runtime_error("The Store state directory must be an absolute path.");
    }

    // Opening caller-owned paths with the caller's privileges avoids opening a root-only file via a symlink.
    if (initgroups(user->pw_name, user->pw_gid) != 0 || setegid(user->pw_gid) != 0 || seteuid(uid) != 0) {
        throw std::runtime_error("Cannot open the Store lock as the Files account.");
    }
    std::error_code error;
    fs::create_directories(state, error);
    if (!error) {
        const auto path = state / "package-transaction.lock";
        lock.fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (lock.fd < 0 && errno == ENOENT) {
            lock.fd = open(path.c_str(), O_RDWR | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0644);
        }
    }
    const int openError = errno;
    if (seteuid(0) != 0 || setegid(0) != 0 || setgroups(0, nullptr) != 0) {
        _exit(1);
    }
    if (error) {
        throw std::runtime_error("Cannot access the Store state directory: " + error.message());
    }
    if (lock.fd < 0) {
        errno = openError;
    } else {
        struct stat info{};
        if (fstat(lock.fd, &info) != 0 || !S_ISREG(info.st_mode) || (info.st_uid != uid && info.st_uid != 0)) {
            throw std::runtime_error("The Store transaction lock is not a regular file owned by Store.");
        }
    }
    lockTransaction(lock);
    const auto pending = fs::symlink_status(state / "pending-package.json", error);
    if (error != std::errc::no_such_file_or_directory && (error || fs::exists(pending))) {
        throw std::runtime_error("Finish the pending Store package transaction first.");
    }
}

bool validField(const std::string& value, const char* allowed)
{
    return !value.empty() && value.find_first_not_of(allowed) == std::string::npos;
}

std::string packageField(const fs::path& path, const char* field)
{
    const auto result = runCommand({"/usr/bin/dpkg-deb", "-f", path.string(), field});
    return result && !result.outputTruncated ? trimWhitespace(result.output) : "";
}

fs::path prepareAptHook(const fs::path& path)
{
    const auto aptHook = path.parent_path() / "verify-apt-input";
    std::ofstream hook(aptHook);
    hook << R"SH(#!/bin/sh
set -eu
found=no
while IFS= read -r archive; do
    if [ "$archive" = "$FILES_REVIEWED_ARCHIVE" ]; then
        found=yes
    fi
done
if [ "$found" != yes ]; then
    echo 'APT selected a different archive for the reviewed package.' >&2
    exit 1
fi
)SH";
    hook.close();
    if (!hook || chmod(aptHook.c_str(), 0700) != 0) {
        throw std::runtime_error("Cannot prepare APT's reviewed archive check.");
    }

    if (setenv("FILES_REVIEWED_ARCHIVE", path.c_str(), 1) != 0) {
        throw std::runtime_error("Cannot set the reviewed archive path.");
    }
    return aptHook;
}

fs::path writePendingRecord(const fs::path& path, const std::string& name, const std::string& version,
                            const std::string& sha256)
{
    const auto md5 = runCommand({"/usr/bin/md5sum", "--", path.string()});
    const auto checksum = md5.output.substr(0, md5.output.find(' '));
    if (!md5 || !digestValid(checksum, 32)) {
        throw std::runtime_error("Cannot prepare Store's package verification record.");
    }
    const auto stage = path.parent_path();
    const auto pending = stage / "pending-package.json";
    const auto temporary = stage / "pending-package.json.tmp";
    ScopedFd record{open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
    if (record.fd < 0) {
        throw std::runtime_error("Cannot create the package recovery record.");
    }
    std::ostringstream json;
    json << "{\"transaction_id\":\"files-local\",\"action\":\"install\","
         << "\"app_id\":\"" << name << "\",\"package\":\"" << name << "\","
         << "\"expected_package_version\":\"" << version << "\",\"expected_md5\":\"" << checksum << "\","
         << "\"expected_sha256\":\"" << sha256 << "\",\"deb_path\":\"" << path.string() << "\"}\n";
    const auto text = json.str();
    writeAll(record.fd, text.data(), text.size(), "Cannot write the package recovery record");
    syncRecoveryFd(record.fd, temporary);
    const int fd = record.fd;
    record.fd = -1;
    if (close(fd) != 0) {
        throw std::runtime_error("Cannot finish writing the package recovery record.");
    }
    fs::rename(temporary, pending);
    syncRecoveryDirectory(stage);
    syncRecoveryDirectory(stage.parent_path());
    return pending;
}

std::string storeInstall(const fs::path& path, const fs::path& pending, bool apt, const fs::path& aptHook)
{
    const auto stage = path.parent_path();
    // Store uses PATH for its package tools. Keep its verification and transaction logic,
    // and apply the reviewed plan and Files' conffile policy to its package manager call.
    const auto tools = stage / "tools";
    fs::create_directory(tools);
    if (chmod(tools.c_str(), 0700) != 0) {
        throw std::runtime_error("Cannot protect the Store package command directory.");
    }
    const auto dpkg = tools / "dpkg";
    std::ofstream wrapper(dpkg);
    wrapper << R"SH(#!/bin/sh
set -eu
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
if [ "${0##*/}" = dpkg ]; then
    case "${1:-}" in
        --audit|--print-architecture|--print-foreign-architectures|--compare-versions)
            exec /usr/bin/dpkg "$@" ;;
    esac
fi
case "${0##*/}" in
    dpkg) [ "$#" -eq 2 ] && [ "$1" = --install ] && [ "$2" = "$FILES_REVIEWED_ARCHIVE" ] ;;
    apt-get) [ "${1:-}" = -y ] && [ "$#" -eq 5 ] &&
             [ "$4" = install ] && [ "$5" = "$FILES_REVIEWED_ARCHIVE" ] ;;
    *) exit 1 ;;
esac || { echo 'Unexpected Store package manager arguments.' >&2; exit 1; }
if [ "${0##*/}" = apt-get ] || [ "$FILES_REVIEWED_USE_APT" = yes ]; then
    exec /usr/bin/apt-get -y --reinstall --no-remove --no-install-recommends \
        -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold \
        -o APT::Status-Fd=1 -o "DPkg::Pre-Install-Pkgs::=$FILES_REVIEWED_APT_HOOK" \
        install "$FILES_REVIEWED_ARCHIVE"
fi
exec /usr/bin/dpkg --force-confdef --force-confold --install "$FILES_REVIEWED_ARCHIVE"
)SH";
    wrapper.close();
    if (!wrapper || chmod(dpkg.c_str(), 0700) != 0) {
        throw std::runtime_error("Cannot prepare Store's package manager command.");
    }
    fs::create_hard_link(dpkg, tools / "apt-get");

    // Store updates its own pending file. Keep the durable Files record intact for recovery.
    const auto storeState = stage / "store";
    if (mkdir(storeState.c_str(), 0700) != 0) {
        throw std::runtime_error("Cannot prepare the private Store transaction directory.");
    }
    const auto storePending = storeState / pending.filename();
    fs::copy_file(pending, storePending);
    if (setenv("M5APPSTORE_STATE_DIR", storeState.c_str(), 1) != 0 ||
        setenv("FILES_REVIEWED_USE_APT", apt ? "yes" : "no", 1) != 0 ||
        setenv("FILES_REVIEWED_APT_HOOK", aptHook.c_str(), 1) != 0 ||
        setenv("PATH", (tools.string() + ":/usr/sbin:/usr/bin:/sbin:/bin").c_str(), 1) != 0) {
        throw std::runtime_error("Cannot set Store's package transaction environment.");
    }
    const auto result = runCommand({storeHelperExecutable, "--package-helper", "install",
                                    "--package-value", path.string(), "--package-transaction", "files-local",
                                    "--package-pending-path", storePending.string()},
                                   [](const std::string& output) { std::cout << output << std::flush; });
    setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
    return result ? std::string{} : "Store's package helper failed (exit " + std::to_string(result.exitCode) + ").";
}

void copyReviewedArchive(const fs::path& path)
{
    struct stat input{};
    if (fstat(STDIN_FILENO, &input) != 0 || !S_ISREG(input.st_mode)) {
        throw std::runtime_error("The reviewed archive must be passed as a regular file.");
    }
    if (const auto error = debStagingError(path.parent_path(), input.st_size); !error.empty()) {
        throw std::runtime_error("Cannot stage the reviewed package: " + error);
    }
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        throw std::runtime_error("Cannot create the protected package copy.");
    }
    ScopedFd output{fd};
    char buffer[128 * 1024];
    off_t remaining = input.st_size;
    while (remaining > 0) {
        const auto count = read(STDIN_FILENO, buffer,
                                static_cast<size_t>(std::min<off_t>(remaining, sizeof(buffer))));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw std::runtime_error("The prepared package changed after review or could not be read.");
        }
        writeAll(fd, buffer, static_cast<size_t>(count), "Cannot write the protected package copy");
        remaining -= count;
    }
    syncRecoveryFd(fd, path);
    output.fd = -1;
    if (close(fd) != 0) {
        throw std::runtime_error("Cannot finish writing the protected package copy.");
    }
}

std::chrono::seconds packageTimeout()
{
    const char* value = std::getenv("M5APPSTORE_PACKAGE_TIMEOUT");
    if (value && *value) {
        char* end = nullptr;
        const auto seconds = std::strtol(value, &end, 10);
        if (*end == '\0' && seconds > 0) {
            return std::chrono::seconds(std::min(seconds, 7200L));
        }
    }
    return std::chrono::hours(1);
}

bool needsApt(const std::string& plan, const std::string& name)
{
    std::istringstream lines(plan);
    for (std::string operation, package; lines >> operation >> package;) {
        package.resize(std::min(package.size(), package.find(':')));
        if (package != name || (operation != "Inst" && operation != "Conf")) {
            return true;
        }
        lines.ignore(128 * 1024, '\n');
    }
    return false;
}

#endif

}  // namespace

std::optional<DebInstallPhase> debInstallPhase(std::string_view line)
{
    using Phase = DebInstallPhase;
    if (line == verifyingArchive) return Phase::Verifying;
    if (line == preparingTransaction) return Phase::Preparing;
    if (line == verifyingInstall) return Phase::Finishing;

    const auto takeField = [](std::string_view& text, char delimiter) {
        const auto end = text.find(delimiter);
        const auto value = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        return value;
    };
    if (line.rfind("PROGRESS\t", 0) == 0) {
        // Store emits phase, byte counters, percentage, then the package manager's message.
        line.remove_prefix(sizeof("PROGRESS\t") - 1);
        const auto phase = takeField(line, '\t');
        if (phase == "verify") return Phase::Verifying;
        if (phase == "dependencies") {
            return line.find("\tDownloading dependencies") != std::string_view::npos ?
                Phase::Downloading : Phase::Preparing;
        }
        if (phase != "package-manager") return std::nullopt;
        takeField(line, '\t');
        takeField(line, '\t');
        takeField(line, '\t');
    } else if (line.rfind("pmstatus:", 0) == 0) {
        line.remove_prefix(sizeof("pmstatus:") - 1);
        takeField(line, ':');
        const auto field = takeField(line, ':');
        // APT can qualify the package name with its architecture.
        if (field.find_first_not_of("0123456789.,") != std::string_view::npos) takeField(line, ':');
    } else if (line.rfind("dlstatus:", 0) == 0 || line.rfind("Get:", 0) == 0) {
        return Phase::Downloading;
    }

    if (line.rfind("Preparing to configure ", 0) == 0 || line.rfind("Configuring ", 0) == 0 ||
        line.rfind("Setting up ", 0) == 0 || line.rfind("Running post-installation ", 0) == 0) {
        return Phase::Configuring;
    }
    if (line.rfind("Preparing to unpack ", 0) == 0 || line.rfind("Preparing ", 0) == 0) return Phase::Preparing;
    if (line.rfind("Unpacking ", 0) == 0) return Phase::Unpacking;
    if (line.rfind("Processing triggers ", 0) == 0) return Phase::Finishing;
    if (line.rfind("Installing ", 0) == 0) return Phase::Installing;
    return std::nullopt;
}

std::string debStagingError(const fs::path& directory, std::uintmax_t bytes)
{
    // Keep the sealed review snapshot bounded on devices with 512 MiB of RAM.
    constexpr std::uintmax_t maxArchiveBytes = 64 * 1024 * 1024;
    if (bytes > maxArchiveBytes) {
        return "Debian package exceeds the 64 MiB review limit.";
    }
    std::error_code error;
    const auto space = fs::space(directory, error);
    if (error) {
        return "Cannot check free space for Debian package staging: " + error.message();
    }
    if (space.available < bytes) {
        return "Not enough free space for Debian package staging in " + directory.string() + ".";
    }
    return {};
}

std::vector<std::string> debAptCommand(const fs::path& path, bool simulate)
{
    std::vector<std::string> command{"/usr/bin/apt-get", "-y", "--reinstall", "--no-remove",
                                    "--no-install-recommends", "-o", "Dpkg::Options::=--force-confdef",
                                    "-o", "Dpkg::Options::=--force-confold"};
    if (simulate) {
        command.push_back("--simulate");
    } else {
        command.insert(command.end(), {"-o", "APT::Status-Fd=1"});
    }
    command.insert(command.end(), {"install", path.string()});
    return command;
}

std::string debAptPlan(const std::string& output)
{
    std::istringstream lines(output);
    std::string plan;
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("Inst ", 0) == 0 || line.rfind("Conf ", 0) == 0 ||
            line.rfind("Remv ", 0) == 0 || line.rfind("Purg ", 0) == 0) {
            plan += line + "\n";
        }
    }
    return plan;
}

std::string sha256CommandOutput(const CommandResult& result)
{
    const auto digest = result.output.substr(0, result.output.find(' '));
    return result && !result.outputTruncated && digestValid(digest, 64) ? digest : std::string{};
}

std::string debAuditMessage()
{
    const auto audit = runTimedCommand({"/usr/bin/dpkg", "--audit"}, std::chrono::seconds(10));
    const auto text = trimWhitespace(cleanCommandText(audit.output));
    if (!audit) {
        return "dpkg --audit could not complete. Check package manager state in a terminal.\n" + text;
    }
    if (text.empty()) {
        return "dpkg --audit reported no unfinished packages. Check the error above before retrying.";
    }
    return "dpkg --audit reported unfinished packages. Use Store's recovery if applicable, or ask an administrator "
           "to review the audit and run sudo dpkg --configure -a.\n" + text;
}

bool trustedSystemExecutable(const fs::path& path)
{
#if defined(__linux__)
    if (!path.is_absolute()) {
        return false;
    }
    bool executable = true;
    for (auto element = path; ; element = element.parent_path()) {
        struct stat info{};
        if (lstat(element.c_str(), &info) != 0 || info.st_uid != 0 || (info.st_mode & 022) ||
            (executable ? !S_ISREG(info.st_mode) || !(info.st_mode & S_IXUSR) : !S_ISDIR(info.st_mode))) {
            return false;
        }
        if (element == "/") {
            return true;
        }
        executable = false;
    }
#else
    (void)path;
    return false;
#endif
}

#if defined(__linux__)
namespace {

int installTransaction(char** argv, bool store)
{
    RootStage stage;
    try {
        std::cout << verifyingArchive << std::endl;
        std::string temporary = "/var/tmp/cardputer-files-install-XXXXXX";
        if (!mkdtemp(temporary.data())) {
            throw std::runtime_error("Cannot create protected package storage.");
        }
        stage.directory = temporary;
        const fs::path package = stage.directory / "package.deb";
        copyReviewedArchive(package);
        if (sha256CommandOutput(runCommand({"/usr/bin/sha256sum", "--", package.string()})) != argv[2]) {
            throw std::runtime_error("The prepared package changed after review. Open it again.");
        }

        const std::string name = packageField(package, "Package");
        const std::string version = packageField(package, "Version");
        if (name != argv[4] || version != argv[5] ||
            !validField(name, "abcdefghijklmnopqrstuvwxyz0123456789+.-") ||
            !validField(version, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+.:~-")) {
            throw std::runtime_error("The protected archive does not match the reviewed package metadata.");
        }
        std::cout << preparingTransaction << std::endl;
        const auto audit = runCommand({"/usr/bin/dpkg", "--audit"});
        if (!audit || !trimWhitespace(audit.output).empty()) {
            throw std::runtime_error("Resolve unfinished package operations before installing.");
        }
        if (chmod(stage.directory.c_str(), 0711) != 0) {
            throw std::runtime_error("Cannot make the reviewed archive readable to APT.");
        }
        const auto simulated = runCommand(debAptCommand(package, true));
        const auto plan = debAptPlan(simulated.output);
        const auto planHash = sha256CommandOutput(runCommand({"/usr/bin/sha256sum"}, {}, plan));
        if (!simulated || simulated.outputTruncated || planHash.empty() || planHash != argv[3]) {
            throw std::runtime_error("The APT plan changed since review. Open the package again.\n" + simulated.output);
        }

        const auto depends = packageField(package, "Depends");
        const auto preDepends = packageField(package, "Pre-Depends");
        const auto dependencies = preDepends + (preDepends.empty() || depends.empty() ? "" : ", ") + depends;
        const bool apt = needsApt(plan, name) ||
                         (!dependencies.empty() && !runCommand({"/usr/bin/dpkg-checkbuilddeps", "-d",
                                                               dependencies, "/dev/null"}));
        const auto aptHook = apt || store ? prepareAptHook(package) : fs::path{};
        const auto pending = writePendingRecord(package, name, version, argv[2]);
        stage.preserveForRecovery = true;
        std::cout << "Installing " << name << "...\n" << std::flush;
        {
            const ReapInstallChildren descendants;
            if (store) {
                std::cout << "Using the Store package transaction helper.\n" << std::flush;
                const auto error = storeInstall(package, pending, apt, aptHook);
                if (!error.empty()) {
                    throw std::runtime_error(error);
                }
            } else {
                const std::vector<std::string> dpkg{"/usr/bin/dpkg", "--force-confdef", "--force-confold",
                                                    "--install", package.string()};
                auto command = apt ? debAptCommand(package, false) : dpkg;
                if (apt) {
                    command.insert(command.end() - 2, {"-o", "DPkg::Pre-Install-Pkgs::=" + aptHook.string()});
                }
                const auto timeout = packageTimeout();
                const auto result = runTimedCommand(command, timeout,
                                                    [](const std::string& output) { std::cout << output << std::flush; });
                if (result.timedOut) {
                    throw std::runtime_error("Package installation exceeded the time limit of " +
                                             std::to_string(timeout.count()) + " s. The package manager was stopped.");
                }
                if (!result) {
                    throw std::runtime_error("Package manager failed (exit " + std::to_string(result.exitCode) + ").");
                }
            }
        }
        std::cout << verifyingInstall << std::endl;
        const auto arch = packageField(package, "Architecture");
        const auto installed = runCommand({"/usr/bin/dpkg-query", "-W", "-f=${Status}\\n${Version}", "--",
                                           arch == "all" ? name : name + ":" + arch});
        if (!installed || trimWhitespace(installed.output) != "install ok installed\n" + version) {
            throw std::runtime_error("Could not verify the installed package version.");
        }
        stage.preserveForRecovery = false;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n\n" << debAuditMessage() << '\n';
        if (stage.preserveForRecovery) {
            std::cerr << "\nRecovery files kept in " << stage.directory.string()
                      << ". Review pending-package.json and remove this directory after resolving package state.\n";
        }
        return 1;
    }
}
}  // namespace
#endif

int runDebInstallHelper(int argc, char** argv)
{
#if defined(__linux__)
    if (geteuid() != 0 || argc != 7 || !digestValid(argv[2], 64) || !digestValid(argv[3], 64)) {
        std::cerr << "Invalid privileged package request.\n";
        return 1;
    }
    ScopedFd globalLock, storeLock;
    try {
        umask(022);
        if (setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1) != 0 ||
            setenv("DEBIAN_FRONTEND", "noninteractive", 1) != 0) {
            throw std::runtime_error("Cannot set the package manager environment.");
        }
        unsetenv("APT_CONFIG");
        globalLock.fd = open("/run/cardputer-files-package.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        lockTransaction(globalLock);
        const bool store = storeHelperPresent();
        if (store) {
            openStoreLock(storeLock, argv[6]);
        }

        // fork shares the locked file descriptions. The worker keeps them without
        // passing either descriptor through exec to package tools or maintainer scripts.
        const auto worker = fork();
        if (worker < 0) {
            throw std::runtime_error("Cannot start the package transaction worker.");
        }
        if (worker == 0) {
            // sudo can signal its direct child or the old process group when the UI dies.
            // Progress writes to a closed UI pipe must not terminate this worker.
            if (setsid() < 0 || prctl(PR_SET_CHILD_SUBREAPER, 1L, 0L, 0L, 0L) < 0 ||
                std::signal(SIGCHLD, SIG_DFL) == SIG_ERR || std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
                std::cerr << "Cannot isolate the package transaction worker.\n";
                _exit(1);
            }
            const int result = installTransaction(argv, store);
            std::cerr.flush();
            std::cout.flush();
            _exit(result);
        }
        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(worker, &status, 0);
        } while (waited < 0 && errno == EINTR);
        return waited == worker && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n\n" << debAuditMessage() << '\n';
        return 1;
    }
#else
    (void)argc;
    (void)argv;
    return 1;
#endif
}

}  // namespace files::internal
