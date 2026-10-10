// Render AcustraEngine peers for rows of an external (non-bank) corpus.
// The job file holds one render per line, "KEY PICKING MIDI VELOCITY"
// (picking finger|pick|thumb, velocity 1-127); blank lines and lines starting
// with '#' are skipped. Each line is written to OUTDIR/model-KEY.f32: 4.2 s of
// 48 kHz stereo float32 little-endian.
//
// Every row is played on steel strings, the instrument's only strings since
// 2026-09-29. A corpus's nylon rows have no model to compare with, so they
// are skipped before they reach a job file (Tools/BenchmarkOpenCorpora.py
// leaves them out and says so); a job line still naming a material is
// rejected, not guessed at.
//
// This is PhysicalFitRenderer's model half with the schedule read from a file
// instead of the embedded bank, so a score on an external corpus is a score of
// the same renders: the options, the 24-value calibration vector, its bounds
// and makeCalibration, and renderModel (a fresh engine per note, 48 kHz,
// 127-sample blocks, default public controls, the default Dreadnought unless
// --shape is given) are copied from Tools/PhysicalFitRenderer.cpp and must be
// kept identical to it. The one difference is that the picking tool comes
// from each job row rather than being Finger for the bank's plucked rows and
// --archtop-picking for its picked ones.
//
// Tools/BenchmarkOpenCorpora.py writes the job files, splits them across
// several processes, and scores the renders. On success one JSON line naming
// the rendered count, calibration and controls is printed to stdout so the
// caller can record what it scored.

#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using acustra::AcustraEngine;
using acustra::EngineParameters;
using acustra::MaterialCalibration;
using acustra::PhysicalCalibration;
using acustra::fittedPhysicalCalibration;

// One model per invocation. Without --shape every row renders the public
// default Dreadnought, the body steel's calibration was fitted on, in its
// default wood (Spruce, the wood the measured g21 body was built of).
std::optional<acustra::BodyShape> renderShapeOverride;
constexpr std::array shapeNames { "parlor", "auditorium", "dreadnought", "jumbo" };
acustra::BodyMaterial renderBodyMaterial { acustra::EngineParameters {}.bodyMaterial };
constexpr std::array bodyMaterialNames { "spruce", "mahogany", "maple" };
// --room: the Room every model renders with (0, the engine's default, is
// the dry instrument the calibration was fitted on).
float renderRoom { 0.0f };
constexpr std::array pickingNames { "finger", "pick", "thumb" };

acustra::BodyShape renderShape() noexcept
{
    return renderShapeOverride.value_or(acustra::EngineParameters {}.shape);
}

constexpr int modelSampleRate = 48000;
constexpr int renderBlockSize = 127;
constexpr double renderSeconds = 4.2;
constexpr std::size_t calibrationValueCount = 24;

using CalibrationValues = std::array<float, calibrationValueCount>;

// Reject rather than silently clamp so the caller's CLI vector is the
// calibration that was actually rendered. These mirror AcustraEngine's bounds
// and PhysicalFitRenderer's copy of them.
constexpr CalibrationValues calibrationMinimums {{
    0.96f, 0.05f, 0.25f, -6.0f, 0.25f, 0.4f,
    0.35f, 0.35f, 0.0f, 0.7f, 0.0f, -1.0f,
    0.25f, 0.0f, -0.06f, 0.5f, 0.0f, 100.0f,
    0.00325f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
}};

constexpr CalibrationValues calibrationMaximums {{
    1.04f, 1.8f, 4.0f, 6.0f, 4.0f, 2.0f,
    3.0f, 2.5f, 3.0f, 3.0f, 1.2f, 1.0f,
    32.0f, 0.04f, 0.05f, 4.0f, 0.02f, 8000.0f,
    0.060f, 2.0f, 4.0f, 1.0e-3f, 0.01f, 2.0f,
}};

struct Job
{
    std::string key;
    acustra::PickingTechnique picking {};
    int midi {};
    int velocity {};
};

std::size_t durationFrames(std::uint32_t sampleRate)
{
    return static_cast<std::size_t>(std::llround(
        renderSeconds * static_cast<double>(sampleRate)));
}

bool finite(const std::vector<float>& samples)
{
    return std::all_of(samples.begin(), samples.end(),
        [] (float value) { return std::isfinite(value); });
}

void writeF32(const std::filesystem::path& path,
              const std::vector<float>& samples)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("could not create " + path.string());

    if constexpr (std::endian::native == std::endian::little)
    {
        output.write(reinterpret_cast<const char*>(samples.data()),
                     static_cast<std::streamsize>(samples.size()
                         * sizeof(float)));
    }
    else
    {
        for (const float sample : samples)
        {
            const auto bits = std::bit_cast<std::uint32_t>(sample);
            const std::array<char, 4> bytes {{
                static_cast<char>(bits),
                static_cast<char>(bits >> 8),
                static_cast<char>(bits >> 16),
                static_cast<char>(bits >> 24),
            }};
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
    }
    output.close();
    if (output.fail())
        throw std::runtime_error("could not write " + path.string());
}

// Copied from PhysicalFitRenderer.cpp; keep the two identical.
PhysicalCalibration makeCalibration(const CalibrationValues& values)
{
    PhysicalCalibration calibration = fittedPhysicalCalibration;
    calibration.bodyFrequencyScale = values[0];
    calibration.bodyQScale = values[1];
    calibration.bridgeMobilityScale = values[2];
    calibration.residueTiltDbPerOctave = values[3];
    MaterialCalibration& steel = calibration.steel;
    steel.stiffnessScale = values[4];
    steel.fundamentalT60Scale = values[5];
    steel.frequencyLossScale = values[6];
    steel.apertureScale = values[7];
    steel.transientScale = values[8];
    steel.pluckDistanceScale = values[9];
    steel.velocityBrightnessDepth = values[10];
    calibration.apertureRegisterExponent = values[11];
    calibration.lowBodyModeGain = values[12];
    calibration.steelDisplacementScaleMetres = values[13];
    calibration.steelFretT60Slope = values[14];
    calibration.highLossCutoffScale = values[15];
    calibration.bridgeConductanceFloor = values[16];
    calibration.bridgeConductanceCornerHz = values[17];
    calibration.bridgeTailLengthMetres = values[18];
    calibration.pickReleaseVelocityShare = values[19];
    calibration.pickReleaseVelocityExponent = values[20];
    calibration.pickEdgeRadiusMetres = values[21];
    calibration.steelWoundFrictionLoss = values[22];
    calibration.steelPlainBendingLoss = values[23];
    return calibration;
}

// PhysicalFitRenderer's renderModel with the picking tool taken from the job
// row; everything else is the same statement for statement.
std::vector<float> renderModel(acustra::PickingTechnique picking,
                               int midi, int velocity,
                               const PhysicalCalibration& calibration)
{
    AcustraEngine engine;
    EngineParameters parameters;
    parameters.shape = renderShape();
    parameters.bodyMaterial = renderBodyMaterial;
    parameters.room = renderRoom;
    parameters.picking = picking;
    engine.setParameters(parameters);
    engine.setPhysicalCalibration(calibration);
    engine.prepare(modelSampleRate, renderBlockSize);
    engine.noteOn(midi, static_cast<float>(velocity) / 127.0f);

    const auto frames = durationFrames(modelSampleRate);
    std::vector<float> output(frames * 2);
    std::array<float, renderBlockSize> left {};
    std::array<float, renderBlockSize> right {};
    for (std::size_t offset = 0; offset < frames; offset += renderBlockSize)
    {
        const int count = static_cast<int>(std::min<std::size_t>(
            renderBlockSize, frames - offset));
        engine.process(left.data(), right.data(), count);
        for (int frame = 0; frame < count; ++frame)
        {
            output[2 * (offset + static_cast<std::size_t>(frame))]
                = left[static_cast<std::size_t>(frame)];
            output[2 * (offset + static_cast<std::size_t>(frame)) + 1]
                = right[static_cast<std::size_t>(frame)];
        }
    }
    return output;
}

std::string calibrationJson(const CalibrationValues& values)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(9) << '[';
    for (std::size_t index = 0; index < values.size(); ++index)
        text << (index == 0 ? "" : ", ") << values[index];
    text << ']';
    return text.str();
}

// PhysicalFitRenderer's modelControlsJson, with picking named per row.
std::string modelControlsJson()
{
    const EngineParameters parameters;
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(9);
    text << "{\"shape\": \""
         << shapeNames[static_cast<std::size_t>(renderShape())]
         << "\", \"body_material\": " << static_cast<int>(renderBodyMaterial)
         // Fixed at the value every manifest carried while a second
         // guitar model existed, so model_controls still compare equal to
         // earlier runs' (BuildRealismEvidence.py pairs runs on them).
         << ", \"guitar_model\": \"original\""
         << ", \"capture\": \""
         << std::array { "stereo_mic", "mono_mic", "mono_mic",
                               "piezo", "piezo", "mono_mic", "piezo", "mono_mic" }[
                      static_cast<std::size_t>(parameters.capture)]
         << "\", \"picking\": \"per job row: finger, pick or thumb\""
         << ", \"tuning\": " << static_cast<int>(parameters.tuning)
         << ", \"string_age\": " << parameters.stringAge
         << ", \"pluck_position\": " << parameters.pluckPosition
         << ", \"touch\": " << parameters.touch
         << ", \"body_amount\": " << parameters.bodyAmount
         << ", \"stereo_width\": " << parameters.stereoWidth
         << ", \"room\": " << renderRoom
         << ", \"output_gain\": " << parameters.outputGain << "}";
    return text.str();
}

bool parseFloat(const char* text, float& value)
{
    if (text == nullptr || *text == '\0')
        return false;
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return input && input.peek() == std::char_traits<char>::eof()
        && std::isfinite(value);
}

bool parseInt(const std::string& text, int low, int high, int& value)
{
    if (text.empty() || text.size() > 4)
        return false;
    if (!std::all_of(text.begin(), text.end(),
                     [] (char c) { return c >= '0' && c <= '9'; }))
        return false;
    value = std::stoi(text);
    return value >= low && value <= high;
}

bool validKey(const std::string& key)
{
    // The key becomes a file name; keep it to a portable alphabet.
    return !key.empty() && key.size() <= 200 && key.front() != '.'
        && std::all_of(key.begin(), key.end(), [] (char c)
           {
               return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                   || (c >= '0' && c <= '9') || c == '-' || c == '_'
                   || c == '.';
           });
}

template <std::size_t Count>
std::optional<std::size_t> findName(const std::array<const char*, Count>& names,
                                    const std::string& text)
{
    for (std::size_t index = 0; index < Count; ++index)
        if (text == names[index])
            return index;
    return std::nullopt;
}

std::vector<Job> readJobs(const std::filesystem::path& path)
{
    std::ifstream file;
    std::istream* input = &std::cin;
    if (path != "-")
    {
        file.open(path);
        if (!file)
            throw std::runtime_error("could not read job file " + path.string());
        input = &file;
    }

    std::vector<Job> jobs;
    std::set<std::string> keys;
    std::string line;
    int number = 0;
    while (std::getline(*input, line))
    {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        std::string key, picking, midi, velocity, extra;
        fields >> key >> picking >> midi >> velocity;
        const auto where = "job line " + std::to_string(number) + ": ";
        if (velocity.empty() || (fields >> extra))
            throw std::runtime_error(where
                + "expected KEY PICKING MIDI VELOCITY");
        Job job;
        job.key = key;
        if (!validKey(key))
            throw std::runtime_error(where + "key must be [A-Za-z0-9._-]: " + key);
        if (!keys.insert(key).second)
            throw std::runtime_error(where + "duplicate key " + key);
        const auto pickingIndex = findName(pickingNames, picking);
        if (!pickingIndex)
            throw std::runtime_error(where + "picking must be finger, pick or thumb");
        job.picking = static_cast<acustra::PickingTechnique>(*pickingIndex);
        if (!parseInt(midi, 0, 127, job.midi))
            throw std::runtime_error(where + "midi must be an integer 0-127");
        if (!parseInt(velocity, 1, 127, job.velocity))
            throw std::runtime_error(where + "velocity must be an integer 1-127");
        jobs.push_back(job);
    }
    if (input->bad())
        throw std::runtime_error("could not read job file " + path.string());
    if (jobs.empty())
        throw std::runtime_error("job file holds no renders");
    return jobs;
}

void prepareOutputDirectory(const std::filesystem::path& directory)
{
    // Several renderer processes share one directory, each with its own job
    // file, so an existing directory is accepted; a partial file is never
    // left under a model name (see renderJobs).
    std::error_code error;
    if (std::filesystem::is_directory(directory, error) && !error)
        return;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory, error))
        throw std::runtime_error("could not create output directory: "
            + directory.string());
}

std::size_t renderJobs(const std::vector<Job>& jobs,
                       const std::filesystem::path& directory,
                       const PhysicalCalibration& calibration)
{
    for (const auto& job : jobs)
    {
        const auto audio = renderModel(job.picking, job.midi,
                                       job.velocity, calibration);
        if (!finite(audio))
            throw std::runtime_error(job.key + " produced non-finite model audio");
        const auto path = directory / ("model-" + job.key + ".f32");
        auto partial = path;
        partial += ".partial";
        writeF32(partial, audio);
        std::error_code error;
        std::filesystem::rename(partial, path, error);
        if (error)
            throw std::runtime_error("could not move " + partial.string()
                + " into place");
    }
    return jobs.size();
}

void printUsage()
{
    std::printf(
        "usage: AcustraExternalCorpusRenderer "
        "[--shape parlor|auditorium|dreadnought|jumbo] "
        "[--body-material spruce|mahogany|maple] "
        "[--room 0..1] "
        "JOBFILE|- OUTDIR "
        "BODY_FREQUENCY BODY_Q BRIDGE_MOBILITY RESIDUE_TILT "
        "STEEL_STIFFNESS STEEL_T60 STEEL_FREQUENCY_LOSS STEEL_APERTURE "
        "STEEL_TRANSIENT STEEL_PLUCK_DISTANCE STEEL_VELOCITY_BRIGHTNESS "
        "APERTURE_REGISTER_EXPONENT LOW_BODY_MODE_GAIN "
        "STEEL_DISPLACEMENT_METRES STEEL_FRET_T60_SLOPE "
        "HIGH_LOSS_CUTOFF_SCALE BRIDGE_CONDUCTANCE_FLOOR "
        "BRIDGE_CONDUCTANCE_CORNER_HZ BRIDGE_TAIL_LENGTH_METRES "
        "PICK_RELEASE_VELOCITY_SHARE PICK_RELEASE_VELOCITY_EXPONENT "
        "PICK_EDGE_RADIUS_METRES "
        "STEEL_WOUND_FRICTION_LOSS STEEL_PLAIN_BENDING_LOSS\n"
        "Give all 24 calibration values (OptimizePhysicalModel.NAMES).\n"
        "JOBFILE lines: KEY finger|pick|thumb MIDI VELOCITY (steel strings); "
        "writes OUTDIR/model-KEY.f32 (48 kHz stereo float32, 4.2 s)\n");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2
        && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
    {
        printUsage();
        return 0;
    }

    // The same option order as PhysicalFitRenderer, without its corpus modes
    // and --archtop-picking (picking is per job row here).
    int first = 1;
    if (argc > first && std::string(argv[first]) == "--shape")
    {
        const auto name = std::find(shapeNames.begin(), shapeNames.end(),
            argc > first + 1 ? std::string(argv[first + 1]) : std::string());
        if (name == shapeNames.end())
        {
            printUsage();
            return 2;
        }
        renderShapeOverride = static_cast<acustra::BodyShape>(
            std::distance(shapeNames.begin(), name));
        first += 2;
    }
    if (argc > first && std::string(argv[first]) == "--body-material")
    {
        const auto name = std::find(bodyMaterialNames.begin(), bodyMaterialNames.end(),
            argc > first + 1 ? std::string(argv[first + 1]) : std::string());
        if (name == bodyMaterialNames.end())
        {
            printUsage();
            return 2;
        }
        renderBodyMaterial = static_cast<acustra::BodyMaterial>(
            std::distance(bodyMaterialNames.begin(), name));
        first += 2;
    }
    if (argc > first && std::string(argv[first]) == "--room")
    {
        float room = 0.0f;
        if (argc <= first + 1 || !parseFloat(argv[first + 1], room)
            || !(room >= 0.0f && room <= 1.0f))
        {
            printUsage();
            return 2;
        }
        renderRoom = room;
        first += 2;
    }
    const int given = argc - first - 2;
    if (given != static_cast<int>(calibrationValueCount))
    {
        printUsage();
        return 2;
    }

    CalibrationValues values {};
    const int valueArgument = first + 2;
    for (std::size_t index = 0; index < static_cast<std::size_t>(given); ++index)
    {
        const char* text = argv[valueArgument + static_cast<int>(index)];
        if (!parseFloat(text, values[index]))
        {
            std::fprintf(stderr, "invalid calibration value %zu: %s\n",
                         index + 1, text);
            return 2;
        }
        if (values[index] < calibrationMinimums[index]
            || values[index] > calibrationMaximums[index])
        {
            std::fprintf(stderr,
                         "calibration value %zu is outside [%g, %g]: %g\n",
                         index + 1,
                         static_cast<double>(calibrationMinimums[index]),
                         static_cast<double>(calibrationMaximums[index]),
                         static_cast<double>(values[index]));
            return 2;
        }
    }

    try
    {
        const auto jobs = readJobs(argv[first]);
        const std::filesystem::path directory(argv[first + 1]);
        if (directory.empty())
            throw std::runtime_error("output directory path is empty");
        prepareOutputDirectory(directory);
        const auto count = renderJobs(jobs, directory, makeCalibration(values));
        std::printf("{\"rendered\": %zu, \"calibration_values\": %s, "
                    "\"model_controls\": %s}\n",
                    count, calibrationJson(values).c_str(),
                    modelControlsJson().c_str());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "AcustraExternalCorpusRenderer: %s\n", error.what());
        return 1;
    }
}
