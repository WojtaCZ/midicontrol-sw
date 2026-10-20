#include "midicontrol/config.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace mc {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

int parseInt(const std::string& key, const std::string& value, int minValue) {
    size_t used = 0;
    int v = 0;
    try {
        v = std::stoi(value, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (used == 0 || used != value.size() || v < minValue) {
        throw std::runtime_error("config: invalid value for " + key + ": '" + value + "'");
    }
    return v;
}

bool parseBool(const std::string& key, const std::string& value) {
    std::string v = upper(value);
    if (v == "1" || v == "TRUE" || v == "YES" || v == "ON") return true;
    if (v == "0" || v == "FALSE" || v == "NO" || v == "OFF") return false;
    throw std::runtime_error("config: invalid boolean for " + key + ": '" + value + "'");
}

bool isDir(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace

std::map<std::string, std::map<std::string, std::string>> parseIni(const std::string& text) {
    std::map<std::string, std::map<std::string, std::string>> sections;
    std::istringstream in(text);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = upper(trim(t.substr(1, t.size() - 2)));
            sections[section];  // ensure the section exists even if empty
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string value = trim(t.substr(eq + 1));
        if (!key.empty()) sections[section][key] = value;
    }
    return sections;
}

Config parseConfig(const std::string& iniText) {
    auto sections = parseIni(iniText);

    auto filesIt = sections.find("FILES");
    if (filesIt == sections.end()) {
        throw std::runtime_error("config: missing [FILES] section");
    }
    const auto& files = filesIt->second;

    Config cfg;
    auto playIt = files.find("PlayFolder");
    if (playIt == files.end()) throw std::runtime_error("config: PlayFolder not specified");
    cfg.playFolder = playIt->second;

    auto recIt = files.find("RecordFolder");
    if (recIt == files.end()) throw std::runtime_error("config: RecordFolder not specified");
    cfg.recFolder = recIt->second;

    auto bindIt = sections.find("BINDINGS");
    if (bindIt != sections.end()) cfg.bindings = bindIt->second;

    auto playbackIt = sections.find("PLAYBACK");
    if (playbackIt != sections.end()) {
        const auto& pb = playbackIt->second;
        auto getInt = [&](const char* key, int& out, int minValue) {
            auto it = pb.find(key);
            if (it != pb.end()) out = parseInt(std::string(key), it->second, minValue);
        };
        auto getBool = [&](const char* key, bool& out) {
            auto it = pb.find(key);
            if (it != pb.end()) out = parseBool(std::string(key), it->second);
        };
        getInt("WarmupMs", cfg.warmupMs, 0);
        getBool("KeepPowerOn", cfg.keepPowerOn);
        getInt("VerseGapMs", cfg.verseGapMs, 0);
    }

    auto webIt = sections.find("WEB");
    if (webIt != sections.end()) {
        const auto& web = webIt->second;
        if (auto it = web.find("Enabled"); it != web.end()) {
            cfg.webEnabled = parseBool("Enabled", it->second);
        }
        if (auto it = web.find("Port"); it != web.end()) {
            cfg.webPort = parseInt("Port", it->second, 1);
            if (cfg.webPort > 65535) {
                throw std::runtime_error("config: invalid value for Port: '" + it->second + "'");
            }
        }
        if (auto it = web.find("Bind"); it != web.end()) {
            if (it->second.empty()) throw std::runtime_error("config: Bind is empty");
            cfg.webBind = it->second;
        }
    }

    auto diagIt = sections.find("DIAG");
    if (diagIt != sections.end()) {
        const auto& d = diagIt->second;
        if (auto it = d.find("LogPort"); it != d.end()) cfg.diagLogPort = it->second;
        if (auto it = d.find("Dir"); it != d.end()) cfg.diagDir = it->second;
    }

    return cfg;
}

Config loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("config: unable to find the config file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    Config cfg = parseConfig(ss.str());

    std::string configDir = ".";
    auto slash = path.find_last_of('/');
    if (slash != std::string::npos) configDir = slash == 0 ? "/" : path.substr(0, slash);
    const char* home = std::getenv("HOME");
    cfg.playFolder = resolveFolder(cfg.playFolder, configDir, home ? home : "");
    cfg.recFolder = resolveFolder(cfg.recFolder, configDir, home ? home : "");
    // The diagnostics directory is created by the store (recursively), so it need not exist yet.
    cfg.diagDir = cfg.diagDir.empty() ? defaultDiagDir([](const char* k) { return std::getenv(k); })
                                      : resolveFolder(cfg.diagDir, configDir, home ? home : "");

    if (!isDir(cfg.playFolder)) {
        throw std::runtime_error("config: play folder not found: " + cfg.playFolder);
    }
    if (!isDir(cfg.recFolder)) {
        throw std::runtime_error("config: record folder not found: " + cfg.recFolder);
    }
    return cfg;
}

std::string resolveFolder(const std::string& value, const std::string& configDir,
                          const std::string& home) {
    if (value == "~") return home;
    if (value.rfind("~/", 0) == 0) return home + value.substr(1);
    if (!value.empty() && value[0] == '/') return value;
    if (value.empty()) return value;
    return configDir + "/" + value;
}

std::string defaultDiagDir(const std::function<const char*(const char*)>& getenv) {
    const char* xdg = getenv("XDG_STATE_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/midicontrol";
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home) + "/.local/state/midicontrol";
    return {};
}

std::string resolveConfigPath(const std::string& cliPath, const EnvLookup& getenv,
                              const FileExists& exists) {
    if (!cliPath.empty()) return cliPath;
    const char* env = getenv("MIDICONTROL_CONFIG");
    if (env && *env) return env;

    std::string xdg;
    const char* xdgHome = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (xdgHome && *xdgHome) {
        xdg = std::string(xdgHome) + "/midicontrol/config.conf";
    } else if (home && *home) {
        xdg = std::string(home) + "/.config/midicontrol/config.conf";
    }
    if (!xdg.empty() && exists(xdg)) return xdg;
    return "config.conf";
}

std::optional<std::string> Config::binding(const std::string& key) const {
    auto it = bindings.find(key);
    if (it == bindings.end()) return std::nullopt;
    return it->second;
}

}  // namespace mc
