#pragma once
#include <string>

// Formatting deliberately preserves the partition table and firmware.
// Apple partition maps require a model-aware Windows restore, not mkfs.
class iPodVolume {
public:
    static std::string filesystemAt(const std::string& mount);
    static bool windowsWritable(const std::string& mount, std::string& error);
    bool inspect(const std::string& mount, std::string& error);
    std::string description() const;
    bool format(std::string& newMount, std::string& error) const;
private:
    std::string mount_, block_, drive_, table_, serial_, model_, uuid_, fsType_;
    std::string sysInfo_, extended_;
    unsigned long long device_ = 0, size_ = 0, offset_ = 0;
    bool validate(bool mounted, std::string& error) const;
};
