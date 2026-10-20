#ifndef MIDICONTROL_DIAG_API_HPP
#define MIDICONTROL_DIAG_API_HPP

#include <functional>
#include <optional>
#include <string>

#include "midicontrol/diag_store.hpp"
#include "midicontrol/status.hpp"

namespace mc::diag {

// GET /api/diag?node=base|remote&level=E|W|I|D&before=<id>&limit=<n<=200>&ack=<id>
//   -> {"schema":1,"entries":[<entry>...],"next_before":<id|null>}
// Newest first; no node = both nodes merged. level = the least severe level shown (default D = all);
// limit defaults to 50 and is clamped to 1..200; next_before is the id to pass as `before` for the next
// (older) page, null when this page was the last. ack=<id> acknowledges every fault up to that id
// (`afterAck` runs then, to refresh the status registry). nullopt for an invalid parameter (HTTP 400).
std::optional<std::string> apiDiag(DiagStore& store, const std::string& query, const std::function<void()>& afterAck = {});

// Push the per-node last fault and unseen-fault count into the status registry (/api/status "last_fault",
// "unseen_faults", the health dot). node "base" -> Node::Base, "remote" -> Node::Controller.
void publishFaultStatus(const DiagStore& store, mc::status::StatusRegistry& status);

// Text of the `diag` console command: per node (base, remote) the last fault with its decoded bits and
// the last 20 stored lines, oldest first.
std::string formatDiag(const DiagStore& store);

}  // namespace mc::diag

#endif
