#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

// A model-tagged ipodpatcher image copied into an immutable Linux memfd.
// No raw disk writes are performed here: ipodpatcher is always device-bound.
class iPodFirmware {
public:
    ~iPodFirmware();
    static std::shared_ptr<iPodFirmware> load(const std::string& filename,
                                             int generation, std::string& error);
    static bool supported(int generation);
    static bool toolsAvailable(std::string& error);
    std::string description() const;
    bool flash(const std::string& stableDevice, unsigned long long partitionSize,
               const std::function<bool(std::string&)>& revalidate,
               std::string& error) const;
private:
    iPodFirmware() = default;
    int fd_ = -1;
    int modelNumber_ = 0;
    std::string modelTag_, digest_;
    std::vector<unsigned char> bytes_;
    bool validate(const std::vector<unsigned char>& bytes, std::string& error) const;
};
