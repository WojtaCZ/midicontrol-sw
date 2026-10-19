#ifndef MIDICONTROL_WEB_PAGE_HPP
#define MIDICONTROL_WEB_PAGE_HPP

#include <string>

namespace mc::web {

// The status page: one self-contained HTML document (inline CSS and JS, no
// external requests) that polls GET /api/status once a second.
const std::string& statusPageHtml();

}  // namespace mc::web

#endif  // MIDICONTROL_WEB_PAGE_HPP
