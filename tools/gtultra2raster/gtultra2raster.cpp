// GTUltra/GoatTracker .sng to Hermit's 1raster tracker converter.
//
// This tool targets the 1raster player data model. It preserves each GTUltra
// feature that can be represented there and reports the remaining losses with
// source locations, because 1raster is much smaller than the GT playroutine.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "gtultra_sng.h"
#include "raster_assets.h"

namespace {

using namespace gtultra2raster;

struct Options {
    std::string inputPath;
    std::string outputBase;
    int subtune = 0;
    std::string format = "both";
    std::string asmMode = "none";
    std::string asmFormat = "auto";
    bool analyzeOnly = false;
};

struct RasterPattern {
    std::vector<uint8_t> ch1Lo;
    std::vector<uint8_t> ch1Hi;
    std::vector<uint8_t> ch2Lo;
    std::vector<uint8_t> ch2Hi;
    std::vector<uint8_t> ch3Program;
};

struct PitchPattern {
    std::array<uint8_t, ORB_PATTERN_LENGTH> lo{};
    std::array<uint8_t, ORB_PATTERN_LENGTH> hi{};

    bool operator==(const PitchPattern& other) const {
        return lo == other.lo && hi == other.hi;
    }
};

struct ProgramRow {
    uint8_t c1 = 0;
    uint8_t c2 = 0;
    uint8_t c3 = 0xff;

    bool operator<(const ProgramRow& other) const {
        return std::tie(c1, c2, c3) < std::tie(other.c1, other.c2, other.c3);
    }
};

struct PulseInit {
    bool present = false;
    uint8_t lo = 0;
    uint8_t hi = 0;
};

struct FilterInit {
    bool present = false;
    bool hasCutoff = false;
    bool hasControl = false;
    uint8_t cutoff = 0;
    uint8_t control = 0;
    uint8_t bandVolume = 0x0f;
};

struct ProgramTable {
    std::array<uint8_t, 256> c1{};
    std::array<uint8_t, 256> c2{};
    std::array<uint8_t, 256> c3{};
    uint8_t next = 1;
    std::map<std::vector<ProgramRow>, uint8_t> cache;

    ProgramTable() {
        c1.fill(0);
        c2.fill(0);
        c3.fill(0xff);
    }
};

struct ConvertStats {
    int unsupportedEvents = 0;
    int clampedNotes = 0;
    int tablePrograms = 0;
    int tableBytesUsed = 0;
    std::map<std::string, int> warningCounts;
    std::map<std::string, std::vector<std::string>> warningExamples;
    std::set<std::string> warningKeys;
};

struct TargetResult {
    bool ok = false;
    std::string formatName;
    std::string error;
    std::vector<uint8_t> image;
    std::array<std::vector<uint8_t>, 3> sequences;
    std::array<int, 3> sourceChannels{{0, 1, 2}};
    bool hasChannelMap = false;
    bool pitchSidAssist = true;
    std::vector<RasterPattern> patterns;
    ProgramTable programTable;
    ConvertStats stats;
};

struct TargetSpec {
    std::string name;
    bool orb = false;
    size_t patternLength = 32;
    size_t maxPatterns = 8;
};

struct SourceChannelCandidate {
    int sourceChannel = 0;
    std::vector<SourceRow> rows;
    int noteRows = 0;
    int keyRows = 0;
    int instrumentRows = 0;
    int commandRows = 0;
    int pitchLossCost = 0;
    int programLossCost = 0;
    int ignoredCost = 0;
    int programRoleValue = 0;
    int pitchUniquePatterns = 0;
    bool hasData = false;
};

struct ChannelAssignment {
    std::array<int, 3> sourceChannels{{0, 1, 2}};
    long long score = 0;
};

std::string upperAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string normalizeAsmMode(const std::string& mode) {
    std::string value = lowerAscii(mode);
    if (value == "none" || value == "off" || value == "no") {
        return "none";
    }
    if (value == "player" || value == "standalone" || value == "with-player" || value == "player+data") {
        return "player";
    }
    if (value == "data" || value == "data-only" || value == "without-player" || value == "no-player") {
        return "data";
    }
    throw std::runtime_error("--asm-mode must be none, player, or data");
}

std::string normalizeAsmFormat(const std::string& format) {
    std::string value = lowerAscii(format);
    if (value != "auto" && value != "orm" && value != "orb") {
        throw std::runtime_error("--asm-format must be auto, orm, or orb");
    }
    return value;
}

std::string extensionOf(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return "";
    }
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string stripKnownExtension(std::string path) {
    std::string ext = extensionOf(path);
    if (ext == ".orm" || ext == ".orb" || ext == ".asm" || ext == ".prg") {
        path.resize(path.size() - ext.size());
    }
    return path;
}

std::string hexByte(uint8_t value) {
    std::ostringstream ss;
    ss << "$" << std::hex << std::setw(2) << std::setfill('0') << int(value);
    return ss.str();
}

std::string commandName(uint8_t command) {
    switch (command) {
        case CMD_DONOTHING: return "0XY/no command";
        case CMD_PORTAUP: return "1XY porta up";
        case CMD_PORTADOWN: return "2XY porta down";
        case CMD_TONEPORTA: return "3XY tone portamento";
        case CMD_VIBRATO: return "4XY vibrato";
        case CMD_SETAD: return "5XY set AD";
        case CMD_SETSR: return "6XY set SR";
        case CMD_SETWAVE: return "7XY set waveform";
        case CMD_SETWAVEPTR: return "8XY set wavetable pointer";
        case CMD_SETPULSEPTR: return "9XY set pulsetable pointer";
        case CMD_SETFILTERPTR: return "AXY set filtertable pointer";
        case CMD_SETFILTERCTRL: return "BXY set filter control";
        case CMD_SETFILTERCUTOFF: return "CXY set filter cutoff";
        case CMD_SETMASTERVOL: return "DXY set master volume";
        case CMD_FUNKTEMPO: return "EXY funktempo";
        case CMD_SETTEMPO: return "FXY set tempo";
        default: break;
    }
    return "unknown command " + hexByte(command);
}

std::string sourceLocation(int channel, const SourceRow& src) {
    std::ostringstream ss;
    if (src.sourceChannel >= 0) {
        ss << "source channel " << (src.sourceChannel + 1);
        if (channel >= 0) {
            ss << " -> 1raster channel " << (channel + 1);
        }
    } else if (channel >= 0) {
        ss << "1raster channel " << (channel + 1);
    } else {
        ss << "source channel ?";
    }
    if (src.pattern >= 0) {
        ss << ", pattern " << hexByte(static_cast<uint8_t>(src.pattern));
    }
    if (src.patternRow >= 0) {
        ss << ", row " << src.patternRow;
    }
    if (src.expandedRow >= 0) {
        ss << ", expanded row " << src.expandedRow;
    }
    return ss.str();
}

std::string instrumentLabel(const Song& song, int instIndex) {
    std::ostringstream ss;
    ss << "instrument " << instIndex;
    if (instIndex > 0 && instIndex < static_cast<int>(song.instruments.size()) && !song.instruments[instIndex].name.empty()) {
        ss << " \"" << song.instruments[instIndex].name << "\"";
    }
    return ss.str();
}

void addWarning(ConvertStats& stats, const std::string& message, const std::string& example = "", bool unsupported = true) {
    stats.warningCounts[message]++;
    if (unsupported) {
        stats.unsupportedEvents++;
    }
    if (!example.empty()) {
        std::vector<std::string>& examples = stats.warningExamples[message];
        if (examples.size() < 6 && std::find(examples.begin(), examples.end(), example) == examples.end()) {
            examples.push_back(example);
        }
    }
}

void addWarningOnce(ConvertStats& stats, const std::string& key, const std::string& message,
                    const std::string& example = "", bool unsupported = true) {
    if (!stats.warningKeys.insert(key).second) {
        return;
    }
    addWarning(stats, message, example, unsupported);
}

uint8_t gtTempoToPlayerSpeed(uint8_t parameter) {
    uint8_t tempo = parameter & 0x7f;
    if (tempo >= 3) {
        --tempo;
    }
    return tempo;
}

uint8_t defaultGtPlayerSpeed(const Song& song) {
    uint32_t multiplier = song.extra.multiplier ? song.extra.multiplier : 1;
    uint32_t tempo = multiplier * 6;
    if (tempo > 0) {
        --tempo;
    }
    return static_cast<uint8_t>(std::min<uint32_t>(tempo, 0x7f));
}

bool isValidNote(uint8_t note) {
    return note >= GT_NOTE && note < GT_REST;
}

bool isKeyEvent(uint8_t note) {
    return note == GT_KEYOFF || note == GT_KEYON;
}

bool rowHasData(const PatternRow& row) {
    return isValidNote(row.note) || isKeyEvent(row.note) || row.instrument != 0 || row.command != 0 || row.parameter != 0;
}

int gtNoteToRaster(uint8_t note, int transpose, ConvertStats& stats) {
    int out = int(note) - int(GT_NOTE) + 1 + transpose;
    if (out < 1) {
        out = 1;
        stats.clampedNotes++;
    } else if (out > 96) {
        out = 96;
        stats.clampedNotes++;
    }
    return out;
}

int programTickBudget(uint8_t playerSpeed) {
    return std::max(1, int(playerSpeed));
}

void writeWholeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot write output file: " + path);
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

PulseInit firstPulseFromInstrument(const Song& song, int instIndex);

uint8_t firstWaveFromInstrument(const Song& song, int instIndex) {
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return 0x41;
    }
    const Instrument& inst = song.instruments[instIndex];
    uint8_t ptr = inst.ptr[WTBL];
    if (ptr > 0) {
        size_t idx = size_t(ptr - 1);
        for (int guard = 0; idx < song.tables[WTBL].left.size() && guard < 16; ++idx, ++guard) {
            uint8_t left = song.tables[WTBL].left[idx];
            if (left == 0xff) {
                break;
            }
            if (left >= 0x10 && left < 0xe0) {
                return left;
            }
            if (left >= 0xe0 && left < 0xf0) {
                return static_cast<uint8_t>(left - 0xe0);
            }
        }
    }
    if (inst.firstwave != 0) {
        return inst.firstwave;
    }
    return 0x41;
}

uint8_t firstPulseHighFromInstrument(const Song& song, int instIndex, uint8_t fallback) {
    PulseInit pulse = firstPulseFromInstrument(song, instIndex);
    return pulse.present ? pulse.hi : fallback;
}

PulseInit firstPulseFromInstrument(const Song& song, int instIndex) {
    PulseInit pulse;
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return pulse;
    }
    uint8_t ptr = song.instruments[instIndex].ptr[PTBL];
    if (ptr == 0) {
        return pulse;
    }
    size_t idx = size_t(ptr - 1);
    for (int guard = 0; idx < song.tables[PTBL].left.size() && guard < 16; ++idx, ++guard) {
        uint8_t left = song.tables[PTBL].left[idx];
        if (left == 0xff) {
            break;
        }
        if (left >= 0x80) {
            pulse.present = true;
            pulse.hi = left & 0x0f;
            pulse.lo = song.tables[PTBL].right[idx];
            return pulse;
        }
    }
    return pulse;
}

FilterInit firstFilterFromInstrument(const Song& song, int instIndex, uint8_t volume = 0x0f) {
    FilterInit filter;
    filter.bandVolume = volume & 0x0f;
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return filter;
    }
    uint8_t ptr = song.instruments[instIndex].ptr[FTBL];
    if (ptr == 0) {
        return filter;
    }
    size_t idx = size_t(ptr - 1);
    for (int guard = 0; idx < song.tables[FTBL].left.size() && guard < 8; ++idx, ++guard) {
        uint8_t left = song.tables[FTBL].left[idx];
        uint8_t right = song.tables[FTBL].right[idx];
        if (left == 0xff) {
            break;
        }
        if (left >= 0x80) {
            filter.present = true;
            filter.hasControl = true;
            filter.control = right;
            filter.bandVolume = static_cast<uint8_t>((left & 0x70) | (volume & 0x0f));
            if (idx + 1 < song.tables[FTBL].left.size() && song.tables[FTBL].left[idx + 1] == 0x00) {
                filter.hasCutoff = true;
                filter.cutoff = song.tables[FTBL].right[idx + 1];
            }
            return filter;
        }
        if (left == 0x00) {
            filter.present = true;
            filter.hasCutoff = true;
            filter.cutoff = right;
            return filter;
        }
        break;
    }
    return filter;
}

int mostUsedInstrument(const std::vector<SourceRow>& rows) {
    std::map<int, int> counts;
    int current = 0;
    for (const SourceRow& src : rows) {
        if (src.row.instrument > 0) {
            current = src.row.instrument;
        }
        if (current > 0 && isValidNote(src.row.note)) {
            counts[current]++;
        }
    }
    int best = 0;
    int bestCount = 0;
    for (const auto& [inst, count] : counts) {
        if (count > bestCount) {
            best = inst;
            bestCount = count;
        }
    }
    return best;
}

uint8_t commonSr(const Song& song, int instIndex, uint8_t fallback) {
    if (instIndex > 0 && instIndex < static_cast<int>(song.instruments.size()) && song.instruments[instIndex].sr != 0) {
        return song.instruments[instIndex].sr;
    }
    return fallback;
}

uint8_t commonAd(const Song& song, int instIndex, uint8_t fallback) {
    if (instIndex > 0 && instIndex < static_cast<int>(song.instruments.size()) && song.instruments[instIndex].ad != 0) {
        return song.instruments[instIndex].ad;
    }
    return fallback;
}

uint8_t firstTempoFromRows(const Song& song, const std::array<std::vector<SourceRow>, 3>& rows) {
    int bestExpandedRow = 0x7fffffff;
    uint8_t bestTempo = 0;
    for (const auto& channelRows : rows) {
        for (const SourceRow& src : channelRows) {
            if (src.row.command == CMD_SETTEMPO && src.row.parameter != 0 && (src.row.parameter & 0x80) == 0) {
                int expandedRow = src.expandedRow >= 0 ? src.expandedRow : 0x7ffffffe;
                if (expandedRow < bestExpandedRow) {
                    bestExpandedRow = expandedRow;
                    bestTempo = gtTempoToPlayerSpeed(src.row.parameter);
                }
            }
        }
    }
    if (bestTempo != 0) {
        return bestTempo;
    }
    return defaultGtPlayerSpeed(song);
}

std::array<uint8_t, HEADER_SIZE> buildHeader(const Song& song, const std::array<std::vector<SourceRow>, 3>& rows, const TargetSpec& spec) {
    std::array<uint8_t, HEADER_SIZE> h{};
    int inst1 = mostUsedInstrument(rows[0]);
    int inst2 = mostUsedInstrument(rows[1]);
    int inst3 = mostUsedInstrument(rows[2]);

    h[0] = 0; // pickup offset
    h[1] = firstTempoFromRows(song, rows);
    h[2] = firstPulseHighFromInstrument(song, inst1, 0x08); // pulse high ch1
    h[3] = firstWaveFromInstrument(song, inst1);
    h[4] = commonSr(song, inst1, 0xf5);
    h[5] = firstPulseHighFromInstrument(song, inst2, 0x08); // pulse high ch2
    h[6] = firstWaveFromInstrument(song, inst2);
    h[7] = commonSr(song, inst2, 0xc5);
    h[8] = firstPulseHighFromInstrument(song, inst3, 0x08); // pulse high ch3
    h[9] = commonAd(song, inst3, 0x00);
    h[10] = commonSr(song, inst3, 0xf9);
    h[11] = 0x08; // filter cutoff
    h[12] = 0xf1; // resonance/switch
    h[13] = 0x1f; // band/volume
    for (int inst : {inst1, inst2, inst3}) {
        FilterInit filter = firstFilterFromInstrument(song, inst, h[13] & 0x0f);
        if (filter.present) {
            if (filter.hasCutoff) {
                h[11] = filter.cutoff;
            }
            if (filter.hasControl) {
                h[12] = filter.control;
                h[13] = filter.bandVolume;
            }
            break;
        }
    }
    h[14] = h[11]; // slide start; keep it static unless explicit filter commands are present
    h[15] = 0x00; // slide speed
    h[16] = 0x16; // slide target
    h[17] = song.extra.ntsc ? 0 : 1;
    h[18] = 1; // note display mode
    h[19] = static_cast<uint8_t>(spec.patternLength - 1);
    h[20] = 1; // frame speed

    std::string author = song.author.empty() ? "UNKNOWN" : song.author;
    std::string title = song.title.empty() ? "UNTITLED" : song.title;
    std::string info = upperAscii(author + " : " + title);
    if (info.size() > 40) {
        info.resize(40);
    }
    while (info.size() < 40) {
        info.push_back(' ');
    }
    std::copy(info.begin(), info.end(), h.begin() + 24);
    return h;
}

int noteToRasterClamped(uint8_t note, int transpose) {
    return std::clamp(int(note) - int(GT_NOTE) + 1 + transpose, 1, 96);
}

int countPitchUniquePatterns(const std::vector<SourceRow>& rows, size_t patternLength) {
    std::vector<PitchPattern> unique;
    uint8_t currentLo = 0;
    uint8_t currentHi = 0;
    for (size_t offset = 0; offset < rows.size(); offset += patternLength) {
        PitchPattern pitch;
        pitch.lo.fill(0);
        pitch.hi.fill(0);
        size_t end = std::min(rows.size(), offset + patternLength);
        for (size_t i = offset; i < end; ++i) {
            const SourceRow& src = rows[i];
            if (isValidNote(src.row.note)) {
                int note = noteToRasterClamped(src.row.note, src.transpose);
                currentLo = FREQ_LO[size_t(note)];
                currentHi = FREQ_HI[size_t(note)];
            }
            size_t out = i - offset;
            pitch.lo[out] = currentLo;
            pitch.hi[out] = currentHi;
        }
        if (std::find(unique.begin(), unique.end(), pitch) == unique.end()) {
            unique.push_back(pitch);
        }
    }
    return static_cast<int>(std::max<size_t>(unique.size(), 1));
}

int pitchOnlyInstrumentLossCost(const Song& song, int instIndex) {
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return 0;
    }
    const Instrument& inst = song.instruments[instIndex];
    int cost = 2; // The note pitch remains, but the exact per-row voice setup does not.
    if (inst.ptr[WTBL] != 0) cost += 16;
    if (inst.ptr[PTBL] != 0) cost += 7;
    if (inst.ptr[FTBL] != 0) cost += 7;
    if (inst.ptr[STBL] != 0 || inst.vibdelay != 0) cost += 10;
    if (inst.ad != 0 || inst.sr != 0 || inst.gatetimer != 0 || inst.firstwave != 0) cost += 6;
    return cost;
}

int programInstrumentLossCost(const Song& song, int instIndex) {
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return 0;
    }
    const Instrument& inst = song.instruments[instIndex];
    int cost = 0;
    if (inst.ptr[PTBL] != 0) cost += 5;
    if (inst.ptr[FTBL] != 0) cost += 5;
    if (inst.ptr[STBL] != 0 || inst.vibdelay != 0) cost += 8;
    return cost;
}

int pitchOnlyCommandLossCost(const PatternRow& row) {
    switch (row.command) {
        case CMD_DONOTHING:
        case CMD_SETFILTERCTRL:
        case CMD_SETFILTERCUTOFF:
        case CMD_SETMASTERVOL:
            return 0;
        case CMD_SETTEMPO:
            return (row.parameter != 0 && (row.parameter & 0x80) == 0) ? 0 : 12;
        case CMD_SETAD:
        case CMD_SETSR:
        case CMD_SETWAVE:
            return 8;
        case CMD_SETWAVEPTR:
        case CMD_SETPULSEPTR:
        case CMD_SETFILTERPTR:
            return 12;
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        case CMD_FUNKTEMPO:
            return 18;
        default:
            return 12;
    }
}

int programCommandLossCost(const PatternRow& row) {
    switch (row.command) {
        case CMD_DONOTHING:
        case CMD_SETAD:
        case CMD_SETSR:
        case CMD_SETWAVE:
        case CMD_SETFILTERCTRL:
        case CMD_SETFILTERCUTOFF:
        case CMD_SETMASTERVOL:
            return 0;
        case CMD_SETTEMPO:
            return (row.parameter != 0 && (row.parameter & 0x80) == 0) ? 0 : 12;
        case CMD_SETWAVEPTR:
        case CMD_SETPULSEPTR:
        case CMD_SETFILTERPTR:
            return 8;
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        case CMD_FUNKTEMPO:
            return 18;
        default:
            return 12;
    }
}

int programRoleInstrumentValue(const Song& song, int instIndex) {
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return 0;
    }
    const Instrument& inst = song.instruments[instIndex];
    int value = 4;
    if (inst.ptr[WTBL] != 0) value += 40;
    if (inst.ptr[PTBL] != 0) value += 18;
    if (inst.ptr[FTBL] != 0) value += 18;
    if (inst.ptr[STBL] != 0 || inst.vibdelay != 0) value += 18;
    if (inst.ad != 0 || inst.sr != 0 || inst.gatetimer != 0 || inst.firstwave != 0) value += 8;
    return value;
}

int programRoleCommandValue(const PatternRow& row) {
    switch (row.command) {
        case CMD_DONOTHING:
            return 0;
        case CMD_SETAD:
        case CMD_SETSR:
        case CMD_SETWAVE:
        case CMD_SETWAVEPTR:
        case CMD_SETFILTERCTRL:
        case CMD_SETFILTERCUTOFF:
        case CMD_SETMASTERVOL:
        case CMD_SETTEMPO:
            return 12;
        case CMD_SETPULSEPTR:
        case CMD_SETFILTERPTR:
            return 16;
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        case CMD_FUNKTEMPO:
            return 24;
        default:
            return 12;
    }
}

SourceChannelCandidate analyzeSourceChannel(const Song& song, int subtune, int sourceChannel, size_t maxRows, const TargetSpec& spec) {
    SourceChannelCandidate candidate;
    candidate.sourceChannel = sourceChannel;
    candidate.rows = expandRowsForChannel(song, subtune, sourceChannel, maxRows);
    candidate.pitchUniquePatterns = countPitchUniquePatterns(candidate.rows, spec.patternLength);

    int contentWeight = 0;
    for (const SourceRow& src : candidate.rows) {
        const PatternRow& row = src.row;
        if (!rowHasData(row)) {
            continue;
        }
        candidate.hasData = true;
        if (isValidNote(row.note)) {
            candidate.noteRows++;
            contentWeight += 20;
            candidate.programRoleValue += 20;
        } else if (isKeyEvent(row.note)) {
            candidate.keyRows++;
            contentWeight += 4;
            candidate.pitchLossCost += 8;
            candidate.programRoleValue += 8;
        }
        if (row.instrument > 0) {
            candidate.instrumentRows++;
            contentWeight += 4;
            candidate.pitchLossCost += pitchOnlyInstrumentLossCost(song, row.instrument);
            candidate.programLossCost += programInstrumentLossCost(song, row.instrument);
            candidate.programRoleValue += programRoleInstrumentValue(song, row.instrument);
        }
        if (row.command != CMD_DONOTHING || row.parameter != 0) {
            candidate.commandRows++;
            contentWeight += 6;
            candidate.pitchLossCost += pitchOnlyCommandLossCost(row);
            candidate.programLossCost += programCommandLossCost(row);
            candidate.programRoleValue += programRoleCommandValue(row);
        }
    }

    if (candidate.pitchUniquePatterns > static_cast<int>(spec.maxPatterns)) {
        candidate.pitchLossCost += 200000 + (candidate.pitchUniquePatterns - int(spec.maxPatterns)) * 1000;
    }
    candidate.ignoredCost = candidate.hasData ? 500 + contentWeight * 20 : 0;
    return candidate;
}

std::string sourceChannelList(const std::array<int, 3>& sourceChannels) {
    std::ostringstream ss;
    ss << "SNG ch " << (sourceChannels[0] + 1) << " -> 1raster ch 1, "
       << "SNG ch " << (sourceChannels[1] + 1) << " -> 1raster ch 2, "
       << "SNG ch " << (sourceChannels[2] + 1) << " -> 1raster ch 3";
    return ss.str();
}

std::vector<ChannelAssignment> rankChannelAssignments(const std::vector<SourceChannelCandidate>& candidates) {
    std::vector<ChannelAssignment> ranked;
    const int n = static_cast<int>(candidates.size());

    for (int ch1 = 0; ch1 < n; ++ch1) {
        for (int ch2 = 0; ch2 < n; ++ch2) {
            if (ch2 == ch1) {
                continue;
            }
            for (int ch3 = 0; ch3 < n; ++ch3) {
                if (ch3 == ch1 || ch3 == ch2) {
                    continue;
                }
                long long loss = candidates[ch1].pitchLossCost + candidates[ch2].pitchLossCost + candidates[ch3].programLossCost;
                for (int c = 0; c < n; ++c) {
                    if (c != ch1 && c != ch2 && c != ch3) {
                        loss += candidates[c].ignoredCost;
                    }
                }
                long long stability = std::abs(ch1 - 0) + std::abs(ch2 - 1) + std::abs(ch3 - 2);
                ranked.push_back({{{ch1, ch2, ch3}}, loss * 100 + stability});
            }
        }
    }

    if (ranked.empty()) {
        ranked.push_back({{{0, 1, 2}}, 0});
    }
    std::sort(ranked.begin(), ranked.end(), [](const ChannelAssignment& a, const ChannelAssignment& b) {
        return a.score < b.score;
    });
    return ranked;
}

void padMappedRows(std::array<std::vector<SourceRow>, 3>& rows, const std::array<int, 3>& sourceChannels) {
    size_t maxRows = std::max({rows[0].size(), rows[1].size(), rows[2].size(), size_t(0)});
    for (int c = 0; c < 3; ++c) {
        while (rows[c].size() < maxRows) {
            SourceRow empty;
            empty.sourceChannel = sourceChannels[c];
            empty.expandedRow = static_cast<int>(rows[c].size());
            rows[c].push_back(empty);
        }
    }
}

std::vector<int> activeInstrumentsForRows(const std::vector<SourceRow>& rows) {
    std::vector<int> active;
    active.reserve(rows.size());
    int currentInstrument = 0;
    for (const SourceRow& row : rows) {
        if (row.row.instrument > 0) {
            currentInstrument = row.row.instrument;
        }
        active.push_back(currentInstrument);
    }
    return active;
}

void warnUnmappedSourceChannels(ConvertStats& stats, const std::vector<SourceChannelCandidate>& candidates,
                                const std::array<int, 3>& sourceChannels) {
    for (const SourceChannelCandidate& candidate : candidates) {
        if (!candidate.hasData) {
            continue;
        }
        if (std::find(sourceChannels.begin(), sourceChannels.end(), candidate.sourceChannel) != sourceChannels.end()) {
            continue;
        }
        std::ostringstream example;
        example << "source channel " << (candidate.sourceChannel + 1)
                << ": " << candidate.noteRows << " note rows, "
                << candidate.keyRows << " key on/off rows, "
                << candidate.instrumentRows << " instrument rows, "
                << candidate.commandRows << " command rows";
        addWarningOnce(stats,
                       "unmapped-source-channel:" + std::to_string(candidate.sourceChannel),
                       "SNG source channel contains data but was not mapped to the 3-channel 1raster player",
                       example.str());
    }
}

int convertArpPitch(uint8_t gtRight, int baseNote) {
    if (gtRight < 0x60) {
        return baseNote + gtRight;
    }
    if (gtRight < 0x80) {
        return baseNote + int(gtRight) - 0x80;
    }
    if (gtRight == 0x80) {
        return baseNote;
    }
    if (gtRight < 0xe0) {
        return int(gtRight) - 0x80;
    }
    return baseNote;
}

uint8_t wavetableWaveValue(uint8_t left) {
    if (left >= 0xe0 && left < 0xf0) {
        return static_cast<uint8_t>(left - 0xe0);
    }
    return left;
}

bool appendSupportedProgramCommand(uint8_t command, uint8_t parameter, std::vector<ProgramRow>& program) {
    switch (command) {
        case CMD_SETAD:
            program.push_back({0, parameter, 0x13});
            return true;
        case CMD_SETSR:
            program.push_back({0, parameter, 0x14});
            return true;
        case CMD_SETFILTERCTRL:
            program.push_back({0, parameter, 0x17});
            return true;
        case CMD_SETFILTERCUTOFF:
            program.push_back({0, parameter, 0x16});
            return true;
        case CMD_SETMASTERVOL:
            program.push_back({0, static_cast<uint8_t>(0x10 | (parameter & 0x0f)), 0x18});
            return true;
        case CMD_SETTEMPO:
            if (parameter != 0 && (parameter & 0x80) == 0) {
                program.push_back({0, gtTempoToPlayerSpeed(parameter), 0xf7});
                return true;
            }
            return false;
        default:
            return false;
    }
}

std::vector<ProgramRow> buildWavetableRows(const Song& song, int instIndex, int baseNote, uint8_t waveOverride,
                                           int maxRows, const SourceRow& src, ConvertStats& stats) {
    std::vector<ProgramRow> rows;
    uint8_t currentWave = waveOverride ? waveOverride : firstWaveFromInstrument(song, instIndex);
    maxRows = std::max(1, maxRows);

    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        rows.push_back({currentWave, FREQ_LO[size_t(baseNote)], FREQ_HI[size_t(baseNote)]});
        return rows;
    }
    uint8_t ptr = song.instruments[instIndex].ptr[WTBL];
    if (ptr == 0) {
        rows.push_back({currentWave, FREQ_LO[size_t(baseNote)], FREQ_HI[size_t(baseNote)]});
        return rows;
    }

    size_t idx = size_t(ptr - 1);
    bool jumped = false;
    for (int guard = 0; idx < song.tables[WTBL].left.size() && guard < 48 && static_cast<int>(rows.size()) < maxRows; ++guard) {
        uint8_t left = song.tables[WTBL].left[idx];
        uint8_t right = song.tables[WTBL].right[idx];

        if (left == 0xff) {
            if (right != 0 && !jumped && right - 1 < song.tables[WTBL].left.size()) {
                jumped = true;
                addWarningOnce(stats,
                               "wtbl-jump:" + std::to_string(instIndex) + ":" + std::to_string(idx) + ":" + std::to_string(right),
                               "GT wavetable jump/loop was unrolled only as far as the 1raster program row budget allows",
                               sourceLocation(2, src) + ": " + instrumentLabel(song, instIndex) + " jumps from WTBL " + hexByte(static_cast<uint8_t>(idx + 1)) + " to " + hexByte(right));
                idx = size_t(right - 1);
                continue;
            }
            break;
        }

        if (left >= 0xf0) {
            uint8_t command = static_cast<uint8_t>(left - 0xf0);
            if (!appendSupportedProgramCommand(command, right, rows)) {
                addWarningOnce(stats,
                               "wtbl-command:" + std::to_string(instIndex) + ":" + std::to_string(idx) + ":" + std::to_string(command) + ":" + std::to_string(right),
                               "GT wavetable command cannot be represented in a 1raster program",
                               sourceLocation(2, src) + ": " + instrumentLabel(song, instIndex) + " WTBL " + hexByte(static_cast<uint8_t>(idx + 1)) + " executes " + commandName(command) + " " + hexByte(right));
            }
            ++idx;
            continue;
        }

        int repeats = 1;
        if (left > 0 && left < 0x10) {
            repeats = left;
            addWarningOnce(stats,
                           "wtbl-delay:" + std::to_string(instIndex) + ":" + std::to_string(idx) + ":" + std::to_string(left),
                           "GT wavetable delay was approximated with repeated 1raster program rows",
                           sourceLocation(2, src) + ": " + instrumentLabel(song, instIndex) + " WTBL " + hexByte(static_cast<uint8_t>(idx + 1)) + " delay " + std::to_string(left),
                           false);
        } else if (left >= 0x10) {
            currentWave = wavetableWaveValue(left);
        }

        int note = std::clamp(convertArpPitch(right, baseNote), 1, 96);
        for (int repeat = 0; repeat < repeats && static_cast<int>(rows.size()) < maxRows; ++repeat) {
            rows.push_back({currentWave, FREQ_LO[size_t(note)], FREQ_HI[size_t(note)]});
        }
        if (static_cast<int>(rows.size()) >= maxRows) {
            addWarningOnce(stats,
                           "wtbl-long:" + std::to_string(instIndex) + ":" + std::to_string(ptr),
                           "long GT wavetable program was shortened to fit the 1raster program table",
                           sourceLocation(2, src) + ": " + instrumentLabel(song, instIndex) + " WTBL starts at " + hexByte(ptr));
            break;
        }
        ++idx;
    }

    if (rows.empty()) {
        rows.push_back({currentWave, FREQ_LO[size_t(baseNote)], FREQ_HI[size_t(baseNote)]});
    }
    return rows;
}

uint8_t allocateProgram(ProgramTable& table, const std::vector<ProgramRow>& program, size_t maxTableRows, ConvertStats& stats) {
    if (program.empty()) {
        return 0;
    }
    auto found = table.cache.find(program);
    if (found != table.cache.end()) {
        return found->second;
    }
    if (size_t(table.next) + program.size() > maxTableRows) {
        throw std::runtime_error("1raster program table overflow");
    }
    uint8_t start = table.next;
    for (const ProgramRow& row : program) {
        table.c1[table.next] = row.c1;
        table.c2[table.next] = row.c2;
        table.c3[table.next] = row.c3;
        table.next++;
    }
    table.cache[program] = start;
    stats.tablePrograms++;
    stats.tableBytesUsed = table.next;
    return start;
}

void warnProgramTrimmed(ConvertStats& stats, const SourceRow& src, size_t wantedRows, int budget) {
    addWarningOnce(stats,
                   "program-budget:" + std::to_string(src.sourceChannel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(wantedRows) + ":" + std::to_string(budget),
                   "1raster row program exceeded the available per-row play-call budget; later SID writes were omitted",
                   sourceLocation(2, src) + ": needed " + std::to_string(wantedRows) + " data rows, budget is " + std::to_string(budget));
}

void appendDirectSidWrite(std::vector<ProgramRow>& program, uint8_t sidOffset, uint8_t value) {
    program.push_back({0, value, sidOffset});
}

int voiceSidBase(int rasterChannel) {
    return rasterChannel * 7;
}

void appendFilterInitProgram(const Song& song, int instIndex, std::vector<ProgramRow>& program) {
    FilterInit filter = firstFilterFromInstrument(song, instIndex);
    if (!filter.present) {
        return;
    }
    if (filter.hasControl) {
        appendDirectSidWrite(program, 0x17, filter.control);
        appendDirectSidWrite(program, 0x18, filter.bandVolume);
    }
    if (filter.hasCutoff) {
        appendDirectSidWrite(program, 0x16, filter.cutoff);
    }
}

void appendPitchChannelProgram(const Song& song, int rasterChannel, const SourceRow& src, int activeInstrument,
                               bool instrumentChanged, std::vector<ProgramRow>& program, ConvertStats& stats) {
    if (rasterChannel < 0 || rasterChannel > 1) {
        return;
    }
    const PatternRow& row = src.row;
    const int base = voiceSidBase(rasterChannel);

    auto waveForActiveInstrument = [&]() {
        uint8_t wave = firstWaveFromInstrument(song, activeInstrument);
        return wave ? wave : uint8_t(0x41);
    };

    auto waveForRow = [&]() {
        if (row.command == CMD_SETWAVE && row.parameter != 0) {
            return row.parameter;
        }
        if (row.command == CMD_SETWAVEPTR && row.parameter > 0 && row.parameter - 1 < song.tables[WTBL].left.size()) {
            uint8_t wave = waveForActiveInstrument();
            size_t idx = size_t(row.parameter - 1);
            for (int guard = 0; idx < song.tables[WTBL].left.size() && guard < 8; ++idx, ++guard) {
                uint8_t left = song.tables[WTBL].left[idx];
                if (left == 0xff) {
                    break;
                }
                if (left >= 0x10 && left < 0xf0) {
                    return wavetableWaveValue(left);
                }
            }
            return wave;
        }
        return waveForActiveInstrument();
    };

    if (isKeyEvent(row.note)) {
        uint8_t wave = waveForRow();
        if (row.note == GT_KEYOFF) {
            wave &= 0xfe;
        } else {
            wave |= 0x01;
        }
        appendDirectSidWrite(program, static_cast<uint8_t>(base + 4), wave);
    }

    if (isValidNote(row.note)) {
        appendDirectSidWrite(program, static_cast<uint8_t>(base + 4), static_cast<uint8_t>(waveForRow() | 0x01));
    }

    if (instrumentChanged && activeInstrument > 0 && activeInstrument < static_cast<int>(song.instruments.size())) {
        const Instrument& inst = song.instruments[activeInstrument];
        PulseInit pulse = firstPulseFromInstrument(song, activeInstrument);
        if (inst.ad != 0) {
            appendDirectSidWrite(program, static_cast<uint8_t>(base + 5), inst.ad);
        }
        if (inst.sr != 0) {
            appendDirectSidWrite(program, static_cast<uint8_t>(base + 6), inst.sr);
        }
        if (pulse.present) {
            appendDirectSidWrite(program, static_cast<uint8_t>(base + 2), pulse.lo);
            appendDirectSidWrite(program, static_cast<uint8_t>(base + 3), pulse.hi);
        }
        appendFilterInitProgram(song, activeInstrument, program);
    }

    switch (row.command) {
        case CMD_DONOTHING:
            break;
        case CMD_SETAD:
            appendDirectSidWrite(program, static_cast<uint8_t>(base + 5), row.parameter);
            break;
        case CMD_SETSR:
            appendDirectSidWrite(program, static_cast<uint8_t>(base + 6), row.parameter);
            break;
        case CMD_SETWAVE:
            if (!isValidNote(row.note) && !isKeyEvent(row.note)) {
                appendDirectSidWrite(program, static_cast<uint8_t>(base + 4), row.parameter);
            }
            break;
        case CMD_SETWAVEPTR:
            if (!isValidNote(row.note) && !isKeyEvent(row.note) && row.parameter > 0 && row.parameter - 1 < song.tables[WTBL].left.size()) {
                appendDirectSidWrite(program, static_cast<uint8_t>(base + 4), waveForRow());
            }
            break;
        case CMD_SETFILTERCTRL:
        case CMD_SETFILTERCUTOFF:
        case CMD_SETMASTERVOL:
        case CMD_SETTEMPO:
            break; // Shared/global commands are appended separately once per row.
        case CMD_SETPULSEPTR:
        case CMD_SETFILTERPTR:
            addWarningOnce(stats,
                           "note-tableptr:" + std::to_string(rasterChannel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "GT pitch-channel table pointer command was only partially represented in 1raster",
                           sourceLocation(rasterChannel, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            break;
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        case CMD_FUNKTEMPO:
            addWarningOnce(stats,
                           "note-cmd:" + std::to_string(rasterChannel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "GT pattern effect on a 1raster pitch-oriented channel was ignored",
                           sourceLocation(rasterChannel, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            break;
        default:
            addWarningOnce(stats,
                           "note-cmd-unknown:" + std::to_string(rasterChannel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "unknown GT pattern command on a pitch-oriented channel was ignored",
                           sourceLocation(rasterChannel, src) + ": command " + hexByte(row.command) + " parameter " + hexByte(row.parameter));
            break;
    }
}

void warnInstrumentTableLimits(const Song& song, int channel, const SourceRow& src, int instIndex, ConvertStats& stats) {
    if (instIndex <= 0 || instIndex >= static_cast<int>(song.instruments.size())) {
        return;
    }
    const Instrument& inst = song.instruments[instIndex];
    const std::string loc = sourceLocation(channel, src) + ": " + instrumentLabel(song, instIndex);
    if (channel < 2 && inst.ptr[WTBL] != 0) {
        addWarningOnce(stats,
                       "inst-wtbl:" + std::to_string(channel) + ":" + std::to_string(instIndex),
                       "GT wavetable/arpeggio on a pitch-oriented 1raster channel is only partially represented",
                       loc + " WTBL pointer " + hexByte(inst.ptr[WTBL]));
    }
    if (inst.ptr[PTBL] != 0) {
        addWarningOnce(stats,
                       "inst-ptbl:" + std::to_string(channel) + ":" + std::to_string(instIndex),
                       "GT pulsetable execution is only sampled at its first set-pulse value; later pulse modulation is not represented",
                       loc + " PTBL pointer " + hexByte(inst.ptr[PTBL]));
    }
    if (inst.ptr[FTBL] != 0) {
        addWarningOnce(stats,
                       "inst-ftbl:" + std::to_string(channel) + ":" + std::to_string(instIndex),
                       "GT filtertable execution is only sampled at its first filter setup/cutoff; later filter modulation is not represented",
                       loc + " FTBL pointer " + hexByte(inst.ptr[FTBL]));
    }
    if (inst.ptr[STBL] != 0 || inst.vibdelay != 0) {
        addWarningOnce(stats,
                       "inst-stbl:" + std::to_string(channel) + ":" + std::to_string(instIndex),
                       "GT instrument vibrato/speedtable behavior is not represented in 1raster",
                       loc + " STBL pointer " + hexByte(inst.ptr[STBL]) + ", vibrato delay " + hexByte(inst.vibdelay));
    }
}

bool appendGlobalCommandFromNoteChannel(int channel, const SourceRow& src, std::vector<ProgramRow>& program, ConvertStats& stats) {
    const PatternRow& row = src.row;
    switch (row.command) {
        case CMD_SETFILTERCTRL:
        case CMD_SETFILTERCUTOFF:
        case CMD_SETMASTERVOL:
            return appendSupportedProgramCommand(row.command, row.parameter, program);
        case CMD_SETTEMPO:
            if (row.parameter != 0 && (row.parameter & 0x80) == 0) {
                return appendSupportedProgramCommand(row.command, row.parameter, program);
            }
            addWarningOnce(stats,
                           "tempo:" + std::to_string(channel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.parameter),
                           "GT channel-local or funk tempo cannot be represented in 1raster",
                           sourceLocation(channel, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            return false;
        default:
            return false;
    }
}

void diagnoseNoteChannelRow(const Song& song, int channel, const SourceRow& src, int activeInstrument,
                            bool pitchSidAssist, ConvertStats& stats) {
    const PatternRow& row = src.row;
    if (row.instrument > 0) {
        warnInstrumentTableLimits(song, channel, src, row.instrument, stats);
        if (!pitchSidAssist) {
            addWarningOnce(stats,
                           "pitch-assist-inst:" + std::to_string(channel) + ":" + std::to_string(row.instrument),
                           "GT pitch-channel instrument SID setup was not emitted because it would overflow the 1raster program table",
                           sourceLocation(channel, src) + ": " + instrumentLabel(song, row.instrument));
        }
    }
    if ((row.note == GT_KEYOFF || row.note == GT_KEYON) && (!pitchSidAssist || activeInstrument <= 0)) {
        addWarningOnce(stats,
                       "key:" + std::to_string(channel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.note),
                       pitchSidAssist ? "GT key on/off row has no active instrument to derive a waveform from"
                                      : "GT key on/off row on a pitch-oriented channel was not emitted because pitch-channel SID assist is disabled",
                       sourceLocation(channel, src) + ": note " + hexByte(row.note));
    }

    switch (row.command) {
        case CMD_DONOTHING:
            return;
        case CMD_SETFILTERCTRL:
        case CMD_SETFILTERCUTOFF:
        case CMD_SETMASTERVOL:
            return; // Moved into the channel-3 program row for this expanded row.
        case CMD_SETTEMPO:
            if (row.parameter != 0 && (row.parameter & 0x80) == 0) {
                return; // Moved into the channel-3 program row for this expanded row.
            }
            return; // appendGlobalCommandFromNoteChannel emits the unsupported warning.
        case CMD_SETAD:
        case CMD_SETSR:
        case CMD_SETWAVE:
        case CMD_SETWAVEPTR:
            if (!pitchSidAssist) {
                addWarningOnce(stats,
                               "pitch-assist-cmd:" + std::to_string(channel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                               "GT pitch-channel SID setup command was ignored because pitch-channel SID assist is disabled",
                               sourceLocation(channel, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            }
            return; // Applied through direct SID writes in the shared program row when assist is enabled.
        case CMD_SETPULSEPTR:
        case CMD_SETFILTERPTR:
            return; // appendPitchChannelProgram emits the partial-representation warning.
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        case CMD_FUNKTEMPO:
            addWarningOnce(stats,
                           "note-cmd:" + std::to_string(channel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "GT pattern effect on a 1raster pitch-oriented channel was ignored",
                           sourceLocation(channel, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            return;
        default:
            addWarningOnce(stats,
                           "note-cmd-unknown:" + std::to_string(channel) + ":" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "unknown GT pattern command on a pitch-only channel was ignored",
                           sourceLocation(channel, src) + ": command " + hexByte(row.command) + " parameter " + hexByte(row.parameter));
            return;
    }
}

uint8_t convertTrack3Row(const Song& song, const SourceRow& src, const SourceRow* ch1Row, const SourceRow* ch2Row,
                         int ch1ActiveInstrument, bool ch1InstrumentChanged,
                         int ch2ActiveInstrument, bool ch2InstrumentChanged,
                         bool pitchSidAssist, int rowProgramBudget, int& currentInstrument,
                         ProgramTable& table, size_t maxTableRows, ConvertStats& stats) {
    const PatternRow& row = src.row;
    if (row.instrument > 0) {
        currentInstrument = row.instrument;
        warnInstrumentTableLimits(song, 2, src, currentInstrument, stats);
    }

    rowProgramBudget = std::max(1, rowProgramBudget);
    std::vector<ProgramRow> prefixProgram;
    std::vector<ProgramRow> trackProgram;
    int note = 0;
    uint8_t waveOverride = 0;

    if (ch1Row) {
        appendGlobalCommandFromNoteChannel(0, *ch1Row, prefixProgram, stats);
        if (pitchSidAssist) {
            appendPitchChannelProgram(song, 0, *ch1Row, ch1ActiveInstrument, ch1InstrumentChanged, prefixProgram, stats);
        }
    }
    if (ch2Row) {
        appendGlobalCommandFromNoteChannel(1, *ch2Row, prefixProgram, stats);
        if (pitchSidAssist) {
            appendPitchChannelProgram(song, 1, *ch2Row, ch2ActiveInstrument, ch2InstrumentChanged, prefixProgram, stats);
        }
    }

    if (row.command == CMD_SETWAVE && row.parameter != 0) {
        waveOverride = row.parameter;
    }

    if (isValidNote(row.note)) {
        note = gtNoteToRaster(row.note, src.transpose, stats);
        std::vector<ProgramRow> wavetableRows = buildWavetableRows(song, currentInstrument, note, waveOverride, rowProgramBudget, src, stats);
        trackProgram.insert(trackProgram.end(), wavetableRows.begin(), wavetableRows.end());
        if (currentInstrument > 0 && currentInstrument < static_cast<int>(song.instruments.size())) {
            const Instrument& inst = song.instruments[currentInstrument];
            if (inst.ad != 0) {
                trackProgram.push_back({0, inst.ad, 0x13});
            }
            if (inst.sr != 0) {
                trackProgram.push_back({0, inst.sr, 0x14});
            }
        }
    } else if (row.note == GT_KEYOFF) {
        trackProgram.push_back({0, 0x00, 0x12});
    } else if (row.note == GT_KEYON) {
        trackProgram.push_back({0, 0x01, 0x12});
    }

    switch (row.command) {
        case CMD_DONOTHING:
        case CMD_SETWAVE:
            break;
        case CMD_SETAD:
            appendSupportedProgramCommand(row.command, row.parameter, trackProgram);
            break;
        case CMD_SETSR:
            appendSupportedProgramCommand(row.command, row.parameter, trackProgram);
            break;
        case CMD_SETFILTERCTRL:
            appendSupportedProgramCommand(row.command, row.parameter, trackProgram);
            break;
        case CMD_SETFILTERCUTOFF:
            appendSupportedProgramCommand(row.command, row.parameter, trackProgram);
            break;
        case CMD_SETMASTERVOL:
            appendSupportedProgramCommand(row.command, row.parameter, trackProgram);
            break;
        case CMD_SETTEMPO:
            if (!appendSupportedProgramCommand(row.command, row.parameter, trackProgram)) {
                addWarningOnce(stats,
                               "tempo:2:" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.parameter),
                               "GT channel-local or funk tempo cannot be represented in 1raster",
                               sourceLocation(2, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            }
            break;
        case CMD_SETWAVEPTR:
        case CMD_SETPULSEPTR:
        case CMD_SETFILTERPTR:
        case CMD_PORTAUP:
        case CMD_PORTADOWN:
        case CMD_TONEPORTA:
        case CMD_VIBRATO:
        case CMD_FUNKTEMPO:
            addWarningOnce(stats,
                           "track3-cmd:" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "GT pattern effect cannot be represented in a 1raster program",
                           sourceLocation(2, src) + ": " + commandName(row.command) + " " + hexByte(row.parameter));
            break;
        default:
            addWarningOnce(stats,
                           "track3-cmd-unknown:" + std::to_string(src.pattern) + ":" + std::to_string(src.patternRow) + ":" + std::to_string(row.command) + ":" + std::to_string(row.parameter),
                           "unknown GT pattern command was ignored",
                           sourceLocation(2, src) + ": command " + hexByte(row.command) + " parameter " + hexByte(row.parameter));
            break;
    }

    std::vector<ProgramRow> program;
    size_t budget = static_cast<size_t>(rowProgramBudget);
    size_t trackRows = std::min(trackProgram.size(), budget);
    program.insert(program.end(), trackProgram.begin(), trackProgram.begin() + static_cast<long>(trackRows));
    budget -= trackRows;

    size_t prefixRows = std::min(prefixProgram.size(), budget);
    program.insert(program.end(), prefixProgram.begin(), prefixProgram.begin() + static_cast<long>(prefixRows));

    if (trackRows < trackProgram.size() || prefixRows < prefixProgram.size()) {
        warnProgramTrimmed(stats, src, prefixProgram.size() + trackProgram.size(), rowProgramBudget);
    }

    if (!program.empty()) {
        program.push_back({0, 0, 0xff});
    }
    return allocateProgram(table, program, maxTableRows, stats);
}

std::vector<std::vector<SourceRow>> chunkRows(const std::vector<SourceRow>& rows, size_t patternLength) {
    std::vector<std::vector<SourceRow>> chunks;
    for (size_t offset = 0; offset < rows.size(); offset += patternLength) {
        size_t end = std::min(rows.size(), offset + patternLength);
        chunks.emplace_back(rows.begin() + static_cast<long>(offset), rows.begin() + static_cast<long>(end));
    }
    if (chunks.empty()) {
        chunks.emplace_back();
    }
    return chunks;
}

template <typename T>
int internPattern(const T& data, std::vector<T>& unique, size_t maxPatterns) {
    auto it = std::find(unique.begin(), unique.end(), data);
    if (it != unique.end()) {
        return static_cast<int>(std::distance(unique.begin(), it));
    }
    if (unique.size() >= maxPatterns) {
        return -1;
    }
    unique.push_back(data);
    return static_cast<int>(unique.size() - 1);
}

TargetResult convertTargetWithAssignment(const Song& song, const TargetSpec& spec,
                                         const std::vector<SourceChannelCandidate>& candidates,
                                         const ChannelAssignment& assignment,
                                         bool pitchSidAssist) {
    TargetResult result;
    result.formatName = spec.name;
    result.pitchSidAssist = pitchSidAssist;

    const size_t maxRows = ORDERLIST_SIZE * spec.patternLength;
    result.sourceChannels = assignment.sourceChannels;
    result.hasChannelMap = true;
    warnUnmappedSourceChannels(result.stats, candidates, assignment.sourceChannels);

    std::array<std::vector<SourceRow>, 3> rows = {
        candidates[static_cast<size_t>(assignment.sourceChannels[0])].rows,
        candidates[static_cast<size_t>(assignment.sourceChannels[1])].rows,
        candidates[static_cast<size_t>(assignment.sourceChannels[2])].rows
    };
    padMappedRows(rows, assignment.sourceChannels);
    std::array<std::vector<int>, 3> activeInstruments = {
        activeInstrumentsForRows(rows[0]),
        activeInstrumentsForRows(rows[1]),
        activeInstrumentsForRows(rows[2])
    };
    for (int c = 0; c < 3; ++c) {
        if (rows[c].size() > maxRows) {
            result.error = spec.name + " cannot fit more than " + std::to_string(maxRows) + " rows per channel";
            return result;
        }
    }

    const size_t maxProgramRows = spec.orb ? 0xf8 : 0xe8;
    const uint8_t stopProgram = allocateProgram(result.programTable, std::vector<ProgramRow>{{0, 0, 0xff}}, maxProgramRows, result.stats);
    int currentPlayerSpeed = firstTempoFromRows(song, rows);

    std::vector<PitchPattern> uniqueCh1;
    std::vector<PitchPattern> uniqueCh2;
    std::vector<std::array<uint8_t, ORB_PATTERN_LENGTH>> uniqueCh3;

    for (int c = 0; c < 3; ++c) {
        std::vector<std::vector<SourceRow>> chunks = chunkRows(rows[c], spec.patternLength);
        if (chunks.size() > ORDERLIST_SIZE) {
            result.error = spec.name + " orderlist would exceed 64 positions";
            return result;
        }
        result.sequences[c].assign(ORDERLIST_SIZE, 0);
        int currentInstrument = 0;
        uint8_t currentLo = 0;
        uint8_t currentHi = 0;
        for (size_t i = 0; i < chunks.size(); ++i) {
            PitchPattern pitch;
            pitch.lo.fill(0);
            pitch.hi.fill(0);
            std::array<uint8_t, ORB_PATTERN_LENGTH> program{};
            program.fill(0);
            for (size_t r = 0; r < chunks[i].size(); ++r) {
                const SourceRow& src = chunks[i][r];
                size_t absoluteRow = i * spec.patternLength + r;
                if (c < 2) {
                    int activeInst = absoluteRow < activeInstruments[c].size() ? activeInstruments[c][absoluteRow] : 0;
                    diagnoseNoteChannelRow(song, c, src, activeInst, pitchSidAssist, result.stats);
                    if (isValidNote(src.row.note)) {
                        int note = gtNoteToRaster(src.row.note, src.transpose, result.stats);
                        currentLo = FREQ_LO[size_t(note)];
                        currentHi = FREQ_HI[size_t(note)];
                    }
                    pitch.lo[r] = currentLo;
                    pitch.hi[r] = currentHi;
                } else {
                    const SourceRow* ch1Row = absoluteRow < rows[0].size() ? &rows[0][absoluteRow] : nullptr;
                    const SourceRow* ch2Row = absoluteRow < rows[1].size() ? &rows[1][absoluteRow] : nullptr;
                    int ch1Inst = absoluteRow < activeInstruments[0].size() ? activeInstruments[0][absoluteRow] : 0;
                    int ch2Inst = absoluteRow < activeInstruments[1].size() ? activeInstruments[1][absoluteRow] : 0;
                    bool ch1Changed = ch1Row && ch1Row->row.instrument > 0;
                    bool ch2Changed = ch2Row && ch2Row->row.instrument > 0;
                    uint8_t pointer = convertTrack3Row(song, src, ch1Row, ch2Row,
                                                       ch1Inst, ch1Changed, ch2Inst, ch2Changed,
                                                       pitchSidAssist,
                                                       programTickBudget(static_cast<uint8_t>(currentPlayerSpeed)),
                                                       currentInstrument, result.programTable, maxProgramRows, result.stats);
                    program[r] = pointer ? pointer : stopProgram;
                    auto updateSpeed = [&](const SourceRow* speedRow) {
                        if (speedRow && speedRow->row.command == CMD_SETTEMPO && speedRow->row.parameter != 0 && (speedRow->row.parameter & 0x80) == 0) {
                            currentPlayerSpeed = programTickBudget(gtTempoToPlayerSpeed(speedRow->row.parameter));
                        }
                    };
                    updateSpeed(&src);
                    updateSpeed(ch1Row);
                    updateSpeed(ch2Row);
                }
            }

            int pat = -1;
            if (c == 0) {
                pat = internPattern(pitch, uniqueCh1, spec.maxPatterns);
                if (pat < 0) {
                    result.error = spec.name + " channel 1 exceeds " + std::to_string(spec.maxPatterns) + " unique patterns";
                    return result;
                }
            } else if (c == 1) {
                pat = internPattern(pitch, uniqueCh2, spec.maxPatterns);
                if (pat < 0) {
                    result.error = spec.name + " channel 2 exceeds " + std::to_string(spec.maxPatterns) + " unique patterns";
                    return result;
                }
            } else {
                pat = internPattern(program, uniqueCh3, spec.maxPatterns);
                if (pat < 0) {
                    result.error = spec.name + " channel 3 exceeds " + std::to_string(spec.maxPatterns) + " unique patterns";
                    return result;
                }
            }
            if (spec.orb) {
                result.sequences[c][i] = static_cast<uint8_t>(0x15 + pat);
            } else {
                result.sequences[c][i] = static_cast<uint8_t>(pat * spec.patternLength);
            }
        }
    }

    size_t patternCount = std::max({uniqueCh1.size(), uniqueCh2.size(), uniqueCh3.size(), size_t(1)});
    result.patterns.assign(patternCount, RasterPattern{});
    for (RasterPattern& p : result.patterns) {
        p.ch1Lo.assign(spec.patternLength, 0);
        p.ch1Hi.assign(spec.patternLength, 0);
        p.ch2Lo.assign(spec.patternLength, 0);
        p.ch2Hi.assign(spec.patternLength, 0);
        p.ch3Program.assign(spec.patternLength, 0);
    }

    auto copyPattern = [&](const std::array<uint8_t, ORB_PATTERN_LENGTH>& src, std::vector<uint8_t>& dst) {
        for (size_t i = 0; i < spec.patternLength; ++i) {
            dst[i] = src[i];
        }
    };

    for (size_t i = 0; i < uniqueCh1.size(); ++i) {
        copyPattern(uniqueCh1[i].lo, result.patterns[i].ch1Lo);
        copyPattern(uniqueCh1[i].hi, result.patterns[i].ch1Hi);
    }
    for (size_t i = 0; i < uniqueCh2.size(); ++i) {
        copyPattern(uniqueCh2[i].lo, result.patterns[i].ch2Lo);
        copyPattern(uniqueCh2[i].hi, result.patterns[i].ch2Hi);
    }
    for (size_t i = 0; i < uniqueCh3.size(); ++i) {
        copyPattern(uniqueCh3[i], result.patterns[i].ch3Program);
    }

    std::array<uint8_t, HEADER_SIZE> header = buildHeader(song, rows, spec);
    const auto& preamble = spec.orb ? ORB_PREAMBLE : ORM_PREAMBLE;
    result.image.assign(preamble.begin(), preamble.end());
    result.image.insert(result.image.end(), header.begin(), header.end());
    for (int c = 0; c < 3; ++c) {
        result.image.insert(result.image.end(), result.sequences[c].begin(), result.sequences[c].end());
    }

    auto writeReversed = [&](const std::vector<uint8_t>& values) {
        for (size_t i = 0; i < spec.patternLength; ++i) {
            result.image.push_back(values[spec.patternLength - 1 - i]);
        }
    };

    if (spec.orb) {
        result.image.insert(result.image.end(), result.programTable.c1.begin(), result.programTable.c1.end());
        result.image.insert(result.image.end(), result.programTable.c2.begin(), result.programTable.c2.end());
        result.image.insert(result.image.end(), result.programTable.c3.begin(), result.programTable.c3.end());
        for (const RasterPattern& pat : result.patterns) {
            size_t start = result.image.size();
            writeReversed(pat.ch1Lo);
            writeReversed(pat.ch1Hi);
            writeReversed(pat.ch2Lo);
            writeReversed(pat.ch2Hi);
            writeReversed(pat.ch3Program);
            while (result.image.size() < start + 0x100) {
                result.image.push_back(0);
            }
        }
    } else {
        for (const RasterPattern& pat : result.patterns) {
            writeReversed(pat.ch1Lo);
        }
        for (size_t i = result.patterns.size(); i < ORM_MAX_PATTERNS; ++i) {
            result.image.insert(result.image.end(), spec.patternLength, 0);
        }
        for (const RasterPattern& pat : result.patterns) {
            writeReversed(pat.ch1Hi);
        }
        for (size_t i = result.patterns.size(); i < ORM_MAX_PATTERNS; ++i) {
            result.image.insert(result.image.end(), spec.patternLength, 0);
        }
        for (const RasterPattern& pat : result.patterns) {
            writeReversed(pat.ch2Lo);
        }
        for (size_t i = result.patterns.size(); i < ORM_MAX_PATTERNS; ++i) {
            result.image.insert(result.image.end(), spec.patternLength, 0);
        }
        for (const RasterPattern& pat : result.patterns) {
            writeReversed(pat.ch2Hi);
        }
        for (size_t i = result.patterns.size(); i < ORM_MAX_PATTERNS; ++i) {
            result.image.insert(result.image.end(), spec.patternLength, 0);
        }
        for (const RasterPattern& pat : result.patterns) {
            writeReversed(pat.ch3Program);
        }
        for (size_t i = result.patterns.size(); i < ORM_MAX_PATTERNS; ++i) {
            result.image.insert(result.image.end(), spec.patternLength, 0);
        }
        result.image.insert(result.image.end(), result.programTable.c1.begin(), result.programTable.c1.end());
        result.image.insert(result.image.end(), result.programTable.c2.begin(), result.programTable.c2.end());
        result.image.insert(result.image.end(), result.programTable.c3.begin(), result.programTable.c3.begin() + 0xe8);
    }

    result.ok = true;
    return result;
}

long long targetQualityScore(const TargetResult& result, const ChannelAssignment& assignment,
                             const std::vector<SourceChannelCandidate>& candidates) {
    if (!result.ok) {
        return 0x4000000000000000LL + assignment.score;
    }
    bool originalChannelOrder = result.sourceChannels == std::array<int, 3>{{0, 1, 2}};
    bool conservativeThreeChannelRemap = candidates.size() == 3 && !originalChannelOrder;
    int bestProgramRoleValue = 0;
    for (const SourceChannelCandidate& candidate : candidates) {
        bestProgramRoleValue = std::max(bestProgramRoleValue, candidate.programRoleValue);
    }
    int programRoleMiss = bestProgramRoleValue - candidates[static_cast<size_t>(result.sourceChannels[2])].programRoleValue;
    return (conservativeThreeChannelRemap ? 100000000000000LL : 0LL)
        + static_cast<long long>(result.stats.unsupportedEvents) * 100000000LL
        + static_cast<long long>(programRoleMiss) * 1000000LL
        + (result.pitchSidAssist ? 0LL : 25000000LL)
        + static_cast<long long>(result.stats.clampedNotes) * 1000000LL
        + static_cast<long long>(result.stats.tableBytesUsed) * 1000LL
        + assignment.score;
}

TargetResult convertTarget(const Song& song, int subtune, const TargetSpec& spec) {
    const size_t maxRows = ORDERLIST_SIZE * spec.patternLength;
    std::vector<SourceChannelCandidate> candidates;
    candidates.reserve(static_cast<size_t>(song.channelCount));
    for (int c = 0; c < song.channelCount; ++c) {
        candidates.push_back(analyzeSourceChannel(song, subtune, c, maxRows, spec));
    }

    std::vector<ChannelAssignment> assignments = rankChannelAssignments(candidates);
    TargetResult best;
    bool haveBest = false;
    long long bestScore = 0;

    for (const ChannelAssignment& assignment : assignments) {
        for (bool pitchSidAssist : {true, false}) {
            TargetResult candidate;
            try {
                candidate = convertTargetWithAssignment(song, spec, candidates, assignment, pitchSidAssist);
            } catch (const std::exception& e) {
                candidate.formatName = spec.name;
                candidate.error = e.what();
                candidate.sourceChannels = assignment.sourceChannels;
                candidate.hasChannelMap = true;
                candidate.pitchSidAssist = pitchSidAssist;
            }
            long long score = targetQualityScore(candidate, assignment, candidates);
            if (!haveBest || score < bestScore || (!best.ok && candidate.ok)) {
                haveBest = true;
                bestScore = score;
                best = std::move(candidate);
            }
        }
    }

    return best;
}

void writeKickAsm(const std::string& path, const TargetResult& result, const std::string& asmMode) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot write ASM output file: " + path);
    }
    bool includePlayer = asmMode == "player";
    size_t tuneDataOffset = result.formatName == "ORB" ? ORB_PREAMBLE.size() : ORM_PREAMBLE.size();
    size_t startOffset = includePlayer ? 2 : tuneDataOffset;

    out << "// Generated by gtultra2raster.\n";
    out << "// Format: " << result.formatName << "\n";
    out << "// ASM mode: " << (includePlayer ? "player+data" : "data-only") << "\n";
    out << "// Player target: Hermit's 1raster" << (result.formatName == "ORB" ? "b ORB" : " ORM") << " player";
    if (includePlayer) {
        out << " embedded below without the two-byte PRG load address";
    } else {
        out << " expected externally at PLAYERADDR";
    }
    out << "\n";
    out << "// Assemble with: java -jar /opt/kickass/KickAss.jar " << path << "\n\n";
    out << ".var PLAYERADDR = $" << std::hex << std::setw(4) << std::setfill('0') << PLAYER_ADDR << std::dec << "\n";
    out << ".var DATAADDR = PLAYERADDR + $" << std::hex << std::setw(4) << std::setfill('0') << (tuneDataOffset - 2) << std::dec << "\n";
    if (includePlayer) {
        out << ".label INIT = PLAYERADDR\n";
        out << ".label PLAY = PLAYERADDR + 3\n";
        out << ".label RASTER_TUNE = DATAADDR\n";
        out << "* = PLAYERADDR \"1raster player and converted tune\"\n\n";
    } else {
        out << ".label RASTER_TUNE = DATAADDR\n";
        out << "* = DATAADDR \"1raster converted tune data\"\n\n";
    }

    for (size_t i = startOffset; i < result.image.size(); i += 16) {
        out << ".byte ";
        for (size_t j = 0; j < 16 && i + j < result.image.size(); ++j) {
            if (j) {
                out << ", ";
            }
            out << "$" << std::hex << std::setw(2) << std::setfill('0') << int(result.image[i + j]) << std::dec;
        }
        out << "\n";
    }
}

void printUsage() {
    std::cout
        << "Usage: gtultra2raster <input.sng> -o <output-base> [options]\n\n"
        << "Options:\n"
        << "  --format orm|orb|both   Output format, default: both\n"
        << "  --subtune N             Convert subtune N, default: 0\n"
        << "  --asm                   Also emit Kick Assembler source with player+data\n"
        << "  --asm-mode MODE         ASM source mode: none, player, or data\n"
        << "  --asm-format FORMAT     ASM source target: auto, orm, or orb\n"
        << "  --asm-data              Shortcut for --asm-mode data\n"
        << "  --analyze               Analyze fit and warnings without writing files\n"
        << "  -h, --help              Show this help\n";
}

Options parseOptions(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            std::exit(0);
        } else if (arg == "-o") {
            if (++i >= argc) {
                throw std::runtime_error("-o requires an output base");
            }
            opt.outputBase = argv[i];
        } else if (arg == "--format") {
            if (++i >= argc) {
                throw std::runtime_error("--format requires orm, orb, or both");
            }
            opt.format = argv[i];
            opt.format = lowerAscii(opt.format);
            if (opt.format != "orm" && opt.format != "orb" && opt.format != "both") {
                throw std::runtime_error("--format must be orm, orb, or both");
            }
        } else if (arg == "--subtune") {
            if (++i >= argc) {
                throw std::runtime_error("--subtune requires an index");
            }
            opt.subtune = std::stoi(argv[i]);
        } else if (arg == "--asm") {
            opt.asmMode = "player";
        } else if (arg == "--asm-mode") {
            if (++i >= argc) {
                throw std::runtime_error("--asm-mode requires none, player, or data");
            }
            opt.asmMode = normalizeAsmMode(argv[i]);
        } else if (arg == "--asm-format") {
            if (++i >= argc) {
                throw std::runtime_error("--asm-format requires auto, orm, or orb");
            }
            opt.asmFormat = normalizeAsmFormat(argv[i]);
        } else if (arg == "--asm-player") {
            opt.asmMode = "player";
        } else if (arg == "--asm-data") {
            opt.asmMode = "data";
        } else if (arg == "--no-asm") {
            opt.asmMode = "none";
        } else if (arg == "--analyze") {
            opt.analyzeOnly = true;
        } else if (arg.rfind("-", 0) == 0) {
            throw std::runtime_error("unknown option: " + arg);
        } else if (opt.inputPath.empty()) {
            opt.inputPath = arg;
        } else if (opt.outputBase.empty()) {
            opt.outputBase = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }
    if (opt.inputPath.empty()) {
        throw std::runtime_error("missing input .sng file");
    }
    if (opt.outputBase.empty() && !opt.analyzeOnly) {
        opt.outputBase = stripKnownExtension(opt.inputPath);
    }
    opt.outputBase = stripKnownExtension(opt.outputBase);
    if (opt.asmMode != "none" && opt.asmFormat != "auto" && opt.format != "both" && opt.format != opt.asmFormat) {
        throw std::runtime_error("--asm-format " + opt.asmFormat + " requires --format " + opt.asmFormat + " or --format both");
    }
    return opt;
}

void printTargetReport(const TargetResult& result) {
    std::cout << result.formatName << ": ";
    if (!result.ok) {
        std::cout << "not written (" << result.error << ")\n";
        if (result.hasChannelMap) {
            std::cout << "  channel map: " << sourceChannelList(result.sourceChannels) << "\n";
        }
        for (const auto& [warning, count] : result.stats.warningCounts) {
            std::cout << "  warning: " << warning;
            if (count > 1) {
                std::cout << " (" << count << " occurrences)";
            }
            std::cout << "\n";
            auto examples = result.stats.warningExamples.find(warning);
            if (examples != result.stats.warningExamples.end()) {
                for (const std::string& example : examples->second) {
                    std::cout << "    - " << example << "\n";
                }
            }
        }
        return;
    }
    std::cout << "ok, " << result.patterns.size() << " patterns, "
              << result.stats.tableBytesUsed << " program-table rows used";
    if (result.stats.unsupportedEvents) {
        std::cout << ", " << result.stats.unsupportedEvents << " unsupported events";
    }
    if (result.stats.clampedNotes) {
        std::cout << ", " << result.stats.clampedNotes << " clamped notes";
    }
    std::cout << "\n";
    if (result.hasChannelMap) {
        std::cout << "  channel map: " << sourceChannelList(result.sourceChannels) << "\n";
    }
    if (!result.pitchSidAssist) {
        std::cout << "  pitch-channel SID assist: disabled to preserve channel-3 program data\n";
    }
    for (const auto& [warning, count] : result.stats.warningCounts) {
        std::cout << "  warning: " << warning;
        if (count > 1) {
            std::cout << " (" << count << " occurrences)";
        }
        std::cout << "\n";
        auto examples = result.stats.warningExamples.find(warning);
        if (examples != result.stats.warningExamples.end()) {
            for (const std::string& example : examples->second) {
                std::cout << "    - " << example << "\n";
            }
        }
    }
}

std::vector<TargetSpec> requestedTargets(const std::string& format) {
    std::vector<TargetSpec> targets;
    if (format == "orm" || format == "both") {
        targets.push_back({"ORM", false, ORM_PATTERN_LENGTH, ORM_MAX_PATTERNS});
    }
    if (format == "orb" || format == "both") {
        targets.push_back({"ORB", true, ORB_PATTERN_LENGTH, ORB_MAX_PATTERNS});
    }
    return targets;
}

const TargetResult* selectAsmSource(const std::vector<TargetResult>& results, const std::string& asmFormat) {
    if (asmFormat == "orm" || asmFormat == "orb") {
        std::string wanted = upperAscii(asmFormat);
        for (const TargetResult& result : results) {
            if (result.ok && result.formatName == wanted) {
                return &result;
            }
        }
        throw std::runtime_error("no successful " + wanted + " target is available for ASM output");
    }

    for (const TargetResult& result : results) {
        if (result.ok && result.formatName == "ORB") {
            return &result;
        }
    }
    for (const TargetResult& result : results) {
        if (result.ok) {
            return &result;
        }
    }
    throw std::runtime_error("no successful target is available for ASM output");
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options opt = parseOptions(argc, argv);
        Song song = parseSong(opt.inputPath);
        if (opt.subtune < 0 || opt.subtune >= song.subtuneCount) {
            throw std::runtime_error("subtune index out of range");
        }

        std::cout << "Input: " << opt.inputPath << "\n";
        std::cout << "Title: " << (song.title.empty() ? "<untitled>" : song.title) << "\n";
        std::cout << "Author: " << (song.author.empty() ? "<unknown>" : song.author) << "\n";
        std::cout << "Subtunes: " << song.subtuneCount << ", channels in file: " << song.channelCount << "\n";
        if (song.extra.present) {
            std::cout << "GTUltra metadata: " << (song.extra.ntsc ? "NTSC" : "PAL")
                      << ", max SID channels " << song.extra.maxSidChannels << "\n";
        }

        std::vector<TargetResult> results;
        for (const TargetSpec& spec : requestedTargets(opt.format)) {
            try {
                results.push_back(convertTarget(song, opt.subtune, spec));
            } catch (const std::exception& e) {
                TargetResult failed;
                failed.formatName = spec.name;
                failed.error = e.what();
                results.push_back(failed);
            }
        }

        for (const TargetResult& result : results) {
            printTargetReport(result);
        }

        if (opt.analyzeOnly) {
            return 0;
        }

        for (const TargetResult& result : results) {
            if (!result.ok) {
                continue;
            }
            std::string ext = result.formatName == "ORM" ? ".orm" : ".orb";
            std::string path = opt.outputBase + ext;
            writeWholeFile(path, result.image);
            std::cout << "Wrote " << path << " (" << result.image.size() << " bytes)\n";
        }

        if (opt.asmMode != "none") {
            const TargetResult* asmSource = selectAsmSource(results, opt.asmFormat);
            std::string asmPath = opt.outputBase + ".asm";
            writeKickAsm(asmPath, *asmSource, opt.asmMode);
            std::cout << "Wrote " << asmPath << " (" << opt.asmMode << " ASM from " << asmSource->formatName << ")\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "gtultra2raster: error: " << e.what() << "\n";
        return 1;
    }
}
