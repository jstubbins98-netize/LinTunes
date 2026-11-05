#pragma once
#include <string>
#include <memory>
#include "iPodFirmware.h"

// Formatting preserves the partition table. Optional firmware restoration uses
// a verified snapshot and a stable, explicitly selected ipodpatcher target.
// Apple partition maps require a model-aware Windows restore, not mkfs.
class iPodVolume {
public:
    static std::string filesystemAt(const std::string& mount);
    static bool windowsWritable(const std::string& mount, std::string& error);
    bool inspect(const std::string& mount, std::string& error);
    std::string description() const;
    bool format(std::string& newMount, std::string& error) const;
    bool prepareFirmware(const std::string& filename, std::string& error);
    std::string firmwareDescription() const;
private:
    std::string mount_, block_, drive_, table_, serial_, model_, uuid_, fsType_;
    std::string sysInfo_, extended_;
    unsigned long long device_ = 0, size_ = 0, offset_ = 0;
    int generation_ = 0;
    std::shared_ptr<iPodFirmware> firmware_;
    std::string diskAlias_, firmwareBlock_;
    unsigned long long diskNumber_ = 0, firmwareSize_ = 0, firmwareOffset_ = 0;
    bool validate(bool mounted, std::string& error) const;
    bool validateFirmwareTarget(std::string& error) const;
    bool unmounted(std::string& error) const;
};
