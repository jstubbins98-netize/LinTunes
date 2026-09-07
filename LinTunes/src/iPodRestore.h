#pragma once

#include <functional>
#include <string>
#include <vector>

enum class iPodRestoreKind {
    TouchIPSW,
    ClassicFirmware
};

struct iPodRestoreResult {
    bool success = false;
    int exit_code = -1;
    std::string message;
};

struct iPodRestoreTarget {
    // Touch restore: device UDID selected before destructive confirmation.
    std::string identifier;
};

using RestoreOutputCallback = std::function<void(const std::string&)>;

class iPodRestore {
public:
    static bool commandAvailable(const std::string& command);
    static bool validFirmwareFile(iPodRestoreKind kind, const std::string& path,
                                  std::string& error);
    static std::vector<std::string> commandArguments(
        iPodRestoreKind kind, const std::string& firmware_path,
        const iPodRestoreTarget& target);
    static std::vector<std::string> discoverTouchDeviceIds(std::string& error);
    static bool confirmationMatches(const std::string& typed,
                                    const std::string& required);
    static iPodRestoreResult restore(iPodRestoreKind kind,
                                     const std::string& firmware_path,
                                     const iPodRestoreTarget& target,
                                     RestoreOutputCallback output = nullptr);

private:
    static bool targetStillMatches(iPodRestoreKind kind,
                                   const iPodRestoreTarget& target,
                                   std::string& error);
    static iPodRestoreResult run(const std::vector<std::string>& args,
                                 RestoreOutputCallback output);
};