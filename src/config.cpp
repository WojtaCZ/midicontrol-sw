#include "midicontrol/config.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
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

    return cfg;
}

Config loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("config: unable to find the config file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    Config cfg = parseConfig(ss.str());

    if (!isDir(cfg.playFolder)) {
        throw std::runtime_error("config: play folder not found: " + cfg.playFolder);
    }
    if (!isDir(cfg.recFolder)) {
        throw std::runtime_error("config: record folder not found: " + cfg.recFolder);
    }
    return cfg;
}

std::optional<std::string> Config::binding(const std::string& key) const {
    auto it = bindings.find(key);
    if (it == bindings.end()) return std::nullopt;
    return it->second;
}

}  // namespace mc
