#include "DSP/AcustraEngine.h"
#include "DSP/GuitarModelData.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto body(AcustraEngine& e, float f, float m) { return e.renderBody(f, m); }
    static bool hasModel(const AcustraEngine& e, GuitarModel model)
    { return e.configuredGuitarModel_ == model; }
    template <typename Bank>
    static bool nominalBridgeMatches(const AcustraEngine& e, const Bank& bank)
    {
        // This inspects the actual configured load, including every unused
        // slot and the old conductance-floor slot. The raw qualified residues
        // must survive nominal model selection without a legacy gain or floor.
        for (std::size_t i = 0; i < e.bridgeLoad_.residueHeave.size(); ++i)
        {
            const auto expected = i < bank.size() ? bank[i] : detail::MeasuredBridgeMode {};
            if (e.bridgeLoad_.residueHeave[i] != expected.heave
                || e.bridgeLoad_.residueCross[i] != expected.cross
                || e.bridgeLoad_.residueRock[i] != expected.rock)
                return false;
        }
        return true;
    }
};
}
namespace
{
int failures = 0;
void expect(bool value, const char* message)
{
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

acustra::EngineParameters parametersFor(acustra::GuitarModel model)
{
    acustra::EngineParameters p;
    p.guitarModel = model;
    p.shape = acustra::BodyShape::Auditorium;
    if (model == acustra::GuitarModel::Bellido1978)
    { p.stringMaterial = acustra::StringMaterial::Nylon; p.bodyMaterial = acustra::BodyMaterial::Cedar; }
    return p;
}

template <typename Bank>
void testRadiation(acustra::GuitarModel model, const Bank& bank, int delay48)
{
    for (int rate : { 24000, 48000, 96000 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        engine->setParameters(parametersFor(model));
        engine->prepare(rate, 64);
        const double pi = std::acos(-1.0);
        double worst = 0, magnitude = 0;
        for (int n = 0; n < 768; ++n)
        {
            const auto actual = acustra::AcustraEngineTestAccess::body(*engine, n == 0 ? 1.f : 0.f, 0.f);
            std::complex<double> expected {};
            const int age = n - delay48 * rate / 48000;
            if (age >= 0)
                for (const auto& m : bank)
                {
                    if (m.frequency >= .46 * rate) continue;
                    const auto pole = std::exp(std::complex<double>(-pi * m.frequency / m.q, 2*pi*m.frequency) / double(rate));
                    const auto ref = std::exp(std::complex<double>(-pi * m.frequency / m.q, 2*pi*m.frequency) / 48000.);
                    const auto residue = std::complex<double>(m.leftReal, m.leftImaginary) * (pole - 1.) / (ref - 1.);
                    expected += residue * std::pow(pole, age);
                }
            const double target = 2 * expected.real()
                * acustra::detail::guitarMicrophoneTrims[static_cast<std::size_t>(model)];
            magnitude = std::max(magnitude, std::abs(target));
            worst = std::max(worst, std::abs(double(actual.left) - target));
            if (model != acustra::GuitarModel::Bellido1978)
                expect(actual.left == actual.right && actual.left == actual.upper,
                       "scalar measured radiation must not invent spatial channels");
        }
        std::cout << "radiation model=" << int(model) << " rate=" << rate << " relative max=" << worst / std::max(magnitude, 1e-9) << '\n';
        expect(worst / std::max(magnitude, 1e-9) < .002,
               "native delayed radiation differs from independent complex recurrence");
    }
}

std::array<std::complex<double>, 3> dtft(
    const std::vector<std::array<double, 3>>& impulse, double omega)
{
    // Goertzel evaluates the DTFT at arbitrary frequencies, independent of
    // the engine recurrence and delay interpolation. Retain its complex phase.
    const double coefficient = 2 * std::cos(omega);
    std::array<double, 3> previous {}, previous2 {};
    for (const auto& sample : impulse)
        for (std::size_t channel = 0; channel < previous.size(); ++channel)
        {
            const double current = sample[channel] + coefficient * previous[channel] - previous2[channel];
            previous2[channel] = previous[channel];
            previous[channel] = current;
        }
    const auto z = std::polar(1., -omega);
    const auto phase = std::polar(1., -omega * double(impulse.size() - 1));
    std::array<std::complex<double>, 3> result {};
    for (std::size_t channel = 0; channel < result.size(); ++channel)
        result[channel] = (previous[channel] - z * previous2[channel]) * phase;
    return result;
}

template <typename Bank>
void testFractionalRadiation(acustra::GuitarModel model, const Bank& bank, int delay48)
{
    const double pi = std::acos(-1.);
    std::vector<double> frequencies;
    for (int i = 0; i < 96; ++i)
        frequencies.push_back(60 * std::pow(10000. / 60., double(i) / 95.));
    // Equal-width high-frequency bands prevent strong bass resonances from
    // concealing fractional-delay treble loss in one broadband norm.
    for (int i = 0; i <= 64; ++i)
        frequencies.push_back(5000. + 5000. * i / 64.);
    for (const auto& mode : bank)
        if (mode.frequency >= 60 && mode.frequency <= 10000)
            frequencies.push_back(mode.frequency);
    for (const int rate : { 44100, 88200, 192000 })
        for (const int axis : { 0, 1 })
        {
            if (axis == 1 && model != acustra::GuitarModel::Bellido1978)
                continue;
            auto engine = std::make_unique<acustra::AcustraEngine>();
            engine->setParameters(parametersFor(model));
            engine->prepare(rate, 64);
            // 0.743 s exceeds nine time constants of the slowest retained
            // radiation pole; scale duration, not just sample count, at192k.
            const int length = static_cast<int>(std::ceil(32768. * rate / 44100.));
            std::vector<std::array<double, 3>> impulse(static_cast<std::size_t>(length));
            for (int n = 0; n < length; ++n)
            {
                const auto value = acustra::AcustraEngineTestAccess::body(
                    *engine, n == 0 && axis == 0 ? 1.f : 0.f,
                    n == 0 && axis == 1 ? 1.f : 0.f);
                impulse[static_cast<std::size_t>(n)] = { value.left, value.right, value.upper };
            }
            double totalError = 0, totalReference = 0;
            std::array<double, 15> bandError {}, bandReference {}, bandActual {};
            for (const double hz : frequencies)
            {
                if (hz >= .45 * rate) continue;
                const auto z = std::polar(1., -2 * pi * hz / rate);
                const auto actual = dtft(impulse, 2 * pi * hz / rate);
                std::array<std::complex<double>, 3> expected {};
                for (const auto& m : bank)
                {
                    if (m.frequency >= .46 * rate) continue;
                    const auto pole = std::exp(std::complex<double>(-pi * m.frequency / m.q, 2*pi*m.frequency) / double(rate));
                    const auto referencePole = std::exp(std::complex<double>(-pi * m.frequency / m.q, 2*pi*m.frequency) / 48000.);
                    const auto scale = (pole - 1.) / (referencePole - 1.);
                    const std::array<std::complex<double>, 3> residues = axis == 0
                        ? std::array<std::complex<double>, 3> { std::complex<double>(m.leftReal, m.leftImaginary),
                            { m.rightReal, m.rightImaginary }, { m.upperReal, m.upperImaginary } }
                        : std::array<std::complex<double>, 3> { std::complex<double>(m.leftMomentReal, m.leftMomentImaginary),
                            { m.rightMomentReal, m.rightMomentImaginary }, { m.upperMomentReal, m.upperMomentImaginary } };
                    for (std::size_t channel = 0; channel < expected.size(); ++channel)
                    {
                        const auto r = scale * residues[channel];
                        expected[channel] += r / (1. - pole * z)
                            + std::conj(r) / (1. - std::conj(pole) * z);
                    }
                }
                const auto idealDelay = std::polar(1., -2 * pi * hz * delay48 / 48000.)
                    * double(acustra::detail::guitarMicrophoneTrims[static_cast<std::size_t>(model)]);
                for (std::size_t channel = 0; channel < expected.size(); ++channel)
                {
                    expected[channel] *= idealDelay;
                    const double error = std::norm(actual[channel] - expected[channel]);
                    const double reference = std::norm(expected[channel]);
                    totalError += error;
                    totalReference += reference;
                    if (hz >= 5000)
                    {
                        const auto band = std::min(4, static_cast<int>((hz - 5000) / 1000));
                        const auto index = channel * 5 + static_cast<std::size_t>(band);
                        bandError[index] += error;
                        bandReference[index] += reference;
                        bandActual[index] += std::norm(actual[channel]);
                    }
                }
            }
            double worstBandComplex = 0, worstBandDb = 0;
            for (std::size_t band = 0; band < bandError.size(); ++band)
            {
                expect(bandReference[band] > 1e-16, "radiation reference band has no energy");
                worstBandComplex = std::max(worstBandComplex,
                    std::sqrt(bandError[band] / std::max(bandReference[band], 1e-30)));
                worstBandDb = std::max(worstBandDb,
                    std::abs(10 * std::log10(std::max(bandActual[band], 1e-30)
                        / std::max(bandReference[band], 1e-30))));
            }
            const double relative = std::sqrt(totalError / std::max(totalReference, 1e-30));
            std::cout << "fractional radiation model=" << int(model) << " rate=" << rate
                << " axis=" << axis << " complex=" << relative << " high-band complex="
                << worstBandComplex << " high-band dB=" << worstBandDb << '\n';
            expect(relative < .035 && worstBandComplex < .035 && worstBandDb < .35,
                   "host-rate radiation lost measured phase or treble through fractional delay");
        }
}

template <typename Bank>
void testNominalBridge(acustra::GuitarModel model, const Bank& bank)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->setParameters(parametersFor(model));
    engine->prepare(48000, 64);
    expect(acustra::AcustraEngineTestAccess::nominalBridgeMatches(*engine, bank),
           "a nominal measured bridge inherited legacy mobility gain or conductance floor");
}

// Values 2-4 were the Washburn 1897, Santa Cruz OM 2022 and Martin D18V 2007,
// whose source measurements carry no redistribution license. A host or file
// that still sends one must hear Original, sample for sample.
std::vector<float> renderChord(acustra::EngineParameters p)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->setParameters(p);
    engine->prepare(48000, 64);
    for (int note : { 40, 47, 52, 55, 59, 64 }) engine->noteOn(note, .8f);
    std::vector<float> audio;
    for (int n = 0; n < 24000; n += 64)
    {
        float l[64], r[64]; engine->process(l, r, 64);
        audio.insert(audio.end(), l, l + 64);
        audio.insert(audio.end(), r, r + 64);
    }
    return audio;
}

void testRetiredModelsPlayOriginal()
{
    for (const auto material : { acustra::StringMaterial::Steel, acustra::StringMaterial::Nylon })
    {
        auto p = parametersFor(acustra::GuitarModel::Original);
        p.stringMaterial = material;
        const auto original = renderChord(p);
        for (int retired : { 2, 3, 4 })
        {
            p.guitarModel = static_cast<acustra::GuitarModel>(retired);
            expect(renderChord(p) == original, "a retired guitar model did not play Original");
        }
    }
}

void testCoupledModels()
{
    for (int model = 0; model < 2; ++model)
        for (int rate : { 44100, 96000 })
        {
            auto engine = std::make_unique<acustra::AcustraEngine>();
            auto p = parametersFor(static_cast<acustra::GuitarModel>(model));
            engine->setParameters(p);
            engine->prepare(rate, 64);
            for (int note : { 40, 47, 52, 55, 59, 64 }) engine->noteOn(note, 1.f);
            double energy = 0, minimum = 0, audioEnergy = 0;
            float peak = 0;
            for (int n = 0; n < rate / 2; ++n)
            {
                float l, r; engine->process(&l, &r, 1);
                energy += engine->getLastBridgeBodyPower() / rate;
                minimum = std::min(minimum, energy);
                audioEnergy += double(l)*l + double(r)*r;
                peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
                expect(std::isfinite(energy) && std::isfinite(l) && std::isfinite(r), "coupled model must remain finite");
            }
            std::cout << "coupled model=" << model << " rate=" << rate << " minimum work=" << minimum << " peak=" << peak << '\n';
            expect(minimum >= -1e-14, "passive body must not generate port energy");
            expect(audioEnergy > 1e-9 && peak < .89f, "measured model must sound without relying on the limiter");
            // A host can move faster than the radiation fade. Verify the final
            // request wins, including its delay history, while a chord rings.
            // 4 and 3 are retired values, which coalesce as Original.
            for (int next : { 4, 1, 3, 1 })
            {
                p.guitarModel = static_cast<acustra::GuitarModel>(next);
                engine->setParameters(p);
                float l[64], r[64]; engine->process(l, r, 64);
            }
            for (int n = 0; n < rate / 8; n += 64)
            {
                float l[64], r[64]; engine->process(l, r, 64);
                for (float value : l) expect(std::isfinite(value) && std::abs(value) <= 1, "rapid model switching must remain bounded");
            }
            expect(acustra::AcustraEngineTestAccess::hasModel(*engine, acustra::GuitarModel::Bellido1978), "coalesced model change lost the last host request");
            engine->allSoundOff();
            float l[512], r[512]; engine->process(l, r, 512);
            for (int n = 0; n < 512; ++n) expect(l[n] == 0 && r[n] == 0, "all sound off must clear radiation delay too");
        }
}
}
int main()
{
    using namespace acustra;
    testRadiation(GuitarModel::Bellido1978, detail::bellidoBodyModes, 0);
    testFractionalRadiation(GuitarModel::Bellido1978, detail::bellidoBodyModes, 0);
    testNominalBridge(GuitarModel::Bellido1978, detail::bellidoBridgeModes);
    testRetiredModelsPlayOriginal();
    testCoupledModels();
    return failures == 0 ? 0 : 1;
}
