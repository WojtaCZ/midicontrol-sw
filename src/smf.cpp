#include "midicontrol/smf.hpp"

#include <algorithm>
#include <cstdio>

#include "midicontrol/sysex.hpp"

namespace mc::smf {

namespace {

// A bounds-checked cursor over the file buffer.
struct Reader {
    const uint8_t* p;
    const uint8_t* end;

    bool atEnd() const { return p >= end; }
    size_t remaining() const { return static_cast<size_t>(end - p); }

    uint8_t u8() {
        if (p >= end) throw ParseError("unexpected end of data");
        return *p++;
    }

    uint32_t u16() {
        uint32_t hi = u8();
        uint32_t lo = u8();
        return (hi << 8) | lo;
    }

    uint32_t u32() {
        uint32_t b0 = u8(), b1 = u8(), b2 = u8(), b3 = u8();
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }

    // Variable-length quantity (7 bits per byte, MSB = continuation).
    uint32_t vlq() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t b = u8();
            value = (value << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) return value;
        }
        throw ParseError("VLQ too long");
    }
};

// Number of data bytes following a channel-voice status byte.
int channelDataLen(uint8_t status) {
    switch (status & 0xF0) {
        case 0xC0:  // program change
        case 0xD0:  // channel pressure
            return 1;
        default:
            return 2;
    }
}

}  // namespace

MidiFile parse(const std::vector<uint8_t>& data) {
    Reader r{data.data(), data.data() + data.size()};

    // ---- Header chunk ----
    if (r.remaining() < 14) throw ParseError("file too small for MThd");
    if (!(data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd')) {
        throw ParseError("missing MThd header");
    }
    r.p += 4;
    uint32_t headerLen = r.u32();
    MidiFile mf;
    mf.format = static_cast<uint16_t>(r.u16());
    uint16_t ntracks = static_cast<uint16_t>(r.u16());
    mf.division = static_cast<uint16_t>(r.u16());
    // Skip any extra header bytes beyond the standard 6.
    if (headerLen > 6) {
        if (r.remaining() < headerLen - 6) throw ParseError("truncated header");
        r.p += (headerLen - 6);
    }

    // ---- Track chunks ----
    for (uint16_t t = 0; t < ntracks && !r.atEnd(); ++t) {
        if (r.remaining() < 8) break;
        bool isTrack = (r.p[0] == 'M' && r.p[1] == 'T' && r.p[2] == 'r' && r.p[3] == 'k');
        r.p += 4;
        uint32_t trackLen = r.u32();
        if (!isTrack) {  // unknown chunk type: skip it
            if (r.remaining() < trackLen) throw ParseError("truncated unknown chunk");
            r.p += trackLen;
            continue;
        }
        if (r.remaining() < trackLen) throw ParseError("truncated track");

        const uint8_t* trackEnd = r.p + trackLen;
        Reader tr{r.p, trackEnd};
        uint32_t absTick = 0;
        uint8_t runningStatus = 0;

        while (tr.p < trackEnd) {
            absTick += tr.vlq();
            uint8_t status = tr.u8();

            if (status == 0xFF) {  // meta event
                uint8_t metaType = tr.u8();
                uint32_t len = tr.vlq();
                if (tr.remaining() < len) throw ParseError("truncated meta event");
                Event ev;
                ev.tick = absTick;
                ev.kind = EventKind::Meta;
                ev.metaType = metaType;
                ev.bytes.assign(tr.p, tr.p + len);
                tr.p += len;
                mf.events.push_back(std::move(ev));
                runningStatus = 0;  // meta cancels running status
            } else if (status == 0xF0 || status == 0xF7) {
                // SysEx: scan to the terminating F7 (these files omit the SMF
                // length prefix and embed a raw F0 .. F7 run).
                Event ev;
                ev.tick = absTick;
                ev.kind = EventKind::SysEx;
                while (tr.p < trackEnd) {
                    uint8_t b = *tr.p++;
                    if (b == 0xF7) break;
                    ev.bytes.push_back(b);
                }
                mf.events.push_back(std::move(ev));
                runningStatus = 0;  // sysex cancels running status
            } else {
                // Channel voice message, possibly using running status.
                uint8_t actualStatus;
                const uint8_t* dataStart;
                if (status & 0x80) {
                    actualStatus = status;
                    runningStatus = status;
                    dataStart = tr.p;
                } else {
                    if (runningStatus == 0) throw ParseError("running status with no prior status");
                    actualStatus = runningStatus;
                    --tr.p;  // the byte we read is actually the first data byte
                    dataStart = tr.p;
                }
                int nData = channelDataLen(actualStatus);
                if (tr.p + nData > trackEnd) throw ParseError("truncated channel event");
                tr.p += nData;

                Event ev;
                ev.tick = absTick;
                ev.kind = EventKind::Channel;
                ev.bytes.push_back(actualStatus);
                ev.bytes.insert(ev.bytes.end(), dataStart, dataStart + nData);
                mf.events.push_back(std::move(ev));
            }
        }

        r.p = trackEnd;
    }

    // ---- Merge: stable-sort by absolute tick (preserves intra-tick order) ----
    std::stable_sort(mf.events.begin(), mf.events.end(),
                     [](const Event& a, const Event& b) { return a.tick < b.tick; });

    // ---- Resolve seconds from the tempo map ----
    const bool smpte = (mf.division & 0x8000) != 0;
    double ticksPerSecond = 0.0;  // used only for SMPTE
    if (smpte) {
        int fps = -static_cast<int8_t>(mf.division >> 8);
        int tpf = mf.division & 0xFF;
        ticksPerSecond = static_cast<double>(fps) * tpf;
        if (ticksPerSecond <= 0) ticksPerSecond = 1.0;
    }

    uint32_t lastTick = 0;
    double lastSeconds = 0.0;
    double usPerQuarter = 500000.0;  // default 120 BPM
    const double ppq = mf.division ? mf.division : 480;

    for (auto& ev : mf.events) {
        uint32_t dt = ev.tick - lastTick;
        double dsec;
        if (smpte) {
            dsec = dt / ticksPerSecond;
        } else {
            dsec = (dt / ppq) * (usPerQuarter / 1e6);
        }
        ev.seconds = lastSeconds + dsec;
        lastSeconds = ev.seconds;
        lastTick = ev.tick;

        if (ev.kind == EventKind::Meta && ev.metaType == 0x51 && ev.bytes.size() == 3) {
            usPerQuarter =
                static_cast<double>((ev.bytes[0] << 16) | (ev.bytes[1] << 8) | ev.bytes[2]);
        }
    }

    return mf;
}

MidiFile load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open file: " + path);
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < 0) {
        std::fclose(f);
        throw std::runtime_error("cannot determine size of: " + path);
    }
    std::vector<uint8_t> data(static_cast<size_t>(size));
    size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    data.resize(got);
    return parse(data);
}

std::vector<uint8_t> MidiFile::firstDisplaySysEx() const {
    for (const auto& ev : events) {
        if (ev.kind != EventKind::SysEx) continue;
        auto info = mc::sysex::parse(ev.bytes);
        if (info.has_value()) return ev.bytes;
    }
    return {};
}

}  // namespace mc::smf
