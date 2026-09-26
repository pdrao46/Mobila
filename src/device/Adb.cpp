#include "Adb.h"
#include "../core/Log.h"
#include <sstream>
#include <cstdlib>

namespace mob {
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

bool Adb::locate(const std::string& preferred) {
#ifdef _WIN32
    const char* exe = "adb.exe";
    const char sep = '\\';
#else
    const char* exe = "adb";
    const char sep = '/';
#endif
    std::vector<std::string> cands;
    if (!preferred.empty()) cands.push_back(preferred);
    cands.push_back(exeDir() + sep + "platform-tools" + sep + exe);
    cands.push_back(exeDir() + sep + exe);
    for (auto& c : cands)
        if (fileExists(c)) { path_ = c; return true; }
    // PATH
    auto r = execCapture({exe, "version"}, 5000);
    if (r.exitCode == 0) { path_ = exe; return true; }
    return false;
}

std::string Adb::version() {
    auto r = execCapture({path_, "version"}, 5000);
    std::istringstream is(r.out);
    std::string l;
    std::getline(is, l);
    return trim(l);
}
bool Adb::startServer() { return execCapture({path_, "start-server"}, 20000).exitCode == 0; }

std::vector<std::string> Adb::baseArgs(const std::string& serial) const {
    std::vector<std::string> a{path_};
    if (!serial.empty()) { a.push_back("-s"); a.push_back(serial); }
    return a;
}
ExecResult Adb::run(const std::string& serial, std::vector<std::string> args, int timeoutMs) {
    auto a = baseArgs(serial);
    a.insert(a.end(), args.begin(), args.end());
    return execCapture(a, timeoutMs);
}
ExecResult Adb::shell(const std::string& serial, const std::string& cmd, int timeoutMs) {
    return run(serial, {"shell", cmd}, timeoutMs);
}

std::vector<AdbDevice> Adb::devices() {
    std::vector<AdbDevice> out;
    auto r = execCapture({path_, "devices", "-l"}, 5000);
    std::istringstream is(r.out);
    std::string line;
    while (std::getline(is, line)) {
        line = trim(line);
        if (line.empty() || line.rfind("List of", 0) == 0 || line[0] == '*') continue;
        std::istringstream ls(line);
        AdbDevice d;
        ls >> d.serial >> d.state;
        std::string tok;
        while (ls >> tok) {
            if (tok.rfind("model:", 0) == 0) d.model = tok.substr(6);
            else if (tok.rfind("usb:", 0) == 0) d.usbPath = tok;
        }
        if (!d.serial.empty() && !d.state.empty()) out.push_back(d);
    }
    return out;
}

float Adb::batteryTemp(const std::string& serial) {
    auto r = shell(serial, "dumpsys battery | grep temperature", 4000);
    auto p = r.out.find(':');
    if (p == std::string::npos) return -1;
    return (float)std::atof(r.out.c_str() + p + 1) / 10.0f;
}

DeviceInfo Adb::queryInfo(const std::string& serial) {
    DeviceInfo i;
    i.serial = serial;
    // Uma única ida ao dispositivo (cada "adb shell" custa dezenas de ms).
    auto r = shell(serial,
        "getprop ro.product.manufacturer; getprop ro.product.model; getprop ro.build.version.release;"
        " getprop ro.build.version.sdk; getprop ro.product.cpu.abi; echo @@; wm size; echo @@; wm density;"
        " echo @@; dumpsys display | grep -m1 -oE 'fps=[0-9.]+|refreshRate [0-9.]+|mRefreshRate=[0-9.]+'",
        8000);
    std::vector<std::string> sec(1);
    std::istringstream is(r.out);
    std::string l;
    while (std::getline(is, l)) {
        l = trim(l);
        if (l == "@@") { sec.emplace_back(); continue; }
        sec.back() += l + "\n";
    }
    std::istringstream p(sec[0]);
    std::getline(p, i.manufacturer); std::getline(p, i.model); std::getline(p, i.androidVersion);
    std::getline(p, i.sdk); std::getline(p, i.abi);
    auto lastNum = [](const std::string& s, const char* key) -> std::string {
        auto k = s.rfind(key);  // "Override size" aparece depois de "Physical size"
        return k == std::string::npos ? "" : trim(s.substr(k + std::string(key).size()));
    };
    if (sec.size() > 1) {
        std::string sz = lastNum(sec[1], "size:");
        std::sscanf(sz.c_str(), "%dx%d", &i.width, &i.height);
    }
    if (sec.size() > 2) i.density = std::atoi(lastNum(sec[2], "density:").c_str());
    if (sec.size() > 3) {
        auto s = sec[3];
        auto k = s.find_first_of("0123456789");
        if (k != std::string::npos) i.refreshRate = (float)std::atof(s.c_str() + k);
    }
    i.batteryTempC = batteryTemp(serial);
    LOGI("dispositivo %s: %s %s Android %s %dx%d %d dpi %.0f Hz", serial.c_str(), i.manufacturer.c_str(),
         i.model.c_str(), i.androidVersion.c_str(), i.width, i.height, i.density, i.refreshRate);
    return i;
}
}
