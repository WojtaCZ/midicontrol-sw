#include "midicontrol/firmware_upload.hpp"

#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/status_json.hpp"

namespace mc::fwupload {

namespace {

const char* unitName(uint8_t board) {
    return board == protocol::ADDR_BASE ? "base" : board == protocol::ADDR_REMOTE ? "controller" : "unknown";
}

std::string versionText(uint32_t v) {
    return std::to_string(v >> 16) + "." + std::to_string((v >> 8) & 0xFF) + "." + std::to_string(v & 0xFF);
}

mc::http::Response json(int status, const std::string& body) { return {status, "application/json", body}; }

mc::http::Response refuse(int status, const std::string& why) {
    return json(status, "{\"error\":" + mc::status::jsonEscape(why) + "}");
}

// Why `file` is not an update file for `board`, in the words the page shows.
std::string problem(const std::vector<uint8_t>& file, uint8_t board, fwimage::Check c) {
    if (file.empty()) return "the file is empty";
    if (file.size() >= 4 && file[0] == 0x7F && file[1] == 'E' && file[2] == 'L' && file[3] == 'F')
        return "this is an ELF file; use the <name>-update.bin next to it in the firmware's build folder";
    if (file[0] == ':') return "this looks like an Intel hex file; use the <name>-update.bin from the firmware build";
    if (c == fwimage::Check::WrongBoard) {
        const uint8_t id = file[file.size() - fwimage::kTrailerSize + 12];  // trailer boardId
        if (id != protocol::ADDR_BASE && id != protocol::ADDR_REMOTE)
            return "this is built for an unknown unit (board ID " + std::to_string(id) + "); nothing was sent";
        const uint8_t other = board == protocol::ADDR_BASE ? protocol::ADDR_REMOTE : protocol::ADDR_BASE;
        if (fwimage::check(file, other) == fwimage::Check::Ok)
            return std::string("this is the ") + unitName(other) + " firmware, not the " + unitName(board) +
                   " firmware; nothing was sent";
    }
    return fwimage::describe(c);
}

}  // namespace

std::optional<uint8_t> boardFor(const std::string& target) {
    if (target == "base") return protocol::ADDR_BASE;
    if (target == "remote") return protocol::ADDR_REMOTE;
    return std::nullopt;
}

mc::http::Response handlePost(const std::string& target, const std::string& body, const Start& start) {
    const auto board = boardFor(target);
    if (!board) return mc::http::errorResponse(404);
    Accepted a;
    a.board = *board;
    a.image.assign(body.begin(), body.end());
    const auto c = fwimage::check(a.image, a.board, &a.info);
    if (c != fwimage::Check::Ok) return refuse(422, problem(a.image, a.board, c));
    const std::string version = versionText(a.info.version);
    const size_t size = a.image.size();
    if (auto why = start(std::move(a)); !why.empty()) return refuse(409, why);
    return json(202, "{\"target\":\"" + target + "\",\"version\":\"" + version + "\",\"size\":" +
                         std::to_string(size) + "}");
}

}  // namespace mc::fwupload
