// Render AcustraEngine peers for rows of an external (non-bank) corpus.
// The job file holds one render per line, "KEY MATERIAL PICKING MIDI VELOCITY"
// (material steel|nylon, picking finger|pick|thumb, velocity 1-127); blank
// lines and lines starting with '#' are skipped. Each line is written to
// OUTDIR/model-KEY.f32: 4.2 s of 48 kHz stereo float32 little-endian.
//
// This is PhysicalFitRenderer's model half with the schedule read from a file
// instead of the embedded bank, so a score on an external corpus is a score of
// the same renders: the options, the 48-value calibration vector, its bounds
// and makeCalibration, and renderModel (a fresh engine per note, 48 kHz,
// 127-sample blocks, default public controls, nylon on the Auditorium slot
// unless --shape is given) are copied from Tools/PhysicalFitRenderer.cpp and
// must be kept identical to it. The one difference is that the picking tool
// comes from each job row rather than being Finger for the bank's plucked
// rows and --archtop-picking for its picked ones.
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
using acustra::StringMaterial;
using acustra::fittedPhysicalCalibration;

// One model per invocation. Without --shape each material renders the body
// its calibration was fitted on: steel the public default Dreadnought, nylon
// the Auditorium slot that is the measured classical (the Classical preset).
acustra::BridgeModel renderBridgeModel { acustra::BridgeModel::Original };
std::optional<acustra::BodyShape> renderShapeOverride;
constexpr std::array shapeNames { "parlor", "auditorium", "dreadnought", "jumbo" };
acustra::GuitarModel renderGuitarModel { acustra::GuitarModel::Original };
constexpr std::array guitarModelNames { "original", "bellido1978" };
constexpr std::array pickingNames { "finger", "pick", "thumb" };
constexpr std::array materialNames { "nylon", "steel" };

acustra::BodyShape renderShapeFor(acustra::StringMaterial material) noexcept
{
    if (renderShapeOverride)
        return *renderShapeOverride;
    return material == acustra::StringMaterial::Nylon
        ? acustra::BodyShape::Auditorium : acustra::EngineParameters {}.shape;
}

// Each Original bank at the wood it was built of (AcustraEngine::
// measuredBankWood), which Body Material leaves as measured: spruce for
// steel's g21, cedar for nylon's g34. A named model keeps the default wood,
// as its evaluations always have.
acustra::BodyMaterial renderWoodFor(acustra::StringMaterial material) noexcept
{
    if (renderGuitarModel != acustra::GuitarModel::Original)
        return acustra::EngineParameters {}.bodyMaterial;
    return acustra::AcustraEngine::measuredBankWood(material,
                                                    acustra::GuitarModel::Original);
}

constexpr int modelSampleRate = 48000;
constexpr int renderBlockSize = 127;
constexpr double renderSeconds = 4.2;
constexpr std::size_t calibrationValueCount = 48;
// The vector before the plectrum edge and the strings' bending loss were
// fitted values: a 32-value command line takes those five from
// fittedPhysicalCalibration, the values this build ships.
constexpr std::size_t legacyCalibrationValueCount = 32;
// The vector before the contact noise's eleven values: a 37-value command
// line takes them from fittedPhysicalCalibration too.
constexpr std::size_t bendingCalibrationValueCount = 37;

using CalibrationValues = std::array<float, calibrationValueCount>;

// Reject rather than silently clamp so the caller's CLI vector is the
// calibration that was actually rendered. These mirror AcustraEngine's bounds
// and PhysicalFitRenderer's copy of them.
constexpr CalibrationValues calibrationMinimums {{
    0.96f, 0.05f, 0.25f, -6.0f, 0.0f,
    0.4f, 0.35f, 0.35f, 0.0f, 0.7f, 0.0f,
    0.25f, 0.4f, 0.35f, 0.35f, 0.0f, 0.7f, 0.0f,
    -1.0f, 0.25f, 0.0f, -0.06f, 0.5f, 0.0f, 100.0f, 0.00325f,
    0.0f, 10.0f, 0.0f,
    0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 100.0f, 100.0f, 100.0f, 0.0005f,
    0.0f, 0.0f, 0.0f,
}};

constexpr CalibrationValues calibrationMaximums {{
    1.04f, 1.8f, 4.0f, 6.0f, 0.12f,
    2.0f, 3.0f, 2.5f, 3.0f, 3.0f, 1.2f,
    4.0f, 2.0f, 3.0f, 2.5f, 3.0f, 3.0f, 1.2f,
    1.0f, 32.0f, 0.04f, 0.05f, 4.0f, 0.02f, 8000.0f, 0.060f,
    0.5f, 400.0f, 0.82e-3f,
    2.0f, 4.0f, 8.0f,
    1.0e-3f, 2.0f, 2.0f, 2.0f, 2.0f,
    4.0f, 4.0f, 4.0f, 4.0f, 20000.0f, 20000.0f, 20000.0f, 0.05f,
    64.0f, 64.0f, 64.0f,
}};

struct Job
{
    std::string key;
    StringMaterial material {};
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
    calibration.directGain = values[4];

    const auto setMaterial = [&] (MaterialCalibration& material,
                                  std::size_t offset)
    {
        material.stiffnessScale = values[offset];
        material.fundamentalT60Scale = values[offset + 1];
        material.frequencyLossScale = values[offset + 2];
        material.apertureScale = values[offset + 3];
        material.transientScale = values[offset + 4];
        material.pluckDistanceScale = values[offset + 5];
        material.velocityBrightnessDepth = values[offset + 6];
    };
    // Nylon has no stiffnessScale in the calibration array (its bending
    // stiffness is Woodhouse's measured EI, not a fitted scale - see
    // nylonBendingEI in AcustraEngine.cpp), so it gets six values, not seven.
    calibration.nylon.fundamentalT60Scale = values[5];
    calibration.nylon.frequencyLossScale = values[6];
    calibration.nylon.apertureScale = values[7];
    calibration.nylon.transientScale = values[8];
    calibration.nylon.pluckDistanceScale = values[9];
    calibration.nylon.velocityBrightnessDepth = values[10];
    setMaterial(calibration.steel, 11);
    calibration.apertureRegisterExponent = values[18];
    calibration.lowBodyModeGain = values[19];
    calibration.steelDisplacementScaleMetres = values[20];
    calibration.steelFretT60Slope = values[21];
    calibration.highLossCutoffScale = values[22];
    calibration.bridgeConductanceFloor = values[23];
    calibration.bridgeConductanceCornerHz = values[24];
    calibration.bridgeTailLengthMetres = values[25];
    calibration.longitudinalGain = values[26];
    calibration.longitudinalQ = values[27];
    calibration.polarisationEndCorrectionMetres = values[28];
    calibration.pickReleaseVelocityShare = values[29];
    calibration.pickReleaseVelocityExponent = values[30];
    calibration.pickTransientGain = values[31];
    calibration.pickEdgeRadiusMetres = values[32];
    calibration.steelWoundBendingLoss = values[33];
    calibration.steelPlainBendingLoss = values[34];
    calibration.nylonWoundBendingLoss = values[35];
    calibration.nylonPlainBendingLoss = values[36];
    calibration.contactNoiseFinger = values[37];
    calibration.contactNoiseNylon = values[38];
    calibration.contactNoisePick = values[39];
    calibration.contactNoiseVelocityExponent = values[40];
    calibration.contactNoiseCornerHz = values[41];
    calibration.nylonContactNoiseCornerHz = values[42];
    calibration.pickContactNoiseCornerHz = values[43];
    calibration.contactNoiseDecaySeconds = values[44];
    calibration.contactClickFinger = values[45];
    calibration.contactClickNylon = values[46];
    calibration.contactClickPick = values[47];
    return calibration;
}

// PhysicalFitRenderer's renderModel with the picking tool taken from the job
// row; everything else is the same statement for statement.
std::vector<float> renderModel(StringMaterial material,
                               acustra::PickingTechnique picking,
                               int midi, int velocity,
                               const PhysicalCalibration& calibration)
{
    AcustraEngine engine;
    EngineParameters parameters;
    parameters.stringMaterial = material;
    parameters.bridgeModel = renderBridgeModel;
    parameters.guitarModel = renderGuitarModel;
    parameters.shape = renderShapeFor(parameters.stringMaterial);
    parameters.bodyMaterial = renderWoodFor(parameters.stringMaterial);
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
    text << "{\"shape\": \"";
    if (renderShapeOverride)
        text << shapeNames[static_cast<std::size_t>(*renderShapeOverride)];
    else
        text << "per material: "
             << shapeNames[static_cast<std::size_t>(
                    renderShapeFor(StringMaterial::Steel))]
             << " for steel, "
             << shapeNames[static_cast<std::size_t>(
                    renderShapeFor(StringMaterial::Nylon))]
             << " (the measured classical) for nylon";
    text << "\", \"body_material\": ";
    if (renderGuitarModel == acustra::GuitarModel::Original)
        text << "\"per material: " << static_cast<int>(renderWoodFor(StringMaterial::Steel))
             << " for steel, " << static_cast<int>(renderWoodFor(StringMaterial::Nylon))
             << " for nylon (each bank's own wood)\"";
    else
        text << static_cast<int>(parameters.bodyMaterial);
    text
         << ", \"string_material\": \"per job row: nylon or steel\""
         << ", \"bridge_model\": \""
         << (renderBridgeModel == acustra::BridgeModel::FyldeSteel ? "fylde" : "original")
         << "\""
         << ", \"guitar_model\": \""
         << guitarModelNames[static_cast<std::size_t>(renderGuitarModel)]
         << "\""
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
        std::string key, material, picking, midi, velocity, extra;
        fields >> key >> material >> picking >> midi >> velocity;
        const auto where = "job line " + std::to_string(number) + ": ";
        if (velocity.empty() || (fields >> extra))
            throw std::runtime_error(where
                + "expected KEY MATERIAL PICKING MIDI VELOCITY");
        Job job;
        job.key = key;
        if (!validKey(key))
            throw std::runtime_error(where + "key must be [A-Za-z0-9._-]: " + key);
        if (!keys.insert(key).second)
            throw std::runtime_error(where + "duplicate key " + key);
        const auto materialIndex = findName(materialNames, material);
        if (!materialIndex)
            throw std::runtime_error(where + "material must be steel or nylon");
        job.material = *materialIndex == 0 ? StringMaterial::Nylon
                                           : StringMaterial::Steel;
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
        const auto audio = renderModel(job.material, job.picking, job.midi,
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
        "[--bridge-model original|fylde] "
        "[--shape parlor|auditorium|dreadnought|jumbo] "
        "[--guitar-model original|bellido1978] "
        "JOBFILE|- OUTDIR "
        "BODY_FREQUENCY BODY_Q BRIDGE_MOBILITY RESIDUE_TILT DIRECT_GAIN "
        "NYLON_T60 NYLON_FREQUENCY_LOSS NYLON_APERTURE "
        "NYLON_TRANSIENT NYLON_PLUCK_DISTANCE NYLON_VELOCITY_BRIGHTNESS "
        "STEEL_STIFFNESS STEEL_T60 STEEL_FREQUENCY_LOSS STEEL_APERTURE "
        "STEEL_TRANSIENT STEEL_PLUCK_DISTANCE STEEL_VELOCITY_BRIGHTNESS "
        "APERTURE_REGISTER_EXPONENT LOW_BODY_MODE_GAIN "
        "STEEL_DISPLACEMENT_METRES STEEL_FRET_T60_SLOPE "
        "HIGH_LOSS_CUTOFF_SCALE BRIDGE_CONDUCTANCE_FLOOR "
        "BRIDGE_CONDUCTANCE_CORNER_HZ BRIDGE_TAIL_LENGTH_METRES "
        "LONGITUDINAL_GAIN LONGITUDINAL_Q "
        "POLARISATION_END_CORRECTION_METRES "
        "PICK_RELEASE_VELOCITY_SHARE PICK_RELEASE_VELOCITY_EXPONENT "
        "PICK_TRANSIENT_GAIN [PICK_EDGE_RADIUS_METRES "
        "STEEL_WOUND_BENDING_LOSS STEEL_PLAIN_BENDING_LOSS "
        "NYLON_WOUND_BENDING_LOSS NYLON_PLAIN_BENDING_LOSS "
        "[CONTACT_NOISE_FINGER CONTACT_NOISE_NYLON CONTACT_NOISE_PICK "
        "CONTACT_NOISE_VELOCITY_EXPONENT CONTACT_NOISE_CORNER_HZ "
        "NYLON_CONTACT_NOISE_CORNER_HZ PICK_CONTACT_NOISE_CORNER_HZ "
        "CONTACT_NOISE_DECAY_SECONDS CONTACT_CLICK_FINGER "
        "CONTACT_CLICK_NYLON CONTACT_CLICK_PICK]]\n"
        "Give 48 calibration values (OptimizePhysicalModel.NAMES), or the "
        "legacy 32 or 37; the values left out take the shipped calibration.\n"
        "JOBFILE lines: KEY steel|nylon finger|pick|thumb MIDI VELOCITY; "
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
    if (argc > first && std::string(argv[first]) == "--bridge-model")
    {
        if (argc <= first + 1
            || (std::string(argv[first + 1]) != "original"
                && std::string(argv[first + 1]) != "fylde"))
        {
            printUsage();
            return 2;
        }
        renderBridgeModel = std::string(argv[first + 1]) == "fylde"
            ? acustra::BridgeModel::FyldeSteel : acustra::BridgeModel::Original;
        first += 2;
    }
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
    if (argc > first && std::string(argv[first]) == "--guitar-model")
    {
        const auto name = std::find(guitarModelNames.begin(), guitarModelNames.end(),
            argc > first + 1 ? std::string(argv[first + 1]) : std::string());
        if (name == guitarModelNames.end())
        {
            printUsage();
            return 2;
        }
        renderGuitarModel = static_cast<acustra::GuitarModel>(
            std::distance(guitarModelNames.begin(), name));
        first += 2;
    }
    const int given = argc - first - 2;
    if (given != static_cast<int>(calibrationValueCount)
        && given != static_cast<int>(bendingCalibrationValueCount)
        && given != static_cast<int>(legacyCalibrationValueCount))
    {
        printUsage();
        return 2;
    }

    CalibrationValues values {};
    values[32] = fittedPhysicalCalibration.pickEdgeRadiusMetres;
    values[33] = fittedPhysicalCalibration.steelWoundBendingLoss;
    values[34] = fittedPhysicalCalibration.steelPlainBendingLoss;
    values[35] = fittedPhysicalCalibration.nylonWoundBendingLoss;
    values[36] = fittedPhysicalCalibration.nylonPlainBendingLoss;
    values[37] = fittedPhysicalCalibration.contactNoiseFinger;
    values[38] = fittedPhysicalCalibration.contactNoiseNylon;
    values[39] = fittedPhysicalCalibration.contactNoisePick;
    values[40] = fittedPhysicalCalibration.contactNoiseVelocityExponent;
    values[41] = fittedPhysicalCalibration.contactNoiseCornerHz;
    values[42] = fittedPhysicalCalibration.nylonContactNoiseCornerHz;
    values[43] = fittedPhysicalCalibration.pickContactNoiseCornerHz;
    values[44] = fittedPhysicalCalibration.contactNoiseDecaySeconds;
    values[45] = fittedPhysicalCalibration.contactClickFinger;
    values[46] = fittedPhysicalCalibration.contactClickNylon;
    values[47] = fittedPhysicalCalibration.contactClickPick;
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
