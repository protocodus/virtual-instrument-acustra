#pragma once

// Scratch-only observation experiment. No engine implementation is compiled
// here: this TU links the frozen baseline libAcustraDSP.a beside this file.
#include "DSP/AcustraEngine.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static constexpr const char* schema = "shape,wood,mode,leftRe,leftIm,rightRe,rightIm,leftMomentRe,leftMomentIm,rightMomentRe,rightMomentIm";
    static std::string environment(const char* name)
    {
        const char* value = std::getenv(name);
        return value ? value : "";
    }
    static void openNew(std::ofstream& stream, const std::string& path,
                        std::ios::openmode mode = std::ios::out)
    {
        if (std::filesystem::exists(path))
            throw std::runtime_error("refusing to overwrite harness output: " + path);
        stream.open(path, mode);
        stream.imbue(std::locale::classic());
        stream << std::setprecision(17);
        if (!stream) throw std::runtime_error("cannot create harness output: " + path);
    }
    static std::array<float, 8> residues(const AcustraEngine::BodyMode& mode)
    {
        return { mode.leftReal, mode.leftImaginary, mode.rightReal, mode.rightImaginary,
            mode.leftMomentReal, mode.leftMomentImaginary,
            mode.rightMomentReal, mode.rightMomentImaginary };
    }
    static void set(AcustraEngine::BodyMode& mode, const std::array<float, 8>& r)
    {
        mode.leftReal = r[0]; mode.leftImaginary = r[1];
        mode.rightReal = r[2]; mode.rightImaginary = r[3];
        mode.leftMomentReal = r[4]; mode.leftMomentImaginary = r[5];
        mode.rightMomentReal = r[6]; mode.rightMomentImaginary = r[7];
    }
    static void set(AcustraEngine::BodyBank& bank, std::size_t i,
                    const std::array<float, 8>& r)
    {
        bank.leftReal[i] = r[0]; bank.leftImaginary[i] = r[1];
        bank.rightReal[i] = r[2]; bank.rightImaginary[i] = r[3];
        bank.leftMomentReal[i] = r[4]; bank.leftMomentImaginary[i] = r[5];
        bank.rightMomentReal[i] = r[6]; bank.rightMomentImaginary[i] = r[7];
    }
    static void configure(AcustraEngine& engine)
    {
        const auto exportPath = environment("ACUSTRA_STUDIO_OBSERVER_EXPORT");
        if (!exportPath.empty())
        {
            std::ofstream out; openNew(out, exportPath);
            out << schema << '\n';
            for (int shape = 0; shape < 4; ++shape)
                for (int wood = 0; wood < 3; ++wood)
                {
                    const auto& c = engine.bodyConfigurations_[3 * shape + wood];
                    for (int mode = 0; mode < c.count; ++mode)
                    {
                        out << shape << ',' << wood << ',' << mode;
                        for (const auto v : residues(c.modes[mode])) out << ',' << v;
                        out << '\n';
                    }
                }
            if (!out) throw std::runtime_error("observer export failed");
        }
        const auto path = environment("ACUSTRA_STUDIO_OBSERVER_FILE");
        if (path.empty()) return;
        std::ifstream input(path); input.imbue(std::locale::classic());
        if (!input) throw std::runtime_error("cannot open observer CSV: " + path);
        struct Row { int shape, wood, mode; std::array<float, 8> r; };
        std::vector<Row> rows;
        std::set<std::array<int, 3>> seen;
        std::string line;
        int lineNumber = 0;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line == schema && rows.empty()) continue;
            for (auto& c : line) if (c == ',') c = ' ';
            std::istringstream fields(line); fields.imbue(std::locale::classic());
            Row row {};
            bool valid = bool(fields >> row.shape >> row.wood >> row.mode);
            for (auto& v : row.r) valid = valid && bool(fields >> v) && std::isfinite(v);
            fields >> std::ws;
            valid = valid && fields.eof() && row.shape >= 0 && row.shape < 4
                && row.wood >= 0 && row.wood < 3 && row.mode >= 0;
            if (valid)
                valid = row.mode < engine.bodyConfigurations_[3 * row.shape + row.wood].count;
            if (!valid || !seen.insert({row.shape, row.wood, row.mode}).second)
                throw std::runtime_error("invalid/duplicate observer CSV row "
                    + std::to_string(lineNumber));
            rows.push_back(row);
        }
        if (input.bad() || rows.empty()) throw std::runtime_error("empty/unreadable observer CSV");
        // Validate the entire file before changing anything. Only eight output
        // residues are assigned; poles, counts, ordering, filters, mechanics,
        // all states and level references retain their prepared values.
        for (const auto& row : rows)
        {
            set(engine.bodyConfigurations_[3 * row.shape + row.wood].modes[row.mode], row.r);
            if (row.shape == static_cast<int>(engine.configuredBodyShape_)
                && row.wood == static_cast<int>(engine.configuredBodyMaterial_))
            {
                set(engine.bodyModes_[row.mode], row.r);
                set(engine.bodyBank_, row.mode, row.r);
            }
            if (engine.bodyModelFade_ < 1.0f
                && row.shape == static_cast<int>(engine.fadingBodyShape_)
                && row.wood == static_cast<int>(engine.fadingBodyMaterial_))
                set(engine.fadingBodyBank_, row.mode, row.r);
        }
        std::cerr << "observer CSV: " << rows.size() << " rows from " << path << '\n';
    }

    // Explicit dynamic values only: no padding, no observation residues or
    // microphone/filter/output states. Doubles also preserve each float exactly.
    template <class Emit> static void states(const AcustraEngine& engine, Emit emit, bool names = false)
    {
        for (int fading = 0; fading < 2; ++fading)
        {
            const auto& b = fading ? engine.fadingBridgeLoad_ : engine.bridgeLoad_;
            const std::string prefix = fading ? "fadingBridge" : "bridge";
            for (int axis = 0; axis < 2; ++axis)
            {
                const auto& modes = axis ? b.rockModes : b.heaveModes;
                for (std::size_t i = 0; i < modes.size(); ++i)
                {
                    const auto p = names ? prefix + (axis ? "Rock" : "Heave") + std::to_string(i) : "";
                    emit(names ? p + "Input1" : "", modes[i].input1);
                    emit(names ? p + "Output1" : "", modes[i].output1);
                    emit(names ? p + "Output2" : "", modes[i].output2);
                }
            }
            const auto& body = fading ? engine.fadingBodyBank_ : engine.bodyBank_;
            for (int i = 0; i < AcustraEngine::bodyModeCount; ++i)
            {
                const auto p = names ? std::string(fading ? "fadingBody" : "body") + std::to_string(i) : "";
                emit(names ? p + "Real" : "", double(body.real[i]));
                emit(names ? p + "Imag" : "", double(body.imaginary[i]));
                emit(names ? p + "MomentReal" : "", double(body.momentReal[i]));
                emit(names ? p + "MomentImag" : "", double(body.momentImaginary[i]));
            }
        }
    }
    struct Trace
    {
        std::ofstream csv, binary;
        bool active() const { return csv.is_open() || binary.is_open(); }
        void prepare(const AcustraEngine& engine)
        {
            const auto csvPath = environment("ACUSTRA_STUDIO_TRACE_FILE");
            const auto statePath = environment("ACUSTRA_STUDIO_TRACE_STATES_FILE");
            if (!csvPath.empty())
            {
                openNew(csv, csvPath);
                csv << "frame,bridgeVelocity,reactionForce,bodyForce,bridgePower,bodyPower,"
                    "displacement,normalizedRockDisplacement,integratedReactionForce,"
                    "integratedReactionMoment,integratedBodyForce,integratedBodyMoment,"
                    "immediateHeave,immediateCross,immediateRock,pastHeave,pastRock,"
                    "piezoWave,piezoForce,piezoOpenVolts,piezoJackVolts,piezoBufferInputVolts,"
                    "piezoGainStageDriveVolts,piezoOutputVolts,modalStateFnv1a64\n";
            }
            if (!statePath.empty())
            {
                openNew(binary, statePath, std::ios::out | std::ios::binary);
                std::ofstream columns; openNew(columns, statePath + ".columns.txt");
                columns << "frame\n";
                states(engine, [&](const std::string& name, double) { columns << name << '\n'; }, true);
                if (!columns) throw std::runtime_error("state trace schema write failed");
            }
        }
        void sample(const AcustraEngine& engine, std::int64_t frame)
        {
            std::uint64_t hash = 14695981039346656037ULL;
            const auto writeDouble = [&](double value)
            {
                const auto bits = std::bit_cast<std::uint64_t>(value);
                for (unsigned shift = 0; shift < 64; shift += 8)
                {
                    const auto byte = static_cast<unsigned char>(bits >> shift);
                    hash = (hash ^ byte) * 1099511628211ULL;
                    if (binary.is_open()) binary.put(static_cast<char>(byte));
                }
            };
            if (binary.is_open()) writeDouble(double(frame));
            // The hash covers modal state only, independent of binary trace.
            hash = 14695981039346656037ULL;
            states(engine, [&](const std::string&, double value) { writeDouble(value); });
            if (csv.is_open())
            {
                const auto& b = engine.bridgeLoad_;
                const auto p = engine.getLastPiezoProbe();
                csv << frame;
                for (const double v : { double(engine.lastBridgeVelocity_),
                    double(engine.lastBridgeReactionForce_), double(engine.lastBridgeBodyForce_),
                    double(engine.lastBridgePower_), double(engine.lastBridgeBodyPower_),
                    double(b.displacement), double(b.rotation), double(b.mainIntegratedForce),
                    double(b.mainIntegratedMoment), double(b.bodyIntegratedForce),
                    double(b.bodyIntegratedMoment), double(b.immediateHeave),
                    double(b.immediateCross), double(b.immediateRock), double(b.pastHeave),
                    double(b.pastRock), double(engine.lastPiezoWave_), double(engine.lastPiezoForce_),
                    double(p.openCircuit), double(p.jack), double(p.bufferInput),
                    double(p.gainStageDrive), engine.piezoOutputVolts_ }) csv << ',' << v;
                csv << ',' << std::hex << hash << std::dec << '\n';
            }
        }
        void finish()
        {
            for (auto* stream : { &csv, &binary })
                if (stream->is_open())
                {
                    stream->close();
                    if (stream->fail()) throw std::runtime_error("state trace write failed");
                }
        }
    };

    // Read-only independent allow-list check: bytes of the SAME prepared
    // instance before/after injection, with only allowed output residue
    // fields masked. Padding is not interpreted and cannot change between
    // these snapshots. Every pole, cache metadata, bridge coefficient,
    // mechanical state, piezo field and trim remains in the comparison.
    static std::vector<unsigned char> nonObservationBytes(const AcustraEngine& engine)
    {
        const auto* base = reinterpret_cast<const unsigned char*>(&engine);
        std::vector<unsigned char> result(base, base + sizeof(engine));
        const auto mask = [&](const auto& field)
        {
            const auto offset = reinterpret_cast<const unsigned char*>(&field) - base;
            std::fill(result.begin() + offset, result.begin() + offset + sizeof(field), 0);
        };
        const auto maskMode = [&](const auto& m)
        {
            mask(m.leftReal); mask(m.leftImaginary); mask(m.rightReal); mask(m.rightImaginary);
            mask(m.leftMomentReal); mask(m.leftMomentImaginary);
            mask(m.rightMomentReal); mask(m.rightMomentImaginary);
        };
        for (const auto& m : engine.bodyModes_) maskMode(m);
        for (const auto& c : engine.bodyConfigurations_)
            for (const auto& m : c.modes) maskMode(m);
        for (const auto* b : { &engine.bodyBank_, &engine.fadingBodyBank_ }) maskMode(*b);
        return result;
    }
    static std::vector<std::uint32_t> poleBits(const AcustraEngine& engine)
    {
        std::vector<std::uint32_t> result;
        for (const auto& c : engine.bodyConfigurations_)
            for (const auto& m : c.modes)
            {
                result.push_back(std::bit_cast<std::uint32_t>(m.poleReal));
                result.push_back(std::bit_cast<std::uint32_t>(m.poleImaginary));
            }
        return result;
    }
};
} // namespace acustra
