#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gtultra2raster {

constexpr uint8_t REPEAT = 0xd0;
constexpr uint8_t TRANSDOWN = 0xe0;
constexpr uint8_t TRANSUP = 0xf0;
constexpr uint8_t LOOPSONG = 0xff;

constexpr uint8_t GT_NOTE = 0x60;
constexpr uint8_t GT_FIRST_PACKED_REST = 0xc0;
constexpr uint8_t GT_REST = 0xbd;
constexpr uint8_t GT_KEYOFF = 0xbe;
constexpr uint8_t GT_KEYON = 0xbf;
constexpr uint8_t GT_ENDPATT = 0xff;

constexpr uint8_t CMD_DONOTHING = 0x00;
constexpr uint8_t CMD_PORTAUP = 0x01;
constexpr uint8_t CMD_PORTADOWN = 0x02;
constexpr uint8_t CMD_TONEPORTA = 0x03;
constexpr uint8_t CMD_VIBRATO = 0x04;
constexpr uint8_t CMD_SETAD = 0x05;
constexpr uint8_t CMD_SETSR = 0x06;
constexpr uint8_t CMD_SETWAVE = 0x07;
constexpr uint8_t CMD_SETWAVEPTR = 0x08;
constexpr uint8_t CMD_SETPULSEPTR = 0x09;
constexpr uint8_t CMD_SETFILTERPTR = 0x0a;
constexpr uint8_t CMD_SETFILTERCTRL = 0x0b;
constexpr uint8_t CMD_SETFILTERCUTOFF = 0x0c;
constexpr uint8_t CMD_SETMASTERVOL = 0x0d;
constexpr uint8_t CMD_FUNKTEMPO = 0x0e;
constexpr uint8_t CMD_SETTEMPO = 0x0f;

constexpr int WTBL = 0;
constexpr int PTBL = 1;
constexpr int FTBL = 2;
constexpr int STBL = 3;

struct OrderList {
    std::vector<uint8_t> data;
    uint8_t restart = 0;
};

struct Instrument {
    uint8_t ad = 0;
    uint8_t sr = 0;
    std::array<uint8_t, 4> ptr{0, 0, 0, 0};
    uint8_t vibdelay = 0;
    uint8_t gatetimer = 0;
    uint8_t firstwave = 0;
    uint8_t pan = 0;
    std::string name;
};

struct Table {
    std::vector<uint8_t> left;
    std::vector<uint8_t> right;
};

struct PatternRow {
    uint8_t note = GT_REST;
    uint8_t instrument = 0;
    uint8_t command = 0;
    uint8_t parameter = 0;
};

struct Pattern {
    std::vector<PatternRow> rows;
};

struct ExtraInfo {
    bool present = false;
    bool ntsc = false;
    uint32_t maxSidChannels = 0;
    uint32_t multiplier = 0;
    uint32_t hardRestart = 0;
};

struct Song {
    std::string tag;
    std::string title;
    std::string author;
    std::string copyright;
    int subtuneCount = 0;
    int channelCount = 3;
    std::vector<std::vector<OrderList>> orders;
    std::vector<Instrument> instruments;
    std::array<Table, 4> tables;
    std::vector<Pattern> patterns;
    ExtraInfo extra;
};

struct ExpandedPatternRef {
    int pattern = 0;
    int transpose = 0;
};

struct SourceRow {
    PatternRow row;
    int transpose = 0;
    int pattern = -1;
    int patternRow = -1;
    int expandedRow = -1;
    int sourceChannel = -1;
};

struct Reader {
    std::vector<uint8_t> bytes;
    size_t pos = 0;

    explicit Reader(std::vector<uint8_t> data) : bytes(std::move(data)) {}

    uint8_t read8(const std::string& what) {
        if (pos >= bytes.size()) {
            throw std::runtime_error("unexpected EOF while reading " + what);
        }
        return bytes[pos++];
    }

    uint32_t readLe32(const std::string& what) {
        uint32_t v = read8(what);
        v |= uint32_t(read8(what)) << 8;
        v |= uint32_t(read8(what)) << 16;
        v |= uint32_t(read8(what)) << 24;
        return v;
    }

    std::vector<uint8_t> readBytes(size_t count, const std::string& what) {
        if (pos + count > bytes.size()) {
            throw std::runtime_error("unexpected EOF while reading " + what);
        }
        std::vector<uint8_t> out(bytes.begin() + static_cast<long>(pos), bytes.begin() + static_cast<long>(pos + count));
        pos += count;
        return out;
    }
};

inline std::vector<uint8_t> readWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open input file: " + path);
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::string trimCString(const std::vector<uint8_t>& bytes) {
    size_t end = 0;
    while (end < bytes.size() && bytes[end] != 0) {
        ++end;
    }
    while (end > 0 && std::isspace(static_cast<unsigned char>(bytes[end - 1]))) {
        --end;
    }
    return std::string(bytes.begin(), bytes.begin() + static_cast<long>(end));
}

inline bool validatesAsSixChannel(const std::vector<uint8_t>& bytes) {
    size_t pos = 4 + 32 + 32 + 32;
    if (pos >= bytes.size()) {
        return false;
    }
    uint8_t songs = bytes[pos++];
    for (uint8_t s = 0; s < songs; ++s) {
        for (int c = 0; c < 6; ++c) {
            if (pos >= bytes.size()) {
                return false;
            }
            size_t loadSize = size_t(bytes[pos++]) + 1;
            if (loadSize < 2 || pos + loadSize > bytes.size()) {
                return false;
            }
            if (bytes[pos + loadSize - 2] != LOOPSONG || bytes[pos + loadSize - 1] >= loadSize) {
                return false;
            }
            pos += loadSize;
        }
    }
    return true;
}

inline Song parseSong(const std::string& path) {
    std::vector<uint8_t> bytes = readWholeFile(path);
    Reader r(bytes);
    Song song;

    std::vector<uint8_t> tag = r.readBytes(4, "tag");
    song.tag.assign(tag.begin(), tag.end());
    if (song.tag != "GTS3" && song.tag != "GTS4" && song.tag != "GTS5") {
        throw std::runtime_error("unsupported input tag '" + song.tag + "'; expected GTS3/GTS4/GTS5");
    }

    song.title = trimCString(r.readBytes(32, "song title"));
    song.author = trimCString(r.readBytes(32, "author"));
    song.copyright = trimCString(r.readBytes(32, "copyright"));
    song.subtuneCount = r.read8("subtune count");
    if (song.subtuneCount <= 0) {
        throw std::runtime_error("input contains no subtunes");
    }

    song.channelCount = validatesAsSixChannel(bytes) ? 6 : 3;
    song.orders.assign(song.subtuneCount, std::vector<OrderList>(song.channelCount));
    for (int s = 0; s < song.subtuneCount; ++s) {
        for (int c = 0; c < song.channelCount; ++c) {
            uint8_t len = r.read8("orderlist length");
            song.orders[s][c].data = r.readBytes(len, "orderlist data");
            song.orders[s][c].restart = r.read8("orderlist restart");
        }
    }

    uint8_t instrumentCount = r.read8("instrument count");
    song.instruments.assign(size_t(instrumentCount) + 1, Instrument{});
    for (int i = 1; i <= instrumentCount; ++i) {
        Instrument inst;
        inst.ad = r.read8("instrument AD");
        inst.sr = r.read8("instrument SR");
        for (int t = 0; t < 4; ++t) {
            inst.ptr[t] = r.read8("instrument table pointer");
        }
        inst.vibdelay = r.read8("instrument vibrato delay");
        inst.gatetimer = r.read8("instrument gate timer");
        inst.firstwave = r.read8("instrument first wave");
        inst.name = trimCString(r.readBytes(16, "instrument name"));
        song.instruments[i] = inst;
    }

    for (int t = 0; t < 4; ++t) {
        uint8_t len = r.read8("table length");
        song.tables[t].left = r.readBytes(len, "table left");
        song.tables[t].right = r.readBytes(len, "table right");
    }

    uint8_t patternCount = r.read8("pattern count");
    song.patterns.resize(patternCount);
    for (int p = 0; p < patternCount; ++p) {
        uint8_t rows = r.read8("pattern rows");
        std::vector<PatternRow> parsed;
        parsed.reserve(rows);
        for (int i = 0; i < rows; ++i) {
            PatternRow row;
            row.note = r.read8("pattern note");
            row.instrument = r.read8("pattern instrument");
            row.command = r.read8("pattern command");
            row.parameter = r.read8("pattern parameter");
            parsed.push_back(row);
        }
        if (!parsed.empty() && parsed.back().note == GT_ENDPATT) {
            parsed.pop_back();
        }
        song.patterns[p].rows = std::move(parsed);
    }

    while (r.pos < r.bytes.size()) {
        uint8_t id = r.read8("GTUltra chunk id");
        if (id == 0x1f && r.pos + 24 <= r.bytes.size()) {
            song.extra.present = true;
            (void)r.read8("fine vibrato");
            (void)r.read8("optimize pulse");
            (void)r.read8("optimize realtime");
            song.extra.ntsc = r.read8("ntsc") != 0;
            (void)r.read8("sid model");
            song.extra.hardRestart = r.readLe32("hard restart");
            song.extra.multiplier = r.readLe32("speed multiplier");
            song.extra.maxSidChannels = r.readLe32("max SID channels");
            (void)r.read8("stereo mode");
        } else if (id == 0x9a && r.pos + instrumentCount <= r.bytes.size()) {
            for (int i = 1; i <= instrumentCount; ++i) {
                song.instruments[i].pan = r.read8("instrument pan");
            }
        } else if (id == 0x9b && r.pos < r.bytes.size()) {
            (void)r.read8("SIDTracker64 mode");
        } else {
            break;
        }
    }

    return song;
}

inline std::vector<ExpandedPatternRef> expandOrder(const OrderList& order, size_t maxEntries) {
    std::vector<ExpandedPatternRef> base;
    int transpose = 0;
    int repeatCount = 1;
    int expandedLoopPos = -1;

    for (size_t i = 0; i < order.data.size();) {
        if (i == order.restart) {
            expandedLoopPos = static_cast<int>(base.size());
        }
        uint8_t v = order.data[i++];
        if (v == LOOPSONG) {
            break;
        }
        if (v >= TRANSDOWN) {
            if (v < TRANSUP) {
                transpose = -(16 - (v & 0x0f));
            } else {
                transpose = v - TRANSUP;
            }
            continue;
        }
        if (v >= REPEAT) {
            repeatCount = (v - REPEAT) + 1;
            continue;
        }
        for (int r = 0; r < repeatCount; ++r) {
            base.push_back({v, transpose});
        }
        repeatCount = 1;
    }

    std::vector<ExpandedPatternRef> out = base;
    if (order.restart != 0xfd && expandedLoopPos >= 0 && expandedLoopPos < static_cast<int>(base.size())) {
        while (out.size() < maxEntries) {
            for (size_t i = static_cast<size_t>(expandedLoopPos); i < base.size() && out.size() < maxEntries; ++i) {
                out.push_back(base[i]);
            }
            if (base.empty()) {
                break;
            }
        }
    }
    return out;
}

inline std::vector<SourceRow> expandRowsForChannel(const Song& song, int subtune, int channel, size_t maxRows) {
    std::vector<SourceRow> rows;
    std::vector<ExpandedPatternRef> refs = expandOrder(song.orders[subtune][channel], maxRows);
    for (const ExpandedPatternRef& ref : refs) {
        if (ref.pattern < 0 || ref.pattern >= static_cast<int>(song.patterns.size())) {
            continue;
        }
        int patternRow = 0;
        for (const PatternRow& row : song.patterns[ref.pattern].rows) {
            if (rows.size() >= maxRows) {
                return rows;
            }
            rows.push_back({row, ref.transpose, ref.pattern, patternRow, static_cast<int>(rows.size()), channel});
            ++patternRow;
        }
    }
    return rows;
}

inline bool patternContainsData(const Song& song, int patternIndex) {
    if (patternIndex < 0 || patternIndex >= static_cast<int>(song.patterns.size())) {
        return false;
    }
    for (const PatternRow& row : song.patterns[patternIndex].rows) {
        if (row.note != GT_REST || row.instrument != 0 || row.command != 0 || row.parameter != 0) {
            return true;
        }
    }
    return false;
}

inline int countIgnoredHighSidChannels(const Song& song, int subtune) {
    if (song.channelCount <= 3) {
        return 0;
    }
    int count = 0;
    for (int c = 3; c < song.channelCount; ++c) {
        std::vector<ExpandedPatternRef> refs = expandOrder(song.orders[subtune][c], 64);
        for (const auto& ref : refs) {
            if (patternContainsData(song, ref.pattern)) {
                count++;
                break;
            }
        }
    }
    return count;
}

} // namespace gtultra2raster
