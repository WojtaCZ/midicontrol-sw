#include <optional>
#include <string>
#include <vector>

#include "midicontrol/dispatch.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "test_framework.hpp"

using namespace mc;
using namespace mc::protocol;
using Bytes = std::vector<uint8_t>;

namespace {

struct Sent {
    uint8_t dest;
    MsgType type;
    Bytes payload;
    bool reliable;
};

struct FakeSink : CommandSink {
    std::vector<std::string> plays;
    int stops = 0;
    int addVerses = 0;
    bool playing = false;
    std::optional<NakReason> playResult;
    std::vector<size_t> listOffsets;

    std::optional<NakReason> playSong(const std::string& code, uint8_t) override {
        plays.push_back(code);
        return playResult;
    }
    void stopSong(uint8_t) override { ++stops; }
    std::optional<NakReason> addVerse() override {
        ++addVerses;
        return playing ? std::nullopt : std::optional<NakReason>(NakReason::NOT_PLAYING);
    }
    std::vector<uint8_t> songListPage(size_t offset) override {
        listOffsets.push_back(offset);
        return {0x00, '4', '0', '1'};
    }
};

struct Rig {
    FakeSink sink;
    std::vector<Sent> sent;
    std::vector<uint8_t> acked;
    std::vector<std::pair<uint8_t, uint8_t>> naked;
    int keepaliveAcks = 0;
    Dispatcher disp;

    Rig() : disp(sink, output()) {}

    Dispatcher::Output output() {
        Dispatcher::Output out;
        out.send = [this](uint8_t d, MsgType t, const Bytes& p) { sent.push_back({d, t, p, false}); };
        out.sendWithAck = [this](uint8_t d, MsgType t, const Bytes& p) {
            sent.push_back({d, t, p, true});
        };
        out.onAck = [this](uint8_t pid) { acked.push_back(pid); };
        out.onNak = [this](uint8_t pid, uint8_t r) { naked.emplace_back(pid, r); };
        out.onKeepaliveAck = [this]() { ++keepaliveAcks; };
        return out;
    }

    void rx(uint8_t src, uint8_t pid, MsgType type, Bytes payload = {}, uint8_t dest = ADDR_PC) {
        disp.handle(Packet(src, dest, pid, type, std::move(payload)));
    }
};

bool isAck(const Sent& s, uint8_t dest, uint8_t pid) {
    return s.type == MsgType::ACK && s.dest == dest && s.payload == Bytes{pid} && !s.reliable;
}

bool isNak(const Sent& s, uint8_t dest, uint8_t pid, NakReason r) {
    return s.type == MsgType::NAK && s.dest == dest &&
           s.payload == (Bytes{pid, static_cast<uint8_t>(r)});
}

}  // namespace

TEST(dispatch_play_song_acked_and_executed) {
    Rig r;
    r.rx(ADDR_REMOTE, 0x10, MsgType::PLAY_SONG, {'4', '0', '6', 'a'});
    CHECK(r.sink.plays == std::vector<std::string>{"406A"});
    CHECK_EQ(r.sent.size(), 1u);
    CHECK(isAck(r.sent[0], ADDR_REMOTE, 0x10));
}

TEST(dispatch_duplicate_is_reacked_not_reexecuted) {
    Rig r;
    r.rx(ADDR_REMOTE, 0x10, MsgType::PLAY_SONG, {'4', '0', '1'});
    r.rx(ADDR_REMOTE, 0x10, MsgType::PLAY_SONG, {'4', '0', '1'});  // retry, same id
    CHECK_EQ(r.sink.plays.size(), 1u);
    CHECK_EQ(r.sent.size(), 2u);
    CHECK(isAck(r.sent[1], ADDR_REMOTE, 0x10));

    r.rx(ADDR_REMOTE, 0x11, MsgType::PLAY_SONG, {'4', '0', '1'});  // new id
    CHECK_EQ(r.sink.plays.size(), 2u);

    // Same id from a different source is a different frame.
    r.rx(ADDR_BASE, 0x11, MsgType::STOP_SONG);
    CHECK_EQ(r.sink.stops, 1);

    // After reset (reconnect) the id is no longer remembered.
    r.disp.reset();
    r.rx(ADDR_REMOTE, 0x11, MsgType::PLAY_SONG, {'4', '0', '1'});
    CHECK_EQ(r.sink.plays.size(), 3u);
}

TEST(dispatch_duplicate_of_nak_repeats_nak) {
    Rig r;
    r.sink.playResult = NakReason::NOT_FOUND;
    r.rx(ADDR_REMOTE, 0x20, MsgType::PLAY_SONG, {'9', '9', '9'});
    r.rx(ADDR_REMOTE, 0x20, MsgType::PLAY_SONG, {'9', '9', '9'});
    CHECK_EQ(r.sink.plays.size(), 1u);
    CHECK_EQ(r.sent.size(), 2u);
    CHECK(isNak(r.sent[0], ADDR_REMOTE, 0x20, NakReason::NOT_FOUND));
    CHECK(isNak(r.sent[1], ADDR_REMOTE, 0x20, NakReason::NOT_FOUND));
}

TEST(dispatch_invalid_song_code_naks_invalid_payload) {
    Rig r;
    r.rx(ADDR_REMOTE, 1, MsgType::PLAY_SONG, {'.', '.', '/', 'x'});
    r.rx(ADDR_REMOTE, 2, MsgType::PLAY_SONG, {});
    CHECK(r.sink.plays.empty());
    CHECK(isNak(r.sent[0], ADDR_REMOTE, 1, NakReason::INVALID_PAYLOAD));
    CHECK(isNak(r.sent[1], ADDR_REMOTE, 2, NakReason::INVALID_PAYLOAD));
}

TEST(dispatch_add_verse_naks_when_idle) {
    Rig r;
    r.rx(ADDR_REMOTE, 5, MsgType::REPLAY_ANCHOR);
    CHECK(isNak(r.sent.back(), ADDR_REMOTE, 5, NakReason::NOT_PLAYING));
    r.sink.playing = true;
    r.rx(ADDR_REMOTE, 6, MsgType::REPLAY_ANCHOR);
    CHECK(isAck(r.sent.back(), ADDR_REMOTE, 6));
    CHECK_EQ(r.sink.addVerses, 2);
}

TEST(dispatch_stop_song_payload_forms) {
    Rig r;
    r.rx(ADDR_REMOTE, 1, MsgType::STOP_SONG);
    r.rx(ADDR_REMOTE, 2, MsgType::STOP_SONG, {0x00});
    r.rx(ADDR_REMOTE, 3, MsgType::STOP_SONG, {0x01});  // pause = stop
    CHECK_EQ(r.sink.stops, 3);
    r.rx(ADDR_REMOTE, 4, MsgType::STOP_SONG, {0x05});
    CHECK_EQ(r.sink.stops, 3);
    CHECK(isNak(r.sent.back(), ADDR_REMOTE, 4, NakReason::INVALID_PAYLOAD));
}

TEST(dispatch_song_list_replies_to_requester_after_ack) {
    Rig r;
    r.rx(ADDR_REMOTE, 9, MsgType::GET_SONG_LIST, {25});
    CHECK(r.sink.listOffsets == std::vector<size_t>{25});
    CHECK_EQ(r.sent.size(), 2u);
    CHECK(isAck(r.sent[0], ADDR_REMOTE, 9));
    CHECK(r.sent[1].type == MsgType::SONG_LIST_RESP);
    CHECK_EQ(r.sent[1].dest, ADDR_REMOTE);
    CHECK(r.sent[1].reliable);
    CHECK(r.sent[1].payload == (Bytes{0x00, '4', '0', '1'}));

    r.rx(ADDR_BASE, 9, MsgType::GET_SONG_LIST);  // default offset 0, to the base
    CHECK(r.sink.listOffsets == (std::vector<size_t>{25, 0}));
    CHECK_EQ(r.sent.back().dest, ADDR_BASE);

    r.rx(ADDR_BASE, 9, MsgType::GET_SONG_LIST);  // duplicate: ACK only, no second response
    CHECK_EQ(r.sink.listOffsets.size(), 2u);
    CHECK(isAck(r.sent.back(), ADDR_BASE, 9));
}

TEST(dispatch_unknown_and_unsupported_types_nak_unknown_msg) {
    Rig r;
    r.rx(ADDR_BASE, 1, static_cast<MsgType>(0x7F));
    CHECK(isNak(r.sent.back(), ADDR_BASE, 1, NakReason::UNKNOWN_MSG));
    r.rx(ADDR_REMOTE, 2, MsgType::SET_CURRENT, {0x01});  // a base command, not the PC's
    CHECK(isNak(r.sent.back(), ADDR_REMOTE, 2, NakReason::UNKNOWN_MSG));
}

TEST(dispatch_informational_messages_are_acked) {
    Rig r;
    r.rx(ADDR_BASE, 3, MsgType::STATUS_RESP, {1, 0, 1, 1});
    CHECK(isAck(r.sent.back(), ADDR_BASE, 3));
    r.rx(ADDR_REMOTE, 4, MsgType::SIGNAL_ANCHOR, {'x', 0});
    CHECK(isAck(r.sent.back(), ADDR_REMOTE, 4));
}

TEST(dispatch_no_reply_for_ack_nak_keepalive_npi) {
    Rig r;
    r.rx(ADDR_BASE, 1, MsgType::ACK, {0x42});
    r.rx(ADDR_BASE, 2, MsgType::NAK, {0x43, 0x06});
    r.rx(ADDR_BASE, 3, MsgType::KEEPALIVE_ACK);
    r.rx(ADDR_BASE, 4, MsgType::NOW_PLAYING_INFO, {});
    CHECK(r.sent.empty());
    CHECK(r.acked == std::vector<uint8_t>{0x42});
    CHECK_EQ(r.naked.size(), 1u);
    CHECK_EQ(r.naked[0].first, 0x43);
    CHECK_EQ(r.naked[0].second, 0x06);
    CHECK_EQ(r.keepaliveAcks, 1);

    r.rx(ADDR_BASE, 5, MsgType::KEEPALIVE);  // answered with KEEPALIVE_ACK, not ACK
    CHECK_EQ(r.sent.size(), 1u);
    CHECK(r.sent[0].type == MsgType::KEEPALIVE_ACK);
    CHECK(!Dispatcher::needsAck(MsgType::NOW_PLAYING_INFO));
    CHECK(Dispatcher::needsAck(MsgType::REPLAY_ANCHOR));
}

TEST(dispatch_ignores_other_destinations_and_never_answers_broadcast) {
    Rig r;
    r.rx(ADDR_REMOTE, 1, MsgType::PLAY_SONG, {'4', '0', '1'}, ADDR_BASE);
    CHECK(r.sink.plays.empty());
    CHECK(r.sent.empty());

    r.rx(ADDR_REMOTE, 2, MsgType::STOP_SONG, {}, ADDR_BROADCAST);
    CHECK_EQ(r.sink.stops, 1);
    CHECK(r.sent.empty());
}
