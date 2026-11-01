#include "iPodRestore.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace fs = std::filesystem;

bool iPodRestore::commandAvailable(const std::string& command) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::string path = path_env;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(':', start);
        std::string dir = path.substr(start, end == std::string::npos
            ? std::string::npos : end - start);
        if (dir.empty()) dir = ".";
        std::error_code ec;
        fs::path candidate = fs::path(dir) / command;
        if (fs::is_regular_file(candidate, ec) && !ec &&
            access(candidate.c_str(), X_OK) == 0)
            return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

bool iPodRestore::validFirmwareFile(iPodRestoreKind kind,
                                    const std::string& path,
                                    std::string& error) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        error = "The selected firmware file cannot be read.";
        return false;
    }
    auto size = fs::file_size(path, ec);
    if (ec || size < 1024 * 1024) {
        error = "The selected file is too small to be an iPod firmware image.";
        return false;
    }
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (kind == iPodRestoreKind::TouchIPSW && ext != ".ipsw") {
        error = "iPod touch restore requires an Apple .ipsw firmware file.";
        return false;
    }
    if (kind == iPodRestoreKind::ClassicFirmware &&
        ext != ".bin" && ext != ".ipod" && ext != ".img") {
        error = "Disk-mode iPod firmware must be a model-specific .bin, .ipod, or .img file.";
        return false;
    }
    return true;
}

std::vector<std::string> iPodRestore::commandArguments(
    iPodRestoreKind kind, const std::string& firmware_path,
    const iPodRestoreTarget& target) {
    if (kind == iPodRestoreKind::TouchIPSW)
        return {"idevicerestore", "-u", target.identifier, "-e", firmware_path};
    // Stock ipodpatcher auto-selects a disk and provides no supported way to
    // bind the write to LinTunes' selected iPod. Never construct a command
    // that could silently target another attached device.
    return {};
}

std::vector<std::string> iPodRestore::discoverTouchDeviceIds(std::string& error) {
    if (!commandAvailable("idevice_id")) {
        error = "idevice_id is not installed; the intended iPod touch cannot be identified.";
        return {};
    }
    std::string text;
    auto result = run({"idevice_id", "-l"},
        [&text](const std::string& chunk) { text += chunk; });
    if (!result.success) {
        error = result.message;
        return {};
    }
    std::vector<std::string> ids;
    std::istringstream lines(text);
    std::string id;
    while (std::getline(lines, id)) {
        if (!id.empty() && id.find_first_of(" \t\r") == std::string::npos)
            ids.push_back(id);
    }
    if (ids.empty())
        error = "No identifiable iPod touch is connected.";
    return ids;
}

iPodRestoreResult iPodRestore::restore(iPodRestoreKind kind,
                                       const std::string& firmware_path,
                                       const iPodRestoreTarget& target,
                                       RestoreOutputCallback output) {
    std::string error;
    if (!validFirmwareFile(kind, firmware_path, error))
        return {false, -1, error};
    if (!targetStillMatches(kind, target, error))
        return {false, -1, error};

    const char* tool = kind == iPodRestoreKind::TouchIPSW
        ? "idevicerestore" : "ipodpatcher";
    if (!commandAvailable(tool))
        return {false, -1, std::string(tool) + " is not installed."};
    return run(commandArguments(kind, firmware_path, target), std::move(output));
}

bool iPodRestore::targetStillMatches(iPodRestoreKind kind,
                                     const iPodRestoreTarget& target,
                                     std::string& error) {
    if (target.identifier.empty()) {
        error = "The intended iPod could not be identified; restore was cancelled.";
        return false;
    }
    if (kind == iPodRestoreKind::TouchIPSW) {
        auto ids = discoverTouchDeviceIds(error);
        if (std::find(ids.begin(), ids.end(), target.identifier) == ids.end()) {
            error = "The selected iPod touch is no longer connected; restore was cancelled.";
            return false;
        }
        return true;
    }
    error = "Disk-mode firmware restore is disabled because ipodpatcher cannot "
            "bind its destructive write to the iPod selected in LinTunes.";
    return false;
}

iPodRestoreResult iPodRestore::run(const std::vector<std::string>& args,
                                   RestoreOutputCallback output) {
    if (args.empty()) return {false, -1, "No restore command was provided."};

    int pipefd[2];
    if (pipe(pipefd) != 0)
        return {false, -1, std::string("Could not create restore pipe: ") + std::strerror(errno)};

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {false, -1, std::string("Could not start restore tool: ") + std::strerror(errno)};
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    std::array<char, 1024> buffer{};
    std::string all_output;
    ssize_t count;
    while ((count = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
        std::string chunk(buffer.data(), static_cast<size_t>(count));
        all_output += chunk;
        if (output) output(chunk);
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    if (exit_code == 0)
        return {true, 0, "The iPod restore completed successfully."};
    if (all_output.size() > 4000)
        all_output = all_output.substr(all_output.size() - 4000);
    return {false, exit_code, all_output.empty()
        ? "The restore tool failed without an error message." : all_output};
}