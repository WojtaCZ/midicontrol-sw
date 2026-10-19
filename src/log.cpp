#include "midicontrol/log.hpp"

#include <atomic>
#include <iostream>
#include <mutex>

namespace mc::log {

namespace {
std::atomic<mc::status::StatusRegistry*> g_registry{nullptr};
std::mutex g_consoleMutex;
}  // namespace

void setRegistry(mc::status::StatusRegistry* registry) { g_registry.store(registry); }

void write(mc::status::Node node, mc::status::Level level, const std::string& text) {
    using mc::status::Level;
    if (level != Level::Debug) {
        std::lock_guard<std::mutex> lk(g_consoleMutex);
        if (level == Level::Info) {
            std::cout << text << "\n" << std::flush;
        } else {
            std::cerr << (level == Level::Warn ? "warning: " : "error: ") << text << "\n";
        }
    }
    if (auto* r = g_registry.load()) r->event(node, level, text);
}

}  // namespace mc::log
