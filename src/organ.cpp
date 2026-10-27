#include "midicontrol/organ.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace mc::organ {

namespace {

struct DefaultStop {
    const char* name;
    const char* st;
};

// Index = stop number - 1 (spec "Facts about the organ").
// Czech organ terminology as in Czech parish dispositions (noun first: "Flétna trubicová",
// mixtures by ranks: "Mixtura 3řadá"), mapped from the Opus 910 console, which is labelled in Dutch
// (comment after each line). [ORGAN] NameN overrides any of them.
const DefaultStop kDefaultStops[] = {
    {"Spojka II/I", "60"}, {"Spojka I/P", "61"}, {"Spojka II/P", "62"},  // Zwelwerk-Hoofdwerk, Hoofdwerk-/Zwelwerk-Pedaal
    {"Tremolo I", "63"}, {"Tremolo II", "64"}, {"Chorus", "65"},        // Tremulant Hoofdwerk/Zwelwerk, Chorus
    {"Manuálový bas", "66"},                                            // Manuaal Bas
    {"Kontrabas 16'", "01"}, {"Subbas 16'", "02"}, {"Principálbas 8'", "03"},     // Contrabas, Subbas, Prestant
    {"Kryt 8'", "04"}, {"Basflétna 4'", "05"}, {"Mixtura 3řadá", "06"},           // Gedekt, Fluitbas, Mixtuur III
    {"Pozoun 16'", "07"}, {"Trompeta 8'", "10"}, {"MIDI pedál", "11"},             // Bazuin, Trompet, MIDI Pedaal
    {"Bordun 16'", "14"}, {"Principál 8'", "15"}, {"Flétna trubicová 8'", "16"},   // Bourdon, Prestant, Roerfluit
    {"Gamba 8'", "17"}, {"Oktáva 4'", "20"}, {"Flétna otevřená 4'", "21"},         // Gamba, Octaaf, Open Fluit
    {"Kvinta 2 2/3'", "22"}, {"Superoktáva 2'", "23"}, {"Flétna zobcová 2'", "24"},  // Quint, Superoctaaf, Blockfluit
    {"Kornet 4řadý", "25"}, {"Mixtura 4-6řadá", "26"}, {"Trompeta 8'", "27"},      // Cornet IV, Mixtuur IV-VI, Trompet
    {"MIDI I. manuál", "30"},                                                    // MIDI Hoofdwerk
    {"Principál 8'", "33"}, {"Flétna dutá 8'", "34"}, {"Viola da gamba 8'", "35"},  // Prestant, Holpijp, Viola di Gamba
    {"Vox coelestis 8'", "36"}, {"Oktáva 4'", "37"}, {"Flétna 4'", "40"},          // Vox Celeste, Octaaf, Fluit
    {"Flétna kvintová 2 2/3'", "41"}, {"Flétna lesní 2'", "42"}, {"Tercie 1 3/5'", "43"},  // Quintfluit, Woudfluit, Terts
    {"Rauschpfeife 2-4řadá", "44"}, {"Dulcián 8'", "45"}, {"Hoboj 8'", "46"},      // Ruispijp II-IV, Dulciaan, Hobo
    {"MIDI II. manuál", "47"},                                                   // MIDI Zwelwerk
};
constexpr int kDefaultStopCount = static_cast<int>(sizeof(kDefaultStops) / sizeof(kDefaultStops[0]));

[[noreturn]] void bad(const std::string& key, const std::string& value) {
    throw std::runtime_error("config: invalid value for " + key + ": '" + value + "'");
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isdigit(c)) return false;
    }
    return true;
}

int toInt(const std::string& key, const std::string& value, int lo, int hi) {
    std::string v = trim(value);
    bool neg = !v.empty() && v[0] == '-';
    if (!allDigits(neg ? v.substr(1) : v) || v.size() > 9) bad(key, value);
    int n = std::stoi(v);
    if (n < lo || n > hi) bad(key, value);
    return n;
}

bool toBool(const std::string& key, const std::string& value) {
    std::string v;
    for (unsigned char c : trim(value)) v += static_cast<char>(std::toupper(c));
    if (v == "1" || v == "TRUE" || v == "YES" || v == "ON") return true;
    if (v == "0" || v == "FALSE" || v == "NO" || v == "OFF") return false;
    bad(key, value);
}

std::vector<uint8_t> toHexBytes(const std::string& key, const std::string& value) {
    std::vector<uint8_t> out;
    std::istringstream in(value);
    std::string tok;
    while (in >> tok) {
        if (tok.size() > 2) bad(key, value);
        for (unsigned char c : tok) {
            if (!std::isxdigit(c)) bad(key, value);
        }
        out.push_back(static_cast<uint8_t>(std::stoi(tok, nullptr, 16)));
    }
    if (out.empty()) bad(key, value);
    return out;
}

// "LABEL:first-last, ..." contiguous from 1.
std::vector<StopGroup> toGroups(const std::string& value) {
    std::vector<StopGroup> groups;
    std::istringstream in(value);
    std::string item;
    int next = 1;
    while (std::getline(in, item, ',')) {
        item = trim(item);
        auto colon = item.find(':');
        auto dash = item.find('-', colon == std::string::npos ? 0 : colon);
        if (colon == std::string::npos || dash == std::string::npos) bad("Groups", value);
        StopGroup g;
        g.label = trim(item.substr(0, colon));
        std::string a = trim(item.substr(colon + 1, dash - colon - 1));
        std::string b = trim(item.substr(dash + 1));
        if (g.label.empty() || !allDigits(a) || !allDigits(b) || a.size() > 4 || b.size() > 4) {
            bad("Groups", value);
        }
        g.first = std::stoi(a);
        g.last = std::stoi(b);
        if (g.first != next || g.last < g.first) bad("Groups", value);
        next = g.last + 1;
        groups.push_back(g);
    }
    if (groups.empty()) bad("Groups", value);
    return groups;
}

bool isOctal(const std::string& s) {
    if (s.empty() || s.size() > 4) return false;
    for (char c : s) {
        if (c < '0' || c > '7') return false;
    }
    return true;
}

// Key "<prefix><N>" with N all digits -> N; 0 if the key is not of that form.
int numberedKey(const std::string& key, const std::string& prefix) {
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) return 0;
    std::string n = key.substr(prefix.size());
    if (!allDigits(n) || n.size() > 6) return 0;
    return std::stoi(n);
}

}  // namespace

OrganConfig defaultOrgan() {
    OrganConfig o;
    o.allOffSysex = {0xF0, 0x00, 0x4A, 0x4F, 0x48, 0x41, 0x53, 0x7F, 0xF7};
    o.groups = {{"Spojky a pomocná zařízení", 1, 7}, {"Pedál", 8, 16}, {"I. manuál – hlavní stroj", 17, 29},
                {"II. manuál – žaluziový stroj", 30, 42}};
    for (int i = 0; i < kDefaultStopCount; ++i) {
        o.stops.push_back({i + 1, kDefaultStops[i].name, kDefaultStops[i].st, std::nullopt});
    }
    return o;
}

int programFor(const OrganConfig& organ, int stopNumber) {
    if (stopNumber < 1 || stopNumber > organ.stopCount()) return -1;
    const Stop& s = organ.stops[static_cast<size_t>(stopNumber - 1)];
    if (s.program) return *s.program;
    if (!isOctal(s.st)) return -1;
    return std::stoi(s.st, nullptr, 8) + organ.programOffset;
}

OrganConfig parseOrgan(const std::map<std::string, std::string>& section) {
    OrganConfig o = defaultOrgan();
    auto get = [&](const char* key) -> const std::string* {
        auto it = section.find(key);
        return it == section.end() ? nullptr : &it->second;
    };

    if (auto v = get("StopsEnabled")) o.stopsEnabled = toBool("StopsEnabled", *v);
    if (auto v = get("Channel")) o.channel = toInt("Channel", *v, 1, 16);
    if (auto v = get("ProgramOffset")) o.programOffset = toInt("ProgramOffset", *v, -127, 127);
    if (auto v = get("AllOffSysex")) o.allOffSysex = toHexBytes("AllOffSysex", *v);
    if (auto v = get("AllOffDelayMs")) o.allOffDelayMs = toInt("AllOffDelayMs", *v, 0, 60000);
    if (auto v = get("StopDelayMs")) o.stopDelayMs = toInt("StopDelayMs", *v, 0, 60000);
    if (auto v = get("DropSongProgramChanges")) {
        o.dropSongProgramChanges = toBool("DropSongProgramChanges", *v);
    }

    if (auto v = get("Groups")) {
        o.groups = toGroups(*v);
        // Stops beyond the defaults get a placeholder name and need a ProgramN.
        int count = o.groups.back().last;
        o.stops.resize(static_cast<size_t>(count), Stop{0, "", "", std::nullopt});
        for (int i = kDefaultStopCount; i < count; ++i) {
            o.stops[static_cast<size_t>(i)] = {i + 1, "Rejstřík " + std::to_string(i + 1), "", std::nullopt};
        }
    }

    for (const auto& [key, value] : section) {
        if (int n = numberedKey(key, "Name")) {
            if (n > o.stopCount()) bad(key, value);
            o.stops[static_cast<size_t>(n - 1)].name = value;
        } else if (int n2 = numberedKey(key, "ST")) {
            if (n2 > o.stopCount() || !isOctal(value)) bad(key, value);
            o.stops[static_cast<size_t>(n2 - 1)].st = value;
        } else if (int n3 = numberedKey(key, "Program")) {
            if (n3 > o.stopCount()) bad(key, value);
            o.stops[static_cast<size_t>(n3 - 1)].program = toInt(key, value, 0, 127);
        }
    }

    for (const Stop& s : o.stops) {
        int p = programFor(o, s.number);
        if (p < 0 || p > 127) {
            throw std::runtime_error("config: stop " + std::to_string(s.number) +
                                     " has no valid program (check ST" + std::to_string(s.number) +
                                     ", Program" + std::to_string(s.number) + ", ProgramOffset)");
        }
    }
    return o;
}

std::vector<MidiStep> buildPreamble(const OrganConfig& organ, const std::vector<int>& stops) {
    std::vector<MidiStep> steps;
    steps.push_back({organ.allOffSysex, organ.allOffDelayMs / 1000.0});
    const uint8_t status = static_cast<uint8_t>(0xC0 | (organ.channel - 1));
    for (int n : stops) {
        steps.push_back({{status, static_cast<uint8_t>(programFor(organ, n))}, organ.stopDelayMs / 1000.0});
    }
    return steps;
}

}  // namespace mc::organ
