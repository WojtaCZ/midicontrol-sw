#ifndef MIDICONTROL_LOG_HPP
#define MIDICONTROL_LOG_HPP

#include <string>

#include "midicontrol/status.hpp"

// Process-wide logger: a message goes to the console (and from there to the
// journal) and, when a registry is set, into that node's recent events.
//   DEBUG  registry only (packet traffic)
//   INFO   stdout
//   WARN   stderr, "warning: " prefix
//   ERROR  stderr, "error: " prefix
// Interactive CLI output (prompt, list replies) does not go through here.
namespace mc::log {

void setRegistry(mc::status::StatusRegistry* registry);  // nullptr = console only

void write(mc::status::Node node, mc::status::Level level, const std::string& text);

inline void debug(mc::status::Node n, const std::string& t) { write(n, mc::status::Level::Debug, t); }
inline void info(mc::status::Node n, const std::string& t) { write(n, mc::status::Level::Info, t); }
inline void warn(mc::status::Node n, const std::string& t) { write(n, mc::status::Level::Warn, t); }
inline void error(mc::status::Node n, const std::string& t) { write(n, mc::status::Level::Error, t); }

}  // namespace mc::log

#endif  // MIDICONTROL_LOG_HPP
