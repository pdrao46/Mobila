#pragma once
#include <string>
#include <vector>
#include "../core/Platform.h"

namespace mob {

struct AdbDevice {
    std::string serial;
    std::string state;      // device / unauthorized / offline
    std::string model;
    std::string usbPath;    // "usb:1-4"
    bool isUsb() const { return serial.find(':') == std::string::npos && serial.rfind("emulator-", 0) != 0; }
};

struct DeviceInfo {
    std::string serial, manufacturer, model, androidVersion, sdk, abi;
    int width = 0, height = 0, density = 0;
    float refreshRate = 0;
    std::vector<std::string> videoEncoders;  // "h264 c2.qti.avc.encoder (hw)"
    float batteryTempC = -1;
};

class Adb {
public:
    bool locate(const std::string& preferred);
    const std::string& path() const { return path_; }
    std::string version();
    bool startServer();
    std::vector<AdbDevice> devices();
    DeviceInfo queryInfo(const std::string& serial);
    float batteryTemp(const std::string& serial);
    ExecResult run(const std::string& serial, std::vector<std::string> args, int timeoutMs = 15000);
    ExecResult shell(const std::string& serial, const std::string& cmd, int timeoutMs = 15000);
    std::vector<std::string> baseArgs(const std::string& serial) const;
private:
    std::string path_;
};
}
