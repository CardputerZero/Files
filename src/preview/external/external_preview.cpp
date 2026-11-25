#include "preview/external/external_preview.hpp"

#include "models/file_type_registry.hpp"
#include "preview/external/desktop_entry.hpp"
#include <lvgl.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace files {
namespace {

struct ExternalHandler {
    std::string desktopFile;
    std::string name;
    std::string workDir;
    std::vector<std::string> argvTemplate;  // "%f" marks where the path goes
    std::vector<std::string> extensions;
};

std::string applicationsDir()
{
    const char* dir = std::getenv("FILES_APPLICATIONS_DIR");
    if (dir && dir[0] != '\0') {
        return dir;
    }
    return "/usr/share/APPLaunch/applications";
}

std::string parentDir(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    return slash == 0 ? "/" : path.substr(0, slash);
}

bool parseDesktopFile(const std::string& path, const std::string& appRoot, ExternalHandler& handler)
{
    std::ifstream in(path);
    DesktopEntry entry;
    if (!parseDesktopEntry(in, entry)) {
        return false;
    }
    if (entry.hidden || !entry.application || entry.exec.empty() || entry.extensions.empty()) {
        return false;
    }
    std::vector<std::string> argv;
    if (!splitDesktopExec(entry.exec, argv)) {
        spdlog::warn("ExternalPreview: unusable Exec in {}", path);
        return false;
    }
    if (argv.front().front() != '/') {
        argv.front() = appRoot + "/" + argv.front();  // APPLaunch Exec is relative to its root
    }
#if defined(__unix__) || defined(__APPLE__)
    if (::access(argv.front().c_str(), X_OK) != 0) {
        spdlog::warn("ExternalPreview: {} is not executable ({})", argv.front(), path);
        return false;
    }
#endif
    handler.desktopFile  = path;
    handler.name         = entry.name.empty() ? argv.front() : entry.name;
    handler.workDir      = appRoot;
    handler.argvTemplate = std::move(argv);
    handler.extensions   = std::move(entry.extensions);
    return true;
}

// Reads the applications folder each time a file is opened, so apps
// installed or removed while Files runs are picked up. It holds a few dozen
// small files; this is cheap next to starting a preview.
bool findHandler(const std::string& extension, ExternalHandler& out)
{
#if defined(__unix__) || defined(__APPLE__)
    if (extension.empty()) {
        return false;
    }
    const std::string ext  = normalizedExtension(extension);
    const std::string dir  = applicationsDir();
    const std::string root = parentDir(dir);
    DIR* d                 = ::opendir(dir.c_str());
    if (!d) {
        return false;
    }
    std::vector<std::string> names;
    while (dirent* entry = ::readdir(d)) {
        const std::string name = entry->d_name;
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".desktop") == 0) {
            names.push_back(name);
        }
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());  // stable choice when two apps claim a type
    for (const std::string& name : names) {
        ExternalHandler handler;
        if (!parseDesktopFile(dir + "/" + name, root, handler)) {
            continue;
        }
        if (std::find(handler.extensions.begin(), handler.extensions.end(), ext) != handler.extensions.end()) {
            out = std::move(handler);
            return true;
        }
    }
#else
    (void)extension;
    (void)out;
#endif
    return false;
}

class ExternalPreviewPage : public PreviewPage {
public:
    ExternalPreviewPage(FileEntry file, ExternalHandler handler)
        : _file(std::move(file)), _handler(std::move(handler)), _title(_file.name.empty() ? _file.path : _file.name)
    {
    }

    ~ExternalPreviewPage() override
    {
        stopChild();
    }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        (void)parent;
        if (!_started) {
            _started = true;
            launch();
        }
    }

    void detach() override
    {
        stopChild();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        // While the app runs it reads the keyboard itself.
        if (_finished && key == '\x1b') {
            router.back();
        }
    }

    void tick(uint32_t nowMs) override
    {
        (void)nowMs;
        poll();
    }

    bool shouldClose() const override
    {
        return _finished;
    }

    bool suspendsHostRendering() const override
    {
        return _started && !_finished;
    }

    bool capturesInput() const override
    {
        return _started && !_finished;
    }

private:
    FileEntry _file;
    ExternalHandler _handler;
    std::string _title;
    bool _started  = false;
    bool _finished = false;
#if defined(__unix__) || defined(__APPLE__)
    pid_t _pid = -1;
#endif

    void launch()
    {
#if defined(__unix__) || defined(__APPLE__)
        std::vector<std::string> args;
        for (const std::string& arg : _handler.argvTemplate) {
            args.push_back(arg == "%f" ? _file.path : arg);
        }
        std::vector<char*> argv;
        for (std::string& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        // Build the environment before fork: the child may only call
        // async-signal-safe functions, and Files runs other threads.
        std::vector<std::string> env;
        for (char** var = environ; var && *var; ++var) {
            if (std::strncmp(*var, "FILES_OPENED_FROM=", 18) != 0) {
                env.emplace_back(*var);
            }
        }
        env.emplace_back("FILES_OPENED_FROM=1");
        std::vector<char*> envp;
        for (std::string& var : env) {
            envp.push_back(var.data());
        }
        envp.push_back(nullptr);

        _pid = ::fork();
        if (_pid < 0) {
            spdlog::error("ExternalPreview: fork failed for {}: {}", _handler.name, std::strerror(errno));
            finish();
            return;
        }
        if (_pid == 0) {
            if (!_handler.workDir.empty() && ::chdir(_handler.workDir.c_str()) != 0) {
                // keep the inherited directory
            }
            ::execve(argv[0], argv.data(), envp.data());
            _exit(127);
        }
        spdlog::info("ExternalPreview: opened {} with {} (pid {})", _file.path, _handler.name,
                     static_cast<int>(_pid));
#else
        spdlog::error("ExternalPreview: unsupported on this platform");
        finish();
#endif
    }

    void poll()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_pid <= 0) {
            return;
        }
        int status         = 0;
        const pid_t result = ::waitpid(_pid, &status, WNOHANG);
        if (result == _pid || (result < 0 && errno != EINTR)) {
            if (result == _pid && WIFEXITED(status) && WEXITSTATUS(status) == 127) {
                spdlog::error("ExternalPreview: could not start {}", _handler.argvTemplate.front());
            } else {
                spdlog::info("ExternalPreview: {} closed", _handler.name);
            }
            _pid = -1;
            finish();
        }
#endif
    }

    void stopChild()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_pid <= 0) {
            return;
        }
        int status = 0;
        if (::waitpid(_pid, &status, WNOHANG) == 0) {
            ::kill(_pid, SIGTERM);
            for (int attempt = 0; attempt < 50; ++attempt) {
                if (::waitpid(_pid, &status, WNOHANG) == _pid) {
                    _pid = -1;
                    break;
                }
                ::usleep(20000);
            }
            if (_pid > 0) {
                ::kill(_pid, SIGKILL);
                (void)::waitpid(_pid, &status, 0);
            }
        }
        _pid = -1;
#endif
        finish();
    }

    void finish()
    {
        if (_finished) {
            return;
        }
        _finished = true;
        // The other app drew over the whole screen: repaint everything.
        if (lv_screen_active()) {
            lv_obj_invalidate(lv_screen_active());
        }
    }
};

class ExternalPreviewSupport : public PreviewSupport {
public:
    const char* id() const override
    {
        return "external";
    }

    bool supports(const FileEntry& file) const override
    {
        if (!isRegularPreviewFile(file)) {
            return false;
        }
        ExternalHandler handler;
        return findHandler(file.extension, handler);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        if (!isRegularPreviewFile(file)) {
            return nullptr;
        }
        ExternalHandler handler;
        if (!findHandler(file.extension, handler)) {
            return nullptr;
        }
        return std::make_unique<ExternalPreviewPage>(file, std::move(handler));
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createExternalPreviewSupport()
{
    return std::make_unique<ExternalPreviewSupport>();
}

}  // namespace files
