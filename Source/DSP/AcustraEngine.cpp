#include "AcustraEngine.h"
#include "GaussianApertureData.h"
#include "MeasuredBodyData.h"
#if defined(ACUSTRA_MEASURED_BRIDGE_DATA_HEADER)
#include ACUSTRA_MEASURED_BRIDGE_DATA_HEADER
#else
#include "MeasuredBridgeData.h"
#endif
#include "MeasuredJointBodyData.h"
#include "SteelBodyBlend.h"
#include "GuitarModelData.h"
#include "ConstructionLoudnessData.h"
#include "CaptureVoicingData.h"
#include "ModelConvergenceData.h"
#include "PlayerBodyLoading.h"
#include "PiezoBlampTable.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>

// Keeps a rarely taken path's body out of the per-sample loop that calls it,
// where inlined it would cost the loop even when it never runs.
#if defined(__GNUC__) || defined(__clang__)
#define ACUSTRA_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define ACUSTRA_NOINLINE __declspec(noinline)
#else
#define ACUSTRA_NOINLINE
#endif

namespace acustra
{
namespace
{
// C++17 stand-ins for std::span and std::numbers: the Rack Extension
// toolchain compiles this engine as C++17 against a libc++ without either.
// The constants are the same double values std::numbers gives.
constexpr double piDouble = 3.141592653589793238462643383279502884;
constexpr double sqrt2Double = 1.414213562373095048801688724209698079;

template <typename T>
class ConstSpan
{
public:
    template <std::size_t N>
    constexpr ConstSpan(const std::array<T, N>& values) noexcept
        : data_ { values.data() }, size_ { N } {}
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr const T& operator[](std::size_t index) const noexcept { return data_[index]; }
    constexpr const T* begin() const noexcept { return data_; }
    constexpr const T* end() const noexcept { return data_ + size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

private:
    const T* data_ {};
    std::size_t size_ {};
};

// The Rack Extension toolchain compiles with -ffreestanding -fno-builtin, so
// there std::abs, std::isfinite, std::floor, std::sqrt and std::copysign on a
// float or double are out-of-line library calls (fabsf, __fpclassifyf, a
// bit-serial software sqrtf, ...), several per body mode and string per
// sample. Each is an exactly specified IEEE operation - no rounding choice
// is left to the library - so the compiler builtins, which lower to the
// instruction, return the same bits on every toolchain. MSVC keeps std.
// Only real scalars are accepted: a complex or integer argument does not
// convert, so it cannot silently change meaning here.
namespace exact
{
#if defined(__clang__) || defined(__GNUC__)
inline float abs(float value) noexcept { return __builtin_fabsf(value); }
inline double abs(double value) noexcept { return __builtin_fabs(value); }
inline bool isfinite(float value) noexcept { return __builtin_isfinite(value); }
inline bool isfinite(double value) noexcept { return __builtin_isfinite(value); }
inline float floor(float value) noexcept { return __builtin_floorf(value); }
inline double floor(double value) noexcept { return __builtin_floor(value); }
inline float sqrt(float value) noexcept { return __builtin_sqrtf(value); }
inline double sqrt(double value) noexcept { return __builtin_sqrt(value); }
inline float copysign(float magnitude, float sign) noexcept
{
    return __builtin_copysignf(magnitude, sign);
}
#else
inline float abs(float value) noexcept { return std::abs(value); }
inline double abs(double value) noexcept { return std::abs(value); }
inline bool isfinite(float value) noexcept { return std::isfinite(value); }
inline bool isfinite(double value) noexcept { return std::isfinite(value); }
inline float floor(float value) noexcept { return std::floor(value); }
inline double floor(double value) noexcept { return std::floor(value); }
inline float sqrt(float value) noexcept { return std::sqrt(value); }
inline double sqrt(double value) noexcept { return std::sqrt(value); }
inline float copysign(float magnitude, float sign) noexcept
{
    return std::copysign(magnitude, sign);
}
#endif
// The bits of a float, for keys that must tell -0 from +0 and a NaN from
// itself; the builtin keeps the copy inline under -fno-builtin.
inline std::uint32_t bits(float value) noexcept
{
    std::uint32_t result;
#if defined(__clang__) || defined(__GNUC__)
    __builtin_memcpy(&result, &value, sizeof(result));
#else
    std::memcpy(&result, &value, sizeof(result));
#endif
    return result;
}
// std::complex<float> division. Under the Jukebox SDK its libc++ divides
// with logb/scalbn scaling through five out-of-line calls; divide() is that
// algorithm with the calls inlined - newlib's scalbnf replicated below, and
// logbf, fmaxf and fabsf, which are exact for the normal divisors it takes -
// and hands anything else (a non-finite part, a zero or subnormal divisor, a
// NaN result) back to the library, so every result is the library's own.
// Tests/cpu/SdkComplexDivisionFuzz.cpp in the Rack Extension checks it
// against the SDK's compiled code. Other toolchains keep std's division.
#if defined(JUKEBOX_SDK) && (defined(__clang__) || defined(__GNUC__))
inline float fromBits(std::uint32_t value) noexcept
{
    float result;
    __builtin_memcpy(&result, &value, sizeof(result));
    return result;
}

// newlib libm/common/sf_scalbn.c, which the SDK's libc links, bit for bit.
inline float sdkScalbn(float x, int n) noexcept
{
    constexpr float two25 = 3.355443200e+07f;
    constexpr float twom25 = 2.9802322388e-08f;
    constexpr float huge = 1.0e+30f;
    constexpr float tiny = 1.0e-30f;
    std::uint32_t ix = bits(x);
    int k = static_cast<int>((ix & 0x7f800000u) >> 23);
    if (k == 0)
    {
        if ((ix & 0x7fffffffu) == 0)
            return x;
        x *= two25;
        ix = bits(x);
        k = static_cast<int>((ix & 0x7f800000u) >> 23) - 25;
        if (n < -50000)
            return tiny * x;
    }
    if (k == 0xff)
        return x + x;
    k += n;
    if (k > 0xfe)
        return huge * __builtin_copysignf(huge, x);
    if (k > 0)
        return fromBits((ix & 0x807fffffu) | (static_cast<std::uint32_t>(k) << 23));
    // FLT_SMALLEST_EXP: below it newlib flushes to zero rather than round.
    if (k < -22)
        return n > 50000 ? huge * __builtin_copysignf(huge, x)
                         : tiny * __builtin_copysignf(tiny, x);
    k += 25;
    return fromBits((ix & 0x807fffffu) | (static_cast<std::uint32_t>(k) << 23)) * twom25;
}

inline std::complex<float> divide(const std::complex<float>& z,
                                  const std::complex<float>& w) noexcept
{
    const float a = z.real();
    const float b = z.imag();
    float c = w.real();
    float d = w.imag();
    const std::uint32_t scale = bits(__builtin_fmaxf(__builtin_fabsf(c), __builtin_fabsf(d)));
    if (!__builtin_isfinite(a) || !__builtin_isfinite(b) || !__builtin_isfinite(c)
        || !__builtin_isfinite(d) || scale < 0x00800000u || scale >= 0x7f800000u)
        return z / w;
    // logb of a normal float is its unbiased exponent.
    const int ilogbw = static_cast<int>(scale >> 23) - 127;
    c = sdkScalbn(c, -ilogbw);
    d = sdkScalbn(d, -ilogbw);
    const float denominator = c * c + d * d;
    const float x = sdkScalbn((a * c + b * d) / denominator, -ilogbw);
    const float y = sdkScalbn((b * c - a * d) / denominator, -ilogbw);
    if (__builtin_isnan(x) && __builtin_isnan(y))
        return z / w;
    return { x, y };
}
#else
inline std::complex<float> divide(const std::complex<float>& z,
                                  const std::complex<float>& w) noexcept
{
    return z / w;
}
#endif
template <typename T> void abs(T) = delete;
template <typename T> void isfinite(T) = delete;
template <typename T> void floor(T) = delete;
template <typename T> void sqrt(T) = delete;
} // namespace exact
} // namespace
} // namespace acustra


namespace acustra
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 2.0f * pi;
constexpr int localMaximumDelaySamples = 8192;
// Legacy output reference gain. The pair bank preserves raw measured complex
// phase; this gain is not an absolute-SPL calibration of the new bank. Any
// audition RMS match is applied after render.
constexpr float radiationReferenceGain = 18.0f;
// Releasing most of a steel pluck parallel to the top (initialisePluck) left
// the demos a median 5.41 dB quieter at the same controls, so the strings'
// reference rises by that much and a session keeps the loudness it had.
// Each construction and Picking is levelled on top of it
// (outputReferenceFor, ConstructionLoudnessData.h).
constexpr float stringsReference = 1.8637f;
// The share of a pluck's energy released normal to the soundboard at a Touch,
// with a pluck's own draw about it; initialisePluck says where it comes from.
float pluckNormalShare(float touch, float draw = 0.0f) noexcept
{
    return std::clamp(0.30f - 0.08f * touch + draw, 0.17f, 0.35f);
}
static_assert(detail::measuredSteelBodyModes.size() <= ACUSTRA_BODY_MODE_COUNT);
static_assert(detail::measuredSteelBridgeModes.size()
              <= ACUSTRA_BRIDGE_MODE_COUNT);

static_assert(detail::bellidoBodyModes.size() <= ACUSTRA_BODY_MODE_COUNT);
static_assert(detail::bellidoBridgeModes.size() <= ACUSTRA_BRIDGE_MODE_COUNT);

// The measured body is one guitar of one size, so a Shape is a morph of that
// measurement, not a second measurement. The Original's anchor, in the
// Dreadnought slot, is the wider authored box a blind listener chose
// (wideSteelAnchorTransform below). The other shapes are placed relative to
// the anchor by the coupled model below. A named guitar is its own
// measurement at its own box, so its anchor is the identity.
struct AnchorTransform
{
    // A ratio written in Hz, not the air mode itself: the measured bank's
    // modes between 85 and 145 Hz move by airHz / 107 (107 is the identity),
    // so the wide anchor's 98 lowers them by 8%.
    float airHz;
    float modeScale;
    float bass;
    float volume;
    float asymmetry;
};

constexpr AnchorTransform measuredAnchorTransform { 107.0f, 1.0f, 1.0f,
                                                    1.0f, 0.0f };
// Steel's Dreadnought anchor is the wider box the local line authored: a
// lower air mode, the plate modes lower and more bass. The benchmark split on
// it (steel training -0.5%, the never-fitted flat-top rows -5.2%,
// development validation +3.2%) and a blind listener chose it on all four
// steel pairs (Docs/decisions.md, 2026-09-24), so it is chosen by ear.
constexpr AnchorTransform wideSteelAnchorTransform { 98.0f, 0.900f, 1.28f,
                                                     0.93f, 0.018f };

// Body outline and cavity, in metres: lower-bout width, body length, mean
// depth, soundhole diameter, and the fraction of the width-by-length
// rectangle the outline fills (about 0.72 for a waisted guitar plantilla,
// 0.75 for a dreadnought's squarer shoulders). Manufacturer set-up sheets
// publish the first three for each body size:
//   Parlor      Martin Size 0 (13 1/2 x 18 3/8 x 4 1/4 in), the class the
//               Fender PS-220E belongs to
//   Auditorium  Martin 000 "Auditorium" (15 x 19 3/8 x 4 1/8 in); Taylor's
//               Grand Auditorium is a larger take on the same name
//   Dreadnought Martin D-28 (15 5/8 x 20 x 3 7/8 to 4 7/8 in)
//   Jumbo       Gibson SJ-200 (17 x 21 x 4 7/8 in)
// with the 4 in soundhole a steel-string flat-top carries. The classical is
// the Torres-derived plantilla the Bellido follows: 370 mm lower bout,
// 490 mm body, 95 mm mean depth and an 87 mm soundhole. The outline
// fraction is an estimate read off those plantillas, not a published figure.
struct BodyGeometry
{
    float width;
    float length;
    float depth;
    float soundhole;
    float outline;

    constexpr float topArea() const noexcept { return outline * width * length; }
    constexpr float volume() const noexcept { return topArea() * depth; }
};

constexpr std::array<BodyGeometry, 4> steelStringBodies {{
    { 0.343f, 0.467f, 0.108f, 0.1016f, 0.72f }, // Parlor
    { 0.381f, 0.492f, 0.105f, 0.1016f, 0.72f }, // Auditorium
    { 0.397f, 0.508f, 0.111f, 0.1016f, 0.75f }, // Dreadnought
    { 0.432f, 0.533f, 0.124f, 0.1016f, 0.72f }  // Jumbo
}};
constexpr BodyGeometry classicalBody { 0.370f, 0.490f, 0.095f, 0.087f, 0.72f };

// Christensen and Vistisen, "Simple model for low-frequency guitar
// function", J. Acoust. Soc. Am. 68(3) (1980) 758-766: the top plate is one
// piston of effective area A_p, mass m_p and stiffness k_p, the soundhole air
// a plug of area S and mass m_h, and the cavity of volume V the spring
// mu = rho c^2 / V that couples them. In volume-displacement coordinates
// q_p = A_p x_p and q_h = S x_h, divided through by mu, the system is
//     M = diag(1/wa^2, 1/wh^2),  K = [[wp0^2/wa^2 + 1, 1], [1, 1]],
// with wp0^2 = k_p/m_p the plate alone, wa^2 = mu A_p^2/m_p the cavity spring
// on the plate and wh^2 = mu S^2/m_h the Helmholtz resonance of the rigid
// box. Its two modes are the guitar's A0 and T1, and they obey
//     w-^2 + w+^2 = wp0^2 + wa^2 + wh^2,   w-^2 w+^2 = wp0^2 wh^2,
// so a measured A0/T1 pair plus the box's own Helmholtz frequency identify
// the plate's two frequencies, and a different box then gives a different
// pair. The radiated monopole is the volume velocity q_p' + q_h' for a unit
// bridge force, whose modal residues follow from the same eigenvectors; the
// force enters as 1/(mu A_p) = V/(rho c^2 A_p), so a common rho c^2 cancels
// in every ratio taken here.
struct LowBodyPair
{
    float a0Frequency;
    float t1Frequency;
    // |residue| times frequency of each mode in the pressure-per-force
    // response, which is what scales a discrete pole pair's residue; see
    // configureBody. Both carry the common 1/(rho c^2) already dropped.
    float a0Weight;
    float t1Weight;
};

float helmholtzFrequency(const BodyGeometry& body) noexcept
{
    // Rigid-walled Helmholtz resonance with Rayleigh's flanged-end correction
    // of 0.85 r at each face of a 3 mm top.
    constexpr float soundSpeed = 343.0f;
    const float radius = 0.5f * body.soundhole;
    const float area = pi * radius * radius;
    const float neck = 0.003f + 1.7f * radius;
    return soundSpeed / twoPi
        * exact::sqrt(area / (body.volume() * neck));
}

LowBodyPair coupledLowBodyPair(float plateFrequency, float cavitySpringFrequency,
                               float helmholtz, float depth) noexcept
{
    const double wp0 = twoPi * plateFrequency;
    const double wa = twoPi * cavitySpringFrequency;
    const double wh = twoPi * helmholtz;
    const double sum = wp0 * wp0 + wa * wa + wh * wh;
    const double product = wp0 * wp0 * wh * wh;
    const double discriminant = exact::sqrt(std::max(sum * sum - 4.0 * product, 0.0));
    const double lambdas[] { 0.5 * (sum - discriminant), 0.5 * (sum + discriminant) };
    LowBodyPair pair {};
    float* frequencies[] { &pair.a0Frequency, &pair.t1Frequency };
    float* weights[] { &pair.a0Weight, &pair.t1Weight };
    for (int mode = 0; mode < 2; ++mode)
    {
        const double lambda = lambdas[mode];
        // Second row of (K - lambda M) phi = 0 with the hole part taken as 1:
        // phi_p + (1 - lambda/wh^2) phi_h = 0, then mass-normalise.
        const double platePart = -(1.0 - lambda / (wh * wh));
        const double massNorm = exact::sqrt(platePart * platePart / (wa * wa)
                                          + 1.0 / (wh * wh));
        const double phiPlate = platePart / massNorm;
        const double phiHole = 1.0 / massNorm;
        // Force enters on the plate coordinate as V/A_p = depth; the output is
        // the sum of both volume velocities.
        const double residue = (phiPlate + phiHole) * phiPlate * depth;
        const double frequency = exact::sqrt(lambda) / twoPi;
        *frequencies[mode] = static_cast<float>(frequency);
        *weights[mode] = static_cast<float>(exact::abs(residue) * frequency);
    }
    return pair;
}

// What one Shape does to the anchor bank: the A0 group (every mode below
// 150 Hz) and T1 are retuned and reweighted by the coupled pair, and every
// plate mode above T1 follows the equal-thickness plate law f ~ 1/A_p with
// its radiation scaled by the plate area it radiates from. Every factor is
// exactly 1 for the anchor shape, which is what keeps it bit-identical.
struct BodyShapeMorph
{
    int t1Index { -1 };
    // Where a bridge bank's T1 group ends when the same factors are applied
    // to it: half way (on a log scale) from the measured radiation T1 to the
    // next radiation mode, so a bridge mode takes the class of the radiation
    // mode it sits beside. At T1 itself, the Bellido's bridge T1 (216.4 Hz,
    // over its radiation T1 at 212.2 Hz) took the plate factor and landed a
    // semitone from the radiation T1 under every Shape but its own.
    float t1UpperHz { 0.0f };
    float a0Frequency { 1.0f };
    float a0Level { 1.0f };
    float t1Frequency { 1.0f };
    float t1Level { 1.0f };
    float plateFrequency { 1.0f };
    float plateLevel { 1.0f };
};

constexpr float lowBodyGroupUpperHz = 150.0f;

BodyShapeMorph bodyShapeMorph(ConstSpan<detail::MeasuredBodyMode> bank,
                              const AnchorTransform& anchor,
                              const BodyGeometry& anchorBody,
                              const BodyGeometry& body) noexcept
{
    BodyShapeMorph morph;
    // A0 is the strongest radiating mode below 150 Hz, T1 the strongest
    // between there and 260 Hz, read from the measured force paths.
    const auto weight = [] (const detail::MeasuredBodyMode& mode)
    {
        return std::hypot(mode.leftReal, mode.leftImaginary)
             + std::hypot(mode.rightReal, mode.rightImaginary)
             + std::hypot(mode.upperReal, mode.upperImaginary);
    };
    int a0Index = -1;
    for (int index = 0; index < static_cast<int>(bank.size()); ++index)
    {
        const auto& mode = bank[static_cast<std::size_t>(index)];
        if (mode.frequency < lowBodyGroupUpperHz)
        {
            if (a0Index < 0 || weight(mode) > weight(bank[static_cast<std::size_t>(a0Index)]))
                a0Index = index;
        }
        else if (mode.frequency < 260.0f)
        {
            if (morph.t1Index < 0
                || weight(mode) > weight(bank[static_cast<std::size_t>(morph.t1Index)]))
                morph.t1Index = index;
        }
    }
    if (morph.t1Index >= 0)
    {
        const auto t1 = static_cast<std::size_t>(morph.t1Index);
        morph.t1UpperHz = t1 + 1 < bank.size()
            ? exact::sqrt(bank[t1].frequency * bank[t1 + 1].frequency)
            : bank[t1].frequency;
    }
    const bool sameBox = body.width == anchorBody.width
        && body.length == anchorBody.length && body.depth == anchorBody.depth
        && body.soundhole == anchorBody.soundhole
        && body.outline == anchorBody.outline;
    if (a0Index < 0 || morph.t1Index < 0 || sameBox)
        return morph;

    // The anchor's own pair, as the anchor transform leaves it.
    const auto anchored = [&] (int index)
    {
        const auto& mode = bank[static_cast<std::size_t>(index)];
        const float alternating = (index & 1) == 0 ? 1.0f : -1.0f;
        const bool lowBodyMode = mode.frequency > 85.0f && mode.frequency < 145.0f;
        return mode.frequency * (lowBodyMode ? anchor.airHz / 107.0f : anchor.modeScale)
            * (1.0f + alternating * anchor.asymmetry
               / exact::sqrt(static_cast<float>(index + 1)));
    };
    const float a0 = anchored(a0Index);
    const float t1 = anchored(morph.t1Index);
    const float anchorHelmholtz = helmholtzFrequency(anchorBody);
    // Invert the sum and product identities for the plate's two frequencies.
    const float plate = a0 * t1 / anchorHelmholtz;
    const float cavitySpringSquared = a0 * a0 + t1 * t1 - plate * plate
                                    - anchorHelmholtz * anchorHelmholtz;
    if (!(cavitySpringSquared > 0.0f))
        return morph;
    const float cavitySpring = exact::sqrt(cavitySpringSquared);
    const float anchorDepth = anchorBody.volume() / anchorBody.topArea();
    const auto reference = coupledLowBodyPair(plate, cavitySpring,
                                              anchorHelmholtz, anchorDepth);

    // The target box: the plate keeps its thickness, so its frequencies go as
    // 1/A_p and its mass as A_p; the cavity spring on it, mu A_p^2 / m_p, then
    // goes as A_p / V.
    const float areaRatio = body.topArea() / anchorBody.topArea();
    const float targetPlate = plate / areaRatio;
    const float targetSpring = cavitySpring
        * exact::sqrt((body.topArea() / body.volume())
                    / (anchorBody.topArea() / anchorBody.volume()));
    const auto target = coupledLowBodyPair(targetPlate, targetSpring,
                                           helmholtzFrequency(body),
                                           body.volume() / body.topArea());
    morph.a0Frequency = target.a0Frequency / reference.a0Frequency;
    morph.t1Frequency = target.t1Frequency / reference.t1Frequency;
    morph.a0Level = target.a0Weight / reference.a0Weight;
    morph.t1Level = target.t1Weight / reference.t1Weight;
    morph.plateFrequency = 1.0f / areaRatio;
    morph.plateLevel = areaRatio;
    return morph;
}

// The Shape slot each bank is heard unwarped in: the Dreadnought for
// Original, and the Bellido's own family.
BodyShape anchorShapeFor(GuitarModel model) noexcept
{
    return model == GuitarModel::Bellido1978 ? BodyShape::Auditorium
                                             : BodyShape::Dreadnought;
}

// The box each anchor describes: the dreadnought the Original is, and the
// classical guitar the Bellido was measured on.
const BodyGeometry& anchorBodyFor(GuitarModel model) noexcept
{
    if (model == GuitarModel::Bellido1978)
        return classicalBody;
    return steelStringBodies[static_cast<std::size_t>(BodyShape::Dreadnought)];
}

// The box a Shape asks for; its anchor slot is the anchor's own box exactly,
// so every morph factor is 1 there.
const BodyGeometry& targetBodyFor(GuitarModel model, BodyShape shape) noexcept
{
    if (shape == anchorShapeFor(model))
        return anchorBodyFor(model);
    return steelStringBodies[static_cast<std::size_t>(shape)];
}

const AnchorTransform& anchorTransformFor(GuitarModel model) noexcept
{
    return model == GuitarModel::Original ? wideSteelAnchorTransform
                                          : measuredAnchorTransform;
}

// The statistical continuation of the radiation above a measured bank's
// fitted band (configureBody): a 1/16-octave grid at unit modal overlap
// (Q = 1/(2^(1/16) - 1), 22.6), to 18 kHz, at -6 dB per octave.
constexpr float radiationContinuationStepsPerOctave = 16.0f;
constexpr float radiationContinuationTopHz = 18000.0f;
constexpr float radiationContinuationDbPerOctave = -6.0f;
// Its slots: AcustraEngine::radiationContinuationSlots.

struct WoodSpec
{
    float frequencyScale;
    float qScale;
    float brightness;
    float radiation;
};

// Effective plate directions, not species-identification claims.  Density,
// stiffness and loss do not reduce to a single "tonewood" number; keeping
// three bounded directions, as far apart as the set allows, is more honest
// than attaching exact woods to unsupported impulse responses.  The
// relationship f ~ sqrt(E/rho) and modal loss scaling are the only laws used
// here.
constexpr std::array<WoodSpec, 3> woodSpecs {{
    { 1.000f, 1.00f, 1.00f, 1.00f }, // spruce reference
    { 0.991f, 0.82f, 0.87f, 1.02f }, // mahogany: lossier/warmer direction
    { 1.025f, 1.08f, 1.08f, 0.96f }  // maple: stiffer/brighter direction
}};

// The wood each measured bank was built of: g21 (MeasuredBridgeData.h) is
// spruce/cypress, the Bellido's g35 (BellidoData.h) cedar/Rio palisander.
// Cedar is not a Body Material choice; it is only the Bellido's reference,
// its softer and more damped direction.
constexpr WoodSpec cedarSpec { 0.982f, 0.88f, 0.93f, 1.04f };
constexpr WoodSpec measuredBankWood(GuitarModel guitar) noexcept
{
    return guitar == GuitarModel::Bellido1978 ? cedarSpec : woodSpecs[0];
}

// What Wood does to a mode's frequency, Q, brightness and radiation,
// relative to the wood the measured bank was built of (measuredBankWood), so
// every bank is heard as measured at its own wood. Every factor is then
// exactly 1 there (x/x in IEEE arithmetic).
struct WoodFactors
{
    float frequency;
    float q;
    float brightness;
    float radiation;
};

WoodFactors woodFactorsFor(BodyMaterial material, GuitarModel guitar) noexcept
{
    const auto wood = woodSpecs[static_cast<std::size_t>(material)];
    const auto reference = measuredBankWood(guitar);
    return { wood.frequencyScale / reference.frequencyScale,
             wood.qScale / reference.qScale,
             wood.brightness / reference.brightness,
             wood.radiation / reference.radiation };
}

struct ModalPole
{
    float frequency;
    float q;
};

// One radiation mode's engine pole: the anchor transform, the Shape morph's
// class factor, Wood and the calibration applied to the measured frequency
// and Q. configureBody sounds it (clamping the frequency to the host rate);
// steel's own bridge takes it for each bridge mode that is the same
// resonance (steelOwnBridgePole below).
// `index` is the mode's place in the bank it was fitted in, which the
// anchor's alternating detune and the Shape morph's T1 group read.
ModalPole radiationPole(const detail::MeasuredBodyMode& measured, int index,
                        const AnchorTransform& anchor,
                        const BodyShapeMorph& morph, WoodFactors wood,
                        bool named,
                        const PhysicalCalibration& calibration) noexcept
{
    const float alternating = (index & 1) == 0 ? 1.0f : -1.0f;
    const bool lowBodyMode = measured.frequency > 85.0f
        && measured.frequency < 145.0f;
    const float lowModeMorph = lowBodyMode
        ? anchor.airHz / 107.0f : anchor.modeScale;
    // The A0 group, T1 and the plate modes above it each take their own
    // factor from the coupled pair; the anchor shape's are exactly 1.
    const float shapeFrequency = measured.frequency < lowBodyGroupUpperHz
        ? morph.a0Frequency
        : index <= morph.t1Index ? morph.t1Frequency : morph.plateFrequency;
    const float frequency = measured.frequency * lowModeMorph * shapeFrequency
        * wood.frequency * calibration.bodyFrequencyScale
        * (1.0f + alternating * anchor.asymmetry
           / exact::sqrt(static_cast<float>(index + 1)));
    // AcustraEngine::clamp: a non-finite Q takes the lower bound.
    // A modest A0 decay extension on the Original, shared with its bridge
    // twin below. At the default construction its free radiation T60 rises
    // from 0.50 to 0.61 s; the loaded note's audible low component rises
    // from 0.45 to 0.50 s, towards the fingered Eastman and Martin recordings.
    // Frequency, residues and the listener's spring-back share stay fixed.
    // The low-frequency joint-body component takes the same ratio so the
    // blend retains its own relative damping. Higher modes and Bellido do not.
    // See Docs/realism-consolidation-2026-10-02.md for the measured bracket.
    constexpr float airQScale = 23.5f / 19.311932f;
    const float decayScale = !named && measured.frequency > 85.0f
        && measured.frequency < 105.0f ? airQScale : 1.0f;
    const float q = measured.q * wood.q * calibration.bodyQScale * decayScale;
    const float low = named ? 1.0f : 4.0f;
    return { frequency, exact::isfinite(q) ? std::max(low, std::min(150.0f, q))
                                           : low };
}

ModalPole radiationPole(ConstSpan<detail::MeasuredBodyMode> bank, int index,
                        const AnchorTransform& anchor,
                        const BodyShapeMorph& morph, WoodFactors wood,
                        bool named,
                        const PhysicalCalibration& calibration) noexcept
{
    return radiationPole(bank[static_cast<std::size_t>(index)], index, anchor,
                         morph, wood, named, calibration);
}

float safetyLimit(float sample) noexcept
{
    // Exactly linear through -1 dBFS, then C1-continuous into unit headroom.
    // The former zero-centred knee altered every ordinary guitar transient.
    constexpr float threshold = 0.89125094f;
    constexpr float headroom = 1.0f - threshold;
    const float magnitude = exact::abs(sample);
    if (magnitude <= threshold)
        return sample;
    const float excess = magnitude - threshold;
    const float limited = threshold
        + excess / (1.0f + excess / headroom);
    return exact::copysign(limited, sample);
}

// D'Addario EJ16 published tensions, low E to high E, converted from pounds
// force.  Wound-string mass cannot be inferred from its outside diameter as a
// solid steel cylinder; tension and scale length give the effective linear
// mass that the waveguide actually needs.
constexpr std::array<float, AcustraEngine::stringCount> steelTensionNewtons {{
    110.759f, 128.554f, 133.002f, 133.892f, 103.643f, 104.088f
}};

constexpr std::array<int, AcustraEngine::stringCount> standardOpenMidi {{
    40, 45, 50, 55, 59, 64
}};

// Wound-string bending is governed mostly by a smaller core than the outside
// diameter.  The low E/A/D effective diameters reproduce the measured open-
// string inharmonicities 1.08e-4, 6.6e-5 and 5.0e-5 reported by Jarvelainen
// and Karjalainen (Acta Acustica 92, 2006); shortening the same construction
// then predicts their seventh-fret values.  G remains an authored effective
// diameter because that paper does not publish a matching value for it. The
// shipped fit then scales every B by stiffnessScale (0.749,
// FittedPhysicalData.h), so the engine's open strings sit at 0.75 of them.
constexpr std::array<float, AcustraEngine::stringCount> steelBendingDiameter {{
    0.477159e-3f, 0.437895e-3f, 0.412021e-3f,
    0.38e-3f, 0.406e-3f, 0.305e-3f
}};

constexpr float steelYoungsModulus = 2.0e11f;

float stringImpedance(int stringIndex, int openMidi) noexcept
{
    const auto index = static_cast<std::size_t>(stringIndex);
    const float frequency = 440.0f
        * std::exp2((static_cast<float>(openMidi) - 69.0f) / 12.0f);
    constexpr float length = 0.648f;
    const float waveSpeed = 2.0f * length * frequency;
    const float standardFrequency = 440.0f * std::exp2(
        (static_cast<float>(standardOpenMidi[index]) - 69.0f) / 12.0f);
    const float standardWaveSpeed = 2.0f * length * standardFrequency;
    if (openMidi == standardOpenMidi[index])
        return steelTensionNewtons[index] / standardWaveSpeed;
    const float linearMass = steelTensionNewtons[index]
                           / (standardWaveSpeed * standardWaveSpeed);
    return linearMass * waveSpeed;
}

// Axial rigidity E*A, in newtons: what a bend works against. It uses the
// same effective core the bending and attack-pitch models use, since a wound
// string's wrap carries almost no axial load.
float stringAxialRigidity(int stringIndex) noexcept
{
    const float diameter = steelBendingDiameter[static_cast<std::size_t>(stringIndex)];
    return steelYoungsModulus * 0.25f * pi * diameter * diameter;
}

// The tension a requested interval needs, from Grimes, PLoS ONE 9(7):e102088
// (2014), Eq. 6. His bent string stretches by e = 1/cos(theta) - 1, which is
// the strain dT/EA, so it carries tension T = T0 + dT, its mass per length
// falls to mu0/(1+e), and the path it vibrates along grows to L0(1+e). Those
// last two together are the mu0(1+e) his Eq. 6 puts under the root at the
// unstretched length: f = (1/2L0) sqrt(T/(mu0(1+e))), so the ratio to the
// open string is f/f0 = sqrt((T/T0)/(1+e)). Requiring that ratio to be r
// inverts to dT = T0 (r^2 - 1) / (1 - r^2 T0/EA). Validated in that paper on
// measured Ernie Ball sets, with plain steel at 177.6-188.4 GPa against the
// 200 GPa this engine's table uses.
// A finger pushes a string only so far: plain steel parts at roughly 1.7 to
// 2 times its tuning tension, and real bends stay within 3 to 4 semitones.
// So the tension a bend asks for stops at twice the open string's r^2 - six
// semitones up, about twice the tuning tension - and a member glide wider
// than that carries on as a slide, in the delay alone, instead of driving
// the junction port with a tension no string survives (at the old limit,
// half way to Grimes' singularity below, a +48 glide reached 170 times the
// tuning tension and 25 dB).
constexpr float maximumBendRatioSquared = 2.0f;

float bentStringTension(float tension, float axialRigidity,
                        float frequencyRatio) noexcept
{
    // The denominator vanishes where the string's own extension would eat the
    // whole of the added tension - a string past breaking, not a bend. A
    // string too stretchy to reach the limit above first (none of this data
    // is) stops half way to that. Past either the pitch still follows the
    // wheel through the delay, as a slide does.
    const float ratioSquared = std::min({ frequencyRatio * frequencyRatio,
                                          0.5f * axialRigidity / tension,
                                          maximumBendRatioSquared });
    const float added = tension * (ratioSquared - 1.0f)
                      / (1.0f - ratioSquared * tension / axialRigidity);
    // Grimes' law describes a string in tension; a slackened one leaves it.
    return std::max(tension + added, 0.05f * tension);
}

// Every construction and Picking plays at the default construction's
// loudness (Docs/decisions.md, 2026-09-29, "Every construction as loud as
// the default"): a construction's cell in ConstructionLoudnessData.h, which
// Tools/CalibrateConstructionLoudness.py measures and writes.
std::size_t constructionLoudnessCell(const EngineParameters& parameters) noexcept
{
    const auto place = [] (auto value, int count) noexcept
    {
        return std::clamp(static_cast<int>(value), 0, count - 1);
    };
    int cell = place(parameters.guitarModel, 2);
    cell = cell * 4 + place(parameters.shape, 4);
    cell = cell * 3 + place(parameters.bodyMaterial, 3);
    cell = cell * 3 + place(parameters.picking, 3);
    return static_cast<std::size_t>(cell);
}
static_assert(detail::constructionMicReference.size() == 2 * 4 * 3 * 3);

// The output reference: the strings', times the construction's level
// (exactly 1 for the default construction, which renders unchanged).
float outputReferenceFor(const EngineParameters& parameters) noexcept
{
    return stringsReference
        * detail::constructionMicReference[constructionLoudnessCell(parameters)];
}

// The mono microphone's own reference: the stereo microphones' times its
// factor over them, so it meets the same target on every construction. It
// is smoothed as a whole, so a change glides along one curve.
float monoReferenceFor(const EngineParameters& parameters) noexcept
{
    return outputReferenceFor(parameters)
        * detail::constructionMonoTrim[constructionLoudnessCell(parameters)];
}

// The piezo's reference before its trim (PiezoDesign::trim):
// the stereo microphones' times its factor over them.
float piezoReferenceFor(const EngineParameters& parameters) noexcept
{
    return outputReferenceFor(parameters)
        * detail::constructionPiezoTrim[constructionLoudnessCell(parameters)];
}

// The Original voice adapts Mores g21, a flamenco guitar, brought to
// steel-string mobility. The Bellido plays its own measured bridge (see
// configureBridge for its mobility with steel).
ConstSpan<detail::MeasuredBridgeMode> measuredBridgeBank(GuitarModel guitar) noexcept
{
    if (guitar == GuitarModel::Bellido1978)
        return detail::bellidoBridgeModes;
    return detail::measuredSteelBridgeModes;
}

ConstSpan<detail::MeasuredBodyMode> measuredBodyBank(GuitarModel guitar) noexcept
{
    if (guitar == GuitarModel::Bellido1978)
        return detail::bellidoBodyModes;
    return detail::measuredSteelBodyModes;
}

// The same coupled-model factors the radiation takes, applied to a bridge
// bank: its A0 group, its modes up to T1, and the plate modes above follow
// the body they belong to. Only modal stiffness moves: each residue matrix,
// and with it the positive-semidefinite heave/rock coupling, and each Q are
// retained, so a fixed shape keeps the passive modal construction. At the
// anchor every factor is exactly 1.
detail::MeasuredBridgeMode shapeBridgeMode(
    detail::MeasuredBridgeMode mode, float a0Frequency, float t1Frequency,
    float plateFrequency, float t1UpperHz) noexcept
{
    mode.frequency *= mode.frequency < lowBodyGroupUpperHz ? a0Frequency
        : mode.frequency <= t1UpperHz ? t1Frequency : plateFrequency;
    return mode;
}

// Steel's own bridge is g21's, the guitar its radiation bank is, so one body
// loads the string and radiates it: a modal body's mode k carries both the
// mobility residue phi_k(bridge)^2/m_k and the radiation residue
// phi_k(bridge) psi_k(mic)/m_k on the same pole. Only on the Original; the
// Bellido follows its radiation by the lighter rule of bellidoBridgePole
// below.

// The engine pole of steel's own bridge mode `index` (see steelOwnBridge).
// A mode that is the same resonance as radiation mode j (the generator's twin
// test: inside j's as-fitted half-power band, and itself narrower than the
// radiation bank's local mode spacing) takes radiationPole(j) exactly, so the
// anchor transform, Shape, Wood and the plate-Q rule move both together. An
// unpaired mode takes the same maps by class: Shape's (shaped, as for any
// bridge), the anchor's air or plate factor, Wood and the calibration; its Q
// the plate-Q rule's octave factor where it applies, then Wood's and the
// calibration's. The residue matrix is kept, so the mode stays passive.
static_assert(detail::steelBridgeRadiationTwins.size()
                  == detail::measuredSteelBridgeModes.size()
              && detail::steelBridgeUnpairedQRatio.size()
                  == detail::measuredSteelBridgeModes.size(),
              "one twin and one Q ratio per steel bridge mode");

ModalPole steelOwnBridgePole(std::size_t index,
                             const detail::MeasuredBridgeMode& source,
                             const detail::MeasuredBridgeMode& shaped,
                             const AnchorTransform& anchor,
                             const BodyShapeMorph& morph, WoodFactors wood,
                             const PhysicalCalibration& calibration) noexcept
{
    const int twin = detail::steelBridgeRadiationTwins[index];
    if (twin >= 0)
        return radiationPole(detail::measuredSteelBodyModes, twin, anchor,
                             morph, wood, false, calibration);
    const bool lowBodyMode = source.frequency > 85.0f
        && source.frequency < 145.0f;
    const float lowModeMorph = lowBodyMode
        ? anchor.airHz / 107.0f : anchor.modeScale;
    return { shaped.frequency * lowModeMorph * wood.frequency
                 * calibration.bodyFrequencyScale,
             source.q * detail::steelBridgeUnpairedQRatio[index] * wood.q
                 * calibration.bodyQScale };
}

// The generator's twin test (Tools/GenerateMeasuredBridge.py,
// radiation_twins) applied to a bridge bank and the radiation bank of the
// same guitar, at compile time: the nearest radiation mode k, when the bridge
// mode lies inside k's half-power band, |f_b - f_k| < f_k/(2 Q_k), and is
// itself one resolved resonance, f_b/Q_b < the radiation's local spacing.
// The Bellido keeps its radiation's Qs as fitted, so its header is the
// test's input (steel's are plate-Q corrected, so its twins are the
// generator's table instead).
constexpr double constexprAbs(double value) noexcept
{
    return value < 0.0 ? -value : value;
}

template <std::size_t BridgeModes, std::size_t RadiationModes>
constexpr std::array<std::int16_t, BridgeModes> radiationTwins(
    const std::array<detail::MeasuredBridgeMode, BridgeModes>& bridge,
    const std::array<detail::MeasuredBodyMode, RadiationModes>& radiation) noexcept
{
    std::array<std::int16_t, BridgeModes> twins {};
    for (std::size_t i = 0; i < BridgeModes; ++i)
    {
        const double frequency = bridge[i].frequency;
        std::size_t k = 0;
        for (std::size_t j = 1; j < RadiationModes; ++j)
            if (constexprAbs(radiation[j].frequency - frequency)
                < constexprAbs(radiation[k].frequency - frequency))
                k = j;
        const double centre = radiation[k].frequency;
        const bool hasBelow = k > 0;
        const bool hasAbove = k + 1 < RadiationModes;
        const double below = hasBelow ? centre - radiation[k - 1].frequency : 0.0;
        const double above = hasAbove ? radiation[k + 1].frequency - centre : 0.0;
        const double spacing = hasBelow && hasAbove ? 0.5 * (below + above)
                             : hasBelow ? below : above;
        const bool inside = constexprAbs(frequency - centre)
            < centre / (2.0 * static_cast<double>(radiation[k].q));
        const bool resolved = frequency / static_cast<double>(bridge[i].q) < spacing;
        twins[i] = inside && resolved ? static_cast<std::int16_t>(k)
                                      : static_cast<std::int16_t>(-1);
    }
    return twins;
}

constexpr auto bellidoBridgeRadiationTwins = radiationTwins(
    detail::bellidoBridgeModes, detail::bellidoBodyModes);

// The engine pole of mode `index` of the Bellido's bridge. It keeps, under
// every Shape and Wood, the relation to its radiation that it has at its
// anchor: a mode twinned with radiation mode j (radiationTwins) is moved by
// exactly the factor radiationPole moves j by from the anchor's own pole
// (Shape's class factor, Wood and the calibration), so a drain stays on the
// resonance it drains wherever the construction goes. An unpaired mode takes
// the same maps by class, as steel's unpaired modes do. Q takes Wood's and
// the calibration's factor. The Bellido's anchor is the identity, so at its
// own box and wood its bridge is its measurement to the bit. The residue
// matrix is kept, so the mode stays passive.
ModalPole bellidoBridgePole(std::size_t index,
                            const detail::MeasuredBridgeMode& source,
                            const detail::MeasuredBridgeMode& shaped,
                            const AnchorTransform& anchor,
                            const BodyShapeMorph& morph, WoodFactors wood,
                            const PhysicalCalibration& calibration) noexcept
{
    const auto& radiation = detail::bellidoBodyModes;
    const int twin = index < bellidoBridgeRadiationTwins.size()
        ? bellidoBridgeRadiationTwins[index] : -1;
    const float q = source.q * wood.q * calibration.bodyQScale;
    if (twin >= 0)
    {
        const auto pole = radiationPole(radiation, twin, anchor, morph, wood,
                                        true, calibration);
        // The same mode at the anchor: the anchor transform alone, computed
        // in the same order, so at the anchor the ratio is exactly 1.
        PhysicalCalibration unit = calibration;
        unit.bodyFrequencyScale = 1.0f;
        unit.bodyQScale = 1.0f;
        const auto anchored = radiationPole(radiation, twin, anchor,
            BodyShapeMorph {}, WoodFactors { 1.0f, 1.0f, 1.0f, 1.0f },
            true, unit);
        return { source.frequency * (pole.frequency / anchored.frequency), q };
    }
    return { shaped.frequency * wood.frequency
                 * calibration.bodyFrequencyScale,
             q };
}

// The steel blend (SteelBodyBlend.h). On the Original guitar the bridge is
// B's aligned bridge and the joint-pole body's kept modes in parallel, and
// the radiation is g21's bank and the joint-pole body's kept radiation in
// parallel, each part at its share.
// A part whose share is zero is not played at all, and a share of exactly 1
// multiplies exactly, so B=1, D=1, E=0 is Set 18's B+D bit for bit.
//
// The joint-pole body (MeasuredJointBodyData.h) is one array: the radiation
// reads the kept modes, the bridge those of them carrying a mobility residue,
// each remembering the joint mode whose pole it rings on. Both are
// compile-time views of it.
template <std::size_t N>
constexpr std::array<detail::MeasuredBodyMode, N> jointRadiationView(
    const std::array<detail::MeasuredJointBodyMode, N>& joint) noexcept
{
    std::array<detail::MeasuredBodyMode, N> out {};
    for (std::size_t index = 0; index < N; ++index)
    {
        const auto& mode = joint[index];
        out[index] = detail::MeasuredBodyMode { mode.frequency, mode.q,
            mode.leftReal, mode.leftImaginary, mode.rightReal, mode.rightImaginary,
            mode.upperReal, mode.upperImaginary, mode.leftMomentReal,
            mode.leftMomentImaginary, mode.rightMomentReal,
            mode.rightMomentImaginary, mode.upperMomentReal,
            mode.upperMomentImaginary };
    }
    return out;
}

// E plays only its joint modes below steelBlendJointBandHz (SteelBodyBlend.h),
// each at its place in the joint array, so its pole, its anchor detune and
// its Shape group are the whole joint body's, and each keeps its own bridge
// residue when it has one: a kept bridge mode always rings on its kept
// radiation twin.
template <std::size_t N>
constexpr std::size_t jointBandCount(
    const std::array<detail::MeasuredJointBodyMode, N>& joint, float bandHz) noexcept
{
    std::size_t count = 0;
    for (const auto& mode : joint)
        if (mode.frequency < bandHz)
            ++count;
    return count;
}

template <std::size_t K, std::size_t N>
constexpr std::array<std::uint16_t, K> jointBandModes(
    const std::array<detail::MeasuredJointBodyMode, N>& joint, float bandHz) noexcept
{
    std::array<std::uint16_t, K> out {};
    std::size_t slot = 0;
    for (std::size_t index = 0; index < N; ++index)
        if (joint[index].frequency < bandHz)
            out[slot++] = static_cast<std::uint16_t>(index);
    return out;
}

constexpr auto steelJointKept = jointBandModes<jointBandCount(
    detail::measuredSteelJointBodyModes, detail::steelBlendJointBandHz)>(
    detail::measuredSteelJointBodyModes, detail::steelBlendJointBandHz);

constexpr bool jointHasMobility(const detail::MeasuredJointBodyMode& mode) noexcept
{
    return mode.heave > 0.0f || mode.rock > 0.0f;
}

template <std::size_t K, std::size_t N>
constexpr std::size_t jointMobilityCount(
    const std::array<std::uint16_t, K>& kept,
    const std::array<detail::MeasuredJointBodyMode, N>& joint) noexcept
{
    std::size_t count = 0;
    for (const auto index : kept)
        if (jointHasMobility(joint[index]))
            ++count;
    return count;
}

template <std::size_t A, std::size_t K, std::size_t N>
constexpr std::array<detail::MeasuredBridgeMode, A> jointBridgeView(
    const std::array<std::uint16_t, K>& kept,
    const std::array<detail::MeasuredJointBodyMode, N>& joint) noexcept
{
    std::array<detail::MeasuredBridgeMode, A> out {};
    std::size_t slot = 0;
    for (const auto index : kept)
        if (jointHasMobility(joint[index]))
        {
            const auto& mode = joint[index];
            out[slot++] = detail::MeasuredBridgeMode { mode.frequency, mode.q,
                                                       mode.heave, mode.cross, mode.rock };
        }
    return out;
}

template <std::size_t A, std::size_t K, std::size_t N>
constexpr std::array<std::uint16_t, A> jointBridgeSource(
    const std::array<std::uint16_t, K>& kept,
    const std::array<detail::MeasuredJointBodyMode, N>& joint) noexcept
{
    std::array<std::uint16_t, A> out {};
    std::size_t slot = 0;
    for (const auto index : kept)
        if (jointHasMobility(joint[index]))
            out[slot++] = index;
    return out;
}

// The whole joint body's radiation: its Shape morph reads A0 and T1 from all
// of it, and each kept mode is read at its own index.
constexpr auto steelJointRadiationModes
    = jointRadiationView(detail::measuredSteelJointBodyModes);
constexpr std::size_t steelJointRadiationCount = steelJointKept.size();
constexpr std::size_t steelJointBridgeCount
    = jointMobilityCount(steelJointKept, detail::measuredSteelJointBodyModes);
constexpr auto steelJointBridgeModes = jointBridgeView<steelJointBridgeCount>(
    steelJointKept, detail::measuredSteelJointBodyModes);
constexpr auto steelJointBridgeSource = jointBridgeSource<steelJointBridgeCount>(
    steelJointKept, detail::measuredSteelJointBodyModes);

// Each part's share of the whole. Below E's band (1 - E) carries everything
// but the joint body: g21's radiation and B's bridge. Above it E is not
// played: B's bridge is whole there, and g21's radiation gives up
// steelBlendHighJointFraction of E's share.
constexpr float steelBlendRestShare = 1.0f - detail::steelBlendJointBodyWeight;
constexpr float steelBlendHighRadiationShare
    = 1.0f - detail::steelBlendJointBodyWeight * detail::steelBlendHighJointFraction;
constexpr float steelBlendJointShare = detail::steelBlendJointBodyWeight;

// The share at which g21's radiation mode, or B's bridge mode, at measured
// frequency hz is played.
constexpr float steelBlendG21Share(float hz) noexcept
{
    return hz < detail::steelBlendJointBandHz ? steelBlendRestShare
                                               : steelBlendHighRadiationShare;
}
constexpr float steelBlendOwnShare(float hz) noexcept
{
    return hz < detail::steelBlendJointBandHz ? steelBlendRestShare : 1.0f;
}

constexpr std::size_t steelBlendBodyModeCount
    = detail::measuredSteelBodyModes.size()
    + (steelBlendJointShare > 0.0f ? steelJointRadiationCount : 0);
constexpr std::size_t steelBlendBridgeModeCount
    = detail::measuredSteelBridgeModes.size()
    + (steelBlendJointShare > 0.0f ? steelJointBridgeCount : 0);
// The in-order sum of g21's bank ends on a whole group of four, so the parts
// after it start on one (BodyBank::render).
static_assert(detail::measuredSteelBodyModes.size() % 4 == 0,
              "g21's bank no longer ends on a group of four: pad it in configureBody");
static_assert(steelBlendBodyModeCount <= ACUSTRA_BODY_MODE_COUNT,
              "the steel blend's radiation exceeds the body slots: with every "
              "joint mode (a band above 10 kHz) it needs "
              "-DACUSTRA_BODY_MODE_COUNT=263");
static_assert(steelBlendBodyModeCount
                  + AcustraEngine::radiationContinuationSlots
                  <= ACUSTRA_BODY_MODE_COUNT
              && (detail::bellidoBodyModes.size() + 3) / 4 * 4
                  + AcustraEngine::radiationContinuationSlots
                  <= ACUSTRA_BODY_MODE_COUNT,
              "the radiation's continuation above the fitted band "
              "(configureBody) no longer fits the body slots");
static_assert(steelBlendBridgeModeCount <= ACUSTRA_BRIDGE_MODE_COUNT,
              "the steel blend's bridge exceeds the bridge slots: with every "
              "joint mode (a band above 10 kHz) it needs "
              "-DACUSTRA_BRIDGE_MODE_COUNT=103");
static_assert(detail::measuredSteelT1PlateQWeight == detail::steelBlendT1PlateQWeight,
              "MeasuredBodyData.h was written with another D weight than "
              "SteelBodyBlend.h's: rerun Tools/GenerateBodyForcePair.py --plate-q median");

// The joint-pole body's own Shape morph: its A0 and T1 are its own modes.
BodyShapeMorph steelJointMorph(const AnchorTransform& anchor,
                               BodyShape shape) noexcept
{
    return bodyShapeMorph(steelJointRadiationModes, anchor,
        anchorBodyFor(GuitarModel::Original),
        targetBodyFor(GuitarModel::Original, shape));
}

// Steel's own bridge in the blend, mode by mode: visit(source, placed, level,
// own) with the mode as the bank stores it, the mode with its engine pole, its
// residues' factor relative to B's own level (the fitted scale times
// steelTopMobilityRatio), so that B's modes keep exactly B's arithmetic, and
// whether it is one of B's modes, which come first.
// B's modes are steelOwnBridgePole's; the joint body's ring on its
// radiation's poles at its own steel-top level.
template <typename Visit>
void visitSteelBlendBridge(float a0, float t1, float plate, float t1UpperHz,
                           const AnchorTransform& anchor,
                           const BodyShapeMorph& morph,
                           const BodyShapeMorph& jointMorph, WoodFactors wood,
                           const PhysicalCalibration& calibration,
                           Visit&& visit) noexcept
{
    const auto& own = detail::measuredSteelBridgeModes;
    for (std::size_t index = 0; index < own.size(); ++index)
    {
        auto placed = shapeBridgeMode(own[index], a0, t1, plate, t1UpperHz);
        const auto pole = steelOwnBridgePole(index, own[index], placed, anchor,
                                             morph, wood, calibration);
        placed.frequency = pole.frequency;
        placed.q = pole.q;
        visit(own[index], placed, steelBlendOwnShare(own[index].frequency), true);
    }
    if (steelBlendJointShare > 0.0f)
    {
        constexpr float level = steelBlendJointShare
            * detail::steelJointTopMobilityRatio / detail::steelTopMobilityRatio;
        for (std::size_t index = 0; index < steelJointBridgeCount; ++index)
        {
            const int source = steelJointBridgeSource[index];
            const auto pole = radiationPole(steelJointRadiationModes, source,
                anchor, jointMorph, wood, false, calibration);
            auto placed = steelJointBridgeModes[index];
            placed.frequency = pole.frequency;
            placed.q = pole.q;
            visit(steelJointBridgeModes[index], placed, level, false);
        }
    }
}

// Where a string crosses the saddle, in units of the half-separation between
// the archive's two bridge impacts. Method.pdf section 2b puts the treble
// impact between B3 and E4 and the bass impact between E2 and A2, so they are
// the midpoints of string pairs (4,5) and (0,1) and sit at u = +1 and -1; the
// six strings are then at (i - 2.5)/2. Only the ratio enters, so the set-up
// spacing at the saddle -- the one strumDelaySamples uses -- cancels.
// Tools/GenerateMeasuredBridge.py fits the bank against these same arms.
constexpr float saddleLeverArm(int stringIndex) noexcept
{
    return 0.5f * (static_cast<float>(stringIndex) - 2.5f);
}

bool includeMeasuredBridgeMode(const detail::MeasuredBridgeMode& mode) noexcept
{
#if defined(ACUSTRA_ANALYSIS_EXCLUDE_MEASURED_OPEN_STRINGS)
    // Analysis only, and applied to whichever bank the material selects: the
    // archive's setup photographs show installed strings.  Of the retained
    // candidates only the steel bank's 82.764 Hz lies within 25 cents of the
    // one open string this surrogate names (E2, 82.407 Hz).  Do not ship it: the measurement string's impedance
    // metadata is unavailable, and the generator's own open-string screen
    // already gates every retained mode near a standard open string on its
    // resolved Q.
    constexpr float openLowE = 82.406889f;
    const float cents = 1200.0f * std::log2(mode.frequency / openLowE);
    return exact::abs(cents) >= 25.0f;
#else
    (void) mode;
    return true;
#endif
}

// The plate conductance floor is one over-damped positive-real section. An
// over-damped s/(s^2+2ds+w0^2) has real poles at w0^2/(2d) and 2d, so its
// conductance is flat between them; centre sqrt(f_low*f_high) with
// q = sqrt(f_low/f_high) places those poles at f_low and f_high, and
// weight = G*2*pi*f_high makes the plateau conductance equal G. The upper
// limit is fixed above the 10 kHz measurement band it extrapolates. As built
// (configureBridge) the centre is prewarped at 48 kHz, which raises both
// analog poles about 5%, and 48 kHz's bilinear map then draws the upper one
// toward Nyquist; the plateau was fitted as that 48 kHz design renders, so
// every host rate keeps the same analog prototype.
constexpr float plateConductanceUpperHz = 16000.0f;

struct PlateConductanceMode
{
    float frequency;
    float q;
    float weight;
};

PlateConductanceMode plateConductanceMode(
    const PhysicalCalibration& calibration) noexcept
{
    const float low = std::clamp(calibration.bridgeConductanceCornerHz,
                                 100.0f, 8000.0f);
    return { exact::sqrt(low * plateConductanceUpperHz),
             exact::sqrt(low / plateConductanceUpperHz),
             calibration.bridgeConductanceFloor * twoPi
                 * plateConductanceUpperHz };
}

int wrapDelayIndex(int index) noexcept
{
    while (index < 0)
        index += localMaximumDelaySamples;
    while (index >= localMaximumDelaySamples)
        index -= localMaximumDelaySamples;
    return index;
}

bool sameStringConstruction(const EngineParameters& a,
                            const EngineParameters& b) noexcept
{
    return a.guitarModel == b.guitarModel && a.tuning == b.tuning;
}

// One dispersion design: the loop delay it was fitted with and, per section,
// the pole pair relative to the fundamental (secondOrderAllpassCoefficients)
// and its coefficients at the design frequency. A zero decay ratio is an
// unused section.
struct DispersionCalibration
{
    double delay { 128.0 };
    std::array<double, 2> decayRatio { 0.0, 0.0 };
    std::array<double, 2> poleRatio { 0.0, 0.0 };
    std::array<double, 2> a1 {};
    std::array<double, 2> a2 {};
};

double referenceLossOmega(double omega, double sampleRate) noexcept
{
    if (sampleRate == 48000.0)
        return omega;
    return 2.0 * std::atan((sampleRate / 48000.0) * std::tan(0.5 * omega));
}

double mixedOnePolePhase(double coefficient, double mix,
                         double omega) noexcept
{
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double denominatorReal = 1.0 - coefficient * cosine;
    const double denominatorImaginary = coefficient * sine;
    const double denominatorNorm = denominatorReal * denominatorReal
                                 + denominatorImaginary * denominatorImaginary;
    const double lowReal = (1.0 - coefficient) * denominatorReal
                         / denominatorNorm;
    const double lowImaginary = -(1.0 - coefficient) * denominatorImaginary
                              / denominatorNorm;
    return -std::atan2(mix * lowImaginary,
                       (1.0 - mix) + mix * lowReal);
}

// A string's own bending loss. Only the bending part of a string's restoring
// force is elastic in a lossy way worth modelling here: give the bending
// stiffness a loss factor, EI(1 + i eta), and mode n, whose potential energy
// is the fraction EI k_n^2 / (T + EI k_n^2) bending, loses
//     1/Q_n = eta * EI k_n^2 / (T + EI k_n^2) = eta * B n^2 / (1 + B n^2),
// with B = pi^2 EI / (T L^2), the same inharmonicity coefficient the
// dispersion is designed from. That is the viscoelastic term of Valette's
// string damping model (C. Valette, "The mechanics of vibrating strings", in
// Mechanics of Musical Instruments, eds. A. Hirschberg, J. Kergomard and G.
// Weinreich, Springer 1995, pp. 115-183) and the bending-loss term of
// Woodhouse's (J. Woodhouse, "On the synthesis of guitar plucks", Acta
// Acustica united with Acustica 90 (2004) 928-944, Sec. 2). Those models'
// other two terms, air damping and a friction that sets a constant Q, have
// no separate counterpart here: the loop's fundamental T60 and its broad and
// high shelves, calibrated as a whole, stand in for them (with two small
// authored per-plane factors, configureVoice, that act as a constant Q on
// top of the requested T60), and this section
// is added to them rather than replacing them (replacing the shelves by
// Woodhouse's published three-term law over-damped 3-6.8 kHz, see
// Docs/decisions.md, 2026-09-04). Its
// decay rate sigma_n = omega_n / (2 Q_n) grows as the cube of frequency until
// B n^2 nears one, which is how a string's highest partials die in tens of
// milliseconds while its first dozen ring for seconds. Wound strings rub
// wrap on wrap and wrap on core as they bend, so eta is one number per
// construction (the two bending-loss factors in FittedPhysicalData.h, wound
// and plain), not a property of the material alone.
//
// Per round trip of the loop, one fundamental period, partial n therefore
// loses L_n = sigma_n / f0 = pi (f_n / f0) eta B n^2 / (1 + B n^2) nepers.
// The section realising it is all-pole with unit gain at DC,
//     H(z) = (1 + a1 + a2) / (1 + a1 z^-1 + a2 z^-2),
// so |H|^-2 = 1 + 4 p s + 16 q s^2 in s = sin^2(omega / 2): p carries an
// omega^2 loss and q an omega^4 one, and the pair can follow the law's cube
// between two collocation partials, which a one-pole's single omega^2 cannot
// (it misplaces the law by a factor of two an octave either side of where it
// is fitted).
// The two partials are where the law's added decay reaches 20 and 160 dB/s,
// an octave apart for the cube: below the first the added loss is inaudible
// against the loop's own, and above the second a partial is gone in under
// 0.4 s whatever the exact rate. The section is designed at the host rate,
// so it meets the law at the same partials at every rate.
struct BendingLossSection
{
    double gain { 1.0 };
    double a1 { 0.0 };
    double a2 { 0.0 };
};

double stretchedPartial(double partial, double inharmonicity) noexcept
{
    return partial * std::sqrt((1.0 + inharmonicity * partial * partial)
                               / (1.0 + inharmonicity));
}

double bendingLossRate(double partial, double inharmonicity, double factor,
                       double fundamental) noexcept
{
    const double bending = inharmonicity * partial * partial;
    return piDouble * fundamental * stretchedPartial(partial, inharmonicity)
         * factor * bending / (1.0 + bending);
}

BendingLossSection bendingLossSection(double factor, double inharmonicity,
                                      double fundamental,
                                      double sampleRate) noexcept
{
    BendingLossSection section;
    if (!(factor > 0.0) || !(inharmonicity > 0.0) || !(fundamental > 0.0)
        || !(sampleRate > 0.0))
        return section;
    // Collocation stays below 0.3 of the host rate: nearer Nyquist a
    // two-pole section's loss flattens while the law's keeps rising, and a
    // collocation pair up there (a plain string whose law reaches 160 dB/s
    // only above 20 kHz) bends the curve between them by 30-50%. Above the
    // limit the section's loss still rises, more slowly than the law's.
    constexpr double collocationLimit = 0.3;
    double below = 1.0;
    double above = collocationLimit * sampleRate / fundamental;
    if (!(above > 1.0))
        return section;
    for (int iteration = 0; iteration < 48; ++iteration)
    {
        const double middle = 0.5 * (below + above);
        if (stretchedPartial(middle, inharmonicity) * fundamental
            < collocationLimit * sampleRate)
            below = middle;
        else
            above = middle;
    }
    const double highest = below;
    // The law is monotonic in n; bisect for the partial reaching a rate.
    const auto partialAt = [&] (double rate)
    {
        double lower = 1.0;
        double upper = highest;
        if (bendingLossRate(upper, inharmonicity, factor, fundamental) <= rate)
            return upper;
        for (int iteration = 0; iteration < 48; ++iteration)
        {
            const double middle = 0.5 * (lower + upper);
            if (bendingLossRate(middle, inharmonicity, factor, fundamental)
                < rate)
                lower = middle;
            else
                upper = middle;
        }
        return 0.5 * (lower + upper);
    };
    constexpr double decibelsPerNeper = 8.685889638065035;
    const double upperPartial = partialAt(160.0 / decibelsPerNeper);
    const double lowerPartial = std::min(partialAt(20.0 / decibelsPerNeper),
                                         0.5 * upperPartial);
    if (!(lowerPartial >= 1.0) || !(upperPartial > lowerPartial))
        return section;
    double target[2] {};
    double s[2] {};
    const double partials[2] { lowerPartial, upperPartial };
    for (int index = 0; index < 2; ++index)
    {
        const double loss = bendingLossRate(partials[index], inharmonicity,
                                            factor, fundamental) / fundamental;
        target[index] = std::expm1(2.0 * loss);
        const double halfOmega = piDouble * fundamental
            * stretchedPartial(partials[index], inharmonicity) / sampleRate;
        s[index] = std::sin(halfOmega) * std::sin(halfOmega);
    }
    const double determinant = 64.0 * s[0] * s[1] * (s[1] - s[0]);
    if (!(std::abs(determinant) > 1.0e-300))
        return section;
    double p = 16.0 * (target[0] * s[1] * s[1] - target[1] * s[0] * s[0])
             / determinant;
    double q = 4.0 * (s[0] * target[1] - s[1] * target[0]) / determinant;
    // Only a law shallower than omega^2 (B n^2 near one) or steeper than
    // omega^4 leaves the pair; keep the upper partial exact with one term.
    if (q < 0.0)
    {
        q = 0.0;
        p = target[1] / (4.0 * s[1]);
    }
    else if (p < 0.0)
    {
        p = 0.0;
        q = target[1] / (16.0 * s[1] * s[1]);
    }
    if (!(p > 0.0 || q > 0.0) || !std::isfinite(p) || !std::isfinite(q))
        return section;
    // Spectral factorisation. On the unit circle 4s = u = 2 - z - 1/z, so
    // |A|^2 / A(1)^2 = 1 + p u + q u^2; each root u_r of that quadratic is a
    // reciprocal pair z + 1/z = 2 - u_r, of which the minimum-phase A keeps
    // the member inside the circle.
    const auto inside = [] (std::complex<double> u)
    {
        const std::complex<double> sum = 2.0 - u;
        const std::complex<double> root = std::sqrt(sum * sum - 4.0);
        const std::complex<double> first = 0.5 * (sum + root);
        const std::complex<double> second = 0.5 * (sum - root);
        return std::abs(first) < std::abs(second) ? first : second;
    };
    if (q > 0.0)
    {
        const std::complex<double> discriminant
            = std::sqrt(std::complex<double>(p * p - 4.0 * q, 0.0));
        const std::complex<double> first = inside((-p + discriminant) / (2.0 * q));
        const std::complex<double> second = inside((-p - discriminant) / (2.0 * q));
        section.a1 = -(first + second).real();
        section.a2 = (first * second).real();
    }
    else
    {
        section.a1 = -inside(std::complex<double>(-1.0 / p, 0.0)).real();
        section.a2 = 0.0;
    }
    section.gain = 1.0 + section.a1 + section.a2;
    if (!std::isfinite(section.gain) || !(section.gain > 0.0)
        || !(std::abs(section.a2) < 1.0)
        || !(std::abs(section.a1) < 1.0 + section.a2))
        return {};
    return section;
}

// Phase lag of the bending-loss section: arg A(e^{j omega}). Both poles lie
// inside the circle, so the lag stays within (-pi, pi) and needs no unwrap.
double bendingLossLag(double a1, double a2, double omega) noexcept
{
    return std::atan2(-a1 * std::sin(omega) - a2 * std::sin(2.0 * omega),
                      1.0 + a1 * std::cos(omega) + a2 * std::cos(2.0 * omega));
}

double bendingLossMagnitude(double gain, double a1, double a2,
                            double omega) noexcept
{
    const double real = 1.0 + a1 * std::cos(omega) + a2 * std::cos(2.0 * omega);
    const double imaginary = a1 * std::sin(omega) + a2 * std::sin(2.0 * omega);
    return gain / std::hypot(real, imaginary);
}

// The loop's fractional delay is a second-order Thiran allpass read from the
// line at an integer tap: an allpass has magnitude exactly one at every
// frequency, and Thiran's coefficients make its phase delay maximally flat at
// D samples around DC (Laakso, Valimaki, Karjalainen and Laine, "Splitting
// the Unit Delay: Tools for Fractional Delay Filter Design", IEEE Signal
// Processing Magazine 13(1), 1996, 30-60, Eq. 86 on p. 49, "Maximally Flat
// Group Delay Design of Allpass Filters"). The four-point
// Catmull-Rom read it replaces lost 0.23 and 0.53 dB per pass at 8 and 10 kHz
// at a half-sample fraction and nothing at an integer one, so a string's
// upper partials decayed at a rate set by the accidental fraction of its loop
// length and by the host rate.
//
// D is kept in [1.1, 2.1). Both edges of that band sit next to a delay at
// which the section is exact - at D = 2 it is two unit delays, at D = 1 one -
// so the loop phase steps by only 0.037 rad at half the Nyquist rate when a
// slewing delay crosses a band edge and the tap moves, and by a higher power
// of frequency below it; its largest phase-delay error inside the band is
// 0.045 rad there, at D = 1.59. A first-order section on its own best band,
// [0.5, 1.5), is worse at both: its phase-delay error runs from 0.142 rad at
// the bottom edge to 0.391 at the top, and its band-edge step is 0.533 rad.
// Going nearer D = 1 shrinks both further, but the pole radius is
// already 0.87 at 1.1 and at D = 1 exactly a pole sits on the unit circle
// with only a zero to cancel it. The band is one sample wide, so the tap is a
// function of the delay alone: the loop and the design that tunes it split
// the same delay the same way, which a hysteretic band would not, and a
// crossing moves D a whole sample to the far edge, so a slewing delay cannot
// chatter across it.
int delayAnchor(double samples) noexcept
{
    return static_cast<int>(exact::floor(samples - 1.1));
}

void thiranCoefficients(double samples, double& a1, double& a2) noexcept
{
    a1 = -2.0 * (samples - 2.0) / (samples + 1.0);
    a2 = (samples - 2.0) * (samples - 1.0)
       / ((samples + 1.0) * (samples + 2.0));
}

// A section's pole pair is stored relative to the loop's fundamental, so a
// bend or a vibrato carries the whole design with the partials it was fitted
// to. A decay ratio of zero reads as a1 = a2 = 0, a two-sample delay; that is
// how the first section runs where there is nothing to disperse, and it marks
// a second section the design leaves unused, which the loop bypasses and
// every phase sum below leaves out (DispersionSections::used).
void secondOrderAllpassCoefficients(double omega, double decayRatio,
                                    double poleRatio,
                                    double& a1, double& a2) noexcept
{
    if (!(decayRatio > 0.0))
    {
        a1 = 0.0;
        a2 = 0.0;
        return;
    }
    const double radius = std::exp(-omega * decayRatio);
    const double angle = omega * poleRatio;
    a1 = -2.0 * radius * std::cos(angle);
    a2 = radius * radius;
}

double secondOrderAllpassPhase(double a1, double a2,
                               double omega) noexcept
{
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double cosine2 = std::cos(2.0 * omega);
    const double sine2 = std::sin(2.0 * omega);
    const double numeratorPhase = std::atan2(
        -a1 * sine - sine2, a2 + a1 * cosine + cosine2);
    const double denominatorPhase = std::atan2(
        -a1 * sine - a2 * sine2, 1.0 + a1 * cosine + a2 * cosine2);
    double lag = denominatorPhase - numeratorPhase;
    constexpr double twoPiDouble = 2.0 * 3.14159265358979323846;
    while (lag < 0.0)
        lag += twoPiDouble;
    while (lag >= twoPiDouble)
        lag -= twoPiDouble;
    return lag;
}

// The loop's dispersion is two second-order allpass sections in cascade
// (calibrateDispersion says why two).
constexpr int dispersionSectionCount = 2;

struct DispersionSections
{
    std::array<double, dispersionSectionCount> a1 {};
    std::array<double, dispersionSectionCount> a2 {};
    std::array<bool, dispersionSectionCount> used { true, false };
};

// Each stable section's lag rises from 0 to 2 pi across the band, so the
// per-section [0, 2 pi) values add without an unwrap. An unused section is
// bypassed and adds none.
double dispersionPhase(const DispersionSections& sections,
                       double omega) noexcept
{
    double lag = 0.0;
    for (std::size_t section = 0; section < dispersionSectionCount; ++section)
        if (sections.used[section])
            lag += secondOrderAllpassPhase(sections.a1[section],
                                           sections.a2[section], omega);
    return lag;
}

// Phase lag of tap plus allpass. The tap is passed in rather than derived
// from the delay: folding delayAnchor() into this leaves the residual the
// tuning solves discontinuous where D crosses its band edge, and the
// collocation stalls on it. Every solve below instead pins one tap, which
// makes its residual smooth in the delay and lets the delay travel as far as
// the pole pair moves it, and then re-derives the tap from its own answer and
// solves again at that tap.
double thiranDelayPhase(double samples, int anchor, double omega) noexcept
{
    double a1 = 0.0;
    double a2 = 0.0;
    thiranCoefficients(samples - static_cast<double>(anchor), a1, a2);
    return static_cast<double>(anchor) * omega
         + secondOrderAllpassPhase(a1, a2, omega);
}

double tunedLoopDelay(double fundamental, double sampleRate,
                      double broadCoefficient, double broadMix,
                      double highCoefficient, double highMix,
                      const DispersionSections& dispersion,
                      double bendingA1 = 0.0, double bendingA2 = 0.0) noexcept
{
    constexpr double twoPiDouble = 2.0 * 3.14159265358979323846;
    const double omega = twoPiDouble * fundamental / sampleRate;
    const double lossOmega = referenceLossOmega(omega, sampleRate);
    // The bending-loss section is designed at the host rate, so its lag is
    // read at the host frequency rather than the 48 kHz reference one.
    const double fixedPhase = mixedOnePolePhase(
        broadCoefficient, broadMix, lossOmega)
        + mixedOnePolePhase(highCoefficient, highMix, lossOmega)
        + bendingLossLag(bendingA1, bendingA2, omega)
        + dispersionPhase(dispersion, omega);
    double delay = std::clamp(sampleRate / fundamental - fixedPhase / omega,
                              3.0,
                              static_cast<double>(localMaximumDelaySamples - 3));
    for (int round = 0; round < 3; ++round)
    {
        const int anchor = delayAnchor(delay);
        for (int iteration = 0; iteration < 6; ++iteration)
        {
            const double residual = thiranDelayPhase(delay, anchor, omega)
                                  + fixedPhase - twoPiDouble;
            if (exact::abs(residual) < 1.0e-11)
                break;
            constexpr double step = 0.01;
            const double slope
                = (thiranDelayPhase(delay + step, anchor, omega)
                 - thiranDelayPhase(delay - step, anchor, omega))
                / (2.0 * step);
            if (exact::abs(slope) < 1.0e-12)
                break;
            delay = std::clamp(delay - residual / slope, 3.0,
                static_cast<double>(localMaximumDelaySamples - 3));
        }
        if (delayAnchor(delay) == anchor)
            break;
    }
    return delay;
}

bool solveThreeByThree(double matrix[3][3], const double rhs[3],
                       double result[3]) noexcept
{
    double augmented[3][4] {};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 3; ++column)
            augmented[row][column] = matrix[row][column];
        augmented[row][3] = rhs[row];
    }

    for (int column = 0; column < 3; ++column)
    {
        int pivot = column;
        for (int row = column + 1; row < 3; ++row)
            if (exact::abs(augmented[row][column])
                > exact::abs(augmented[pivot][column]))
                pivot = row;
        if (exact::abs(augmented[pivot][column]) < 1.0e-13)
            return false;
        if (pivot != column)
            for (int item = column; item < 4; ++item)
                std::swap(augmented[column][item], augmented[pivot][item]);
        for (int row = column + 1; row < 3; ++row)
        {
            const double factor = augmented[row][column]
                                / augmented[column][column];
            for (int item = column; item < 4; ++item)
                augmented[row][item] -= factor * augmented[column][item];
        }
    }

    for (int row = 2; row >= 0; --row)
    {
        double value = augmented[row][3];
        for (int column = row + 1; column < 3; ++column)
            value -= augmented[row][column] * result[column];
        result[row] = value / augmented[row][row];
    }
    return true;
}

// The single-section design: one section by three-point collocation, the
// second left unused. It is the whole design on a flexible string, and on a
// stiffer one the start the two-section fit is refitted from
// (calibrateDispersion below).
DispersionCalibration collocateDispersion(
    double inharmonicity, double fundamental, double sampleRate,
    double broadCoefficient, double broadMix,
    double highCoefficient, double highMix,
    double initialDecayRatio, double initialPoleRatio,
    double bendingA1, double bendingA2) noexcept
{
    constexpr double twoPiDouble = 2.0 * piDouble;
    const double omega0 = twoPiDouble * fundamental / sampleRate;
    DispersionCalibration calibration;
    const double startDecayRatio = std::clamp(initialDecayRatio, 0.1, 30.0);
    const double startPoleRatio = std::clamp(initialPoleRatio, 0.05, 15.0);
    // The second section is unused here.
    const auto sections = [] (double a1, double a2)
    {
        DispersionSections result;
        result.a1[0] = a1;
        result.a2[0] = a2;
        return result;
    };

    const auto stretchedOmega = [=] (double partial)
    {
        return omega0 * partial * exact::sqrt(
            (1.0 + inharmonicity * partial * partial)
            / (1.0 + inharmonicity));
    };
    int highest = 12;
    while (highest > 3 && stretchedOmega(highest) > 0.84 * piDouble)
        --highest;
    // Three loop-phase collocation points determine delay plus the stable
    // complex pole pair. They are H1/H7/H11.5 throughout the normal 44.1
    // kHz-and-up guitar range; placing the upper point half a partial inward
    // distributes the approximation error over the integer partials instead
    // of spending exactness at H12. The upper two move down with Nyquist.
    const double anchors[] {
        1.0, static_cast<double>(1 + highest / 2),
        static_cast<double>(highest) - 0.5
    };

    // The integer tap the delay is split at. It is held fixed inside a
    // solve, so the residual stays smooth in the delay (see
    // thiranDelayPhase), and re-derived from each answer by solve() below.
    int splitAnchor = 0;
    // Everything at the three points that the solve does not move - the
    // loss filters and the bending loss - is read once.
    double anchorOmega[3] {};
    double anchorLag[3] {};
    for (int index = 0; index < 3; ++index)
    {
        const double omega = stretchedOmega(anchors[index]);
        const double lossOmega = referenceLossOmega(omega, sampleRate);
        anchorOmega[index] = omega;
        anchorLag[index] = mixedOnePolePhase(broadCoefficient, broadMix,
                                             lossOmega)
            + mixedOnePolePhase(highCoefficient, highMix, lossOmega)
            + bendingLossLag(bendingA1, bendingA2, omega)
            - twoPiDouble * anchors[index];
    }
    const auto evaluate = [&] (const double values[3], double residuals[3])
    {
        double a1 = 0.0;
        double a2 = 0.0;
        secondOrderAllpassCoefficients(
            omega0, values[1], values[2], a1, a2);
        for (int index = 0; index < 3; ++index)
        {
            const double omega = anchorOmega[index];
            residuals[index] = thiranDelayPhase(values[0], splitAnchor, omega)
                + secondOrderAllpassPhase(a1, a2, omega) + anchorLag[index];
        }
    };
    const auto maximumResidual = [] (const double residuals[3])
    {
        return std::max({ exact::abs(residuals[0]), exact::abs(residuals[1]),
                          exact::abs(residuals[2]) });
    };

    // Nine damped Gauss-Newton steps on (delay, decayRatio, poleRatio)
    // against the three collocation residuals, at the tap splitAnchor
    // currently holds. The delay is free: the pole pair's own lag at the
    // fundamental moves it by several samples from the starting point, so a
    // solve that could not leave its tap's fraction band could not converge.
    constexpr double steps[] { 0.02, 0.01, 0.01 };
    const auto refine = [&] (double values[3])
    {
        for (int iteration = 0; iteration < 9; ++iteration)
        {
            double residuals[3] {};
            evaluate(values, residuals);
            const double oldNorm = maximumResidual(residuals);
            if (oldNorm < 1.0e-10)
                break;

            double jacobian[3][3] {};
            for (int column = 0; column < 3; ++column)
            {
                double higher[] { values[0], values[1], values[2] };
                double lower[] { values[0], values[1], values[2] };
                higher[column] += steps[column];
                lower[column] -= steps[column];
                double higherResiduals[3] {};
                double lowerResiduals[3] {};
                evaluate(higher, higherResiduals);
                evaluate(lower, lowerResiduals);
                for (int row = 0; row < 3; ++row)
                    jacobian[row][column] = (higherResiduals[row]
                        - lowerResiduals[row]) / (2.0 * steps[column]);
            }

            const double rhs[] { -residuals[0], -residuals[1], -residuals[2] };
            double update[3] {};
            if (!solveThreeByThree(jacobian, rhs, update))
                break;

            bool accepted = false;
            for (double amount = 1.0; amount >= 0.03125; amount *= 0.5)
            {
                double candidate[] {
                    std::clamp(values[0] + amount * update[0], 3.0,
                        static_cast<double>(localMaximumDelaySamples - 3)),
                    std::clamp(values[1] + amount * update[1], 0.1, 30.0),
                    std::clamp(values[2] + amount * update[2], 0.05, 15.0)
                };
                double candidateResiduals[3] {};
                evaluate(candidate, candidateResiduals);
                if (maximumResidual(candidateResiduals) < oldNorm)
                {
                    std::copy(std::begin(candidate), std::end(candidate),
                        values);
                    accepted = true;
                    break;
                }
            }
            if (!accepted)
                break;
        }
    };
    // Re-derive the tap from the answer and solve again if it moved, so the
    // split the collocation was fitted at is the split the loop reads with.
    // The first round starts from a tap the pole pair has not yet moved the
    // delay off, so it is the one that walks; the second finds its own
    // answer's tap unchanged and only trims the delay by about a thousandth
    // of a sample.
    const auto solve = [&] (double values[3])
    {
        for (int round = 0; round < 4; ++round)
        {
            splitAnchor = delayAnchor(values[0]);
            refine(values);
            if (delayAnchor(values[0]) == splitAnchor)
                break;
        }
        splitAnchor = delayAnchor(values[0]);
    };

    double parameters[] { 128.0, startDecayRatio, startPoleRatio };
    {
        double a1 = 0.0;
        double a2 = 0.0;
        secondOrderAllpassCoefficients(
            omega0, parameters[1], parameters[2], a1, a2);
        parameters[0] = tunedLoopDelay(
            fundamental, sampleRate, broadCoefficient, broadMix,
            highCoefficient, highMix, sections(a1, a2), bendingA1, bendingA2);
    }
    solve(parameters);

    // This is a Newton solve on a non-convex residual, so it can stall in a
    // corner of the (decayRatio, poleRatio) box instead of reaching the
    // near-zero residual a well-posed collocation admits. Which starting
    // points stall is not a property of the string: at 384 kHz a top-fret
    // treble stalls at a residual of 0.40 rad from the default start, where
    // a coarse sweep of the box shows a
    // solution within 0.014 rad of exact, and moving to the Thiran read
    // moved which notes land in which basin. So the fallback restarts the
    // same cheap three-parameter solve from a spread of both starting ratios
    // and keeps the lowest-residual run.
    // It costs nothing on a note whose first solve converged: the sweep is
    // skipped entirely once the residual is below 1e-9.
    {
        double residuals[3] {};
        evaluate(parameters, residuals);
        double bestNorm = maximumResidual(residuals);
        int bestAnchor = splitAnchor;
        for (const double sweepDecayRatio : { 5.0, 10.0, 20.0 })
        for (const double sweepPoleRatio : { 1.0, 2.0, 4.0, 8.0 })
        {
            if (bestNorm < 1.0e-9)
                break;
            double a1 = 0.0;
            double a2 = 0.0;
            secondOrderAllpassCoefficients(
                omega0, sweepDecayRatio, sweepPoleRatio, a1, a2);
            double candidate[] {
                tunedLoopDelay(fundamental, sampleRate, broadCoefficient,
                    broadMix, highCoefficient, highMix, sections(a1, a2),
                    bendingA1, bendingA2),
                sweepDecayRatio, sweepPoleRatio
            };
            solve(candidate);
            double candidateResiduals[3] {};
            evaluate(candidate, candidateResiduals);
            const double candidateNorm = maximumResidual(candidateResiduals);
            if (candidateNorm < bestNorm)
            {
                bestNorm = candidateNorm;
                bestAnchor = splitAnchor;
                std::copy(std::begin(candidate), std::end(candidate),
                    parameters);
            }
        }
        // Every candidate is compared at its own tap, which is the tap the
        // loop would read it with; the winner's has to be put back.
        splitAnchor = bestAnchor;
    }

    calibration.delay = parameters[0];
    calibration.decayRatio[0] = parameters[1];
    calibration.poleRatio[0] = parameters[2];
    secondOrderAllpassCoefficients(omega0, parameters[1], parameters[2],
        calibration.a1[0], calibration.a2[0]);
    return calibration;
}

// The integer partials a dispersion design is fitted to, with everything in
// the loop's phase that the design does not choose. Up to H12, and only
// partials below 0.84 pi (0.42 of the host rate), as collocateDispersion
// has always bounded its points: nearer Nyquist the loss filters' and the
// Thiran read's own phase dominate, and a partial up there has long gone.
struct DispersionPartials
{
    static constexpr int capacity = 12;
    int highest { 0 };
    double omega0 { 0.0 };
    std::array<double, capacity> omega {};
    // Loss-filter and bending-loss lag at the partial, less 2 pi n.
    std::array<double, capacity> fixedLag {};
    std::array<double, capacity> cosine {};
    std::array<double, capacity> sine {};
    std::array<double, capacity> cosine2 {};
    std::array<double, capacity> sine2 {};
    // Residual weights. 1/n turns a lag error into cents: a partial's
    // resonance moves by its lag error over the loop's whole lag there,
    // 2 pi n. The fundamental, which tunedLoopDelay re-tunes exactly at
    // every configuration anyway, is held thirty times harder than that.
    std::array<double, capacity> weight {};
};

DispersionPartials dispersionPartials(
    double inharmonicity, double fundamental, double sampleRate,
    double broadCoefficient, double broadMix,
    double highCoefficient, double highMix,
    double bendingA1, double bendingA2) noexcept
{
    DispersionPartials partials;
    partials.omega0 = 2.0 * piDouble * fundamental / sampleRate;
    const auto stretched = [&] (int partial)
    {
        return partials.omega0 * stretchedPartial(
            static_cast<double>(partial), inharmonicity);
    };
    int highest = DispersionPartials::capacity;
    while (highest > 3 && stretched(highest) > 0.84 * piDouble)
        --highest;
    partials.highest = highest;
    for (int partial = 1; partial <= highest; ++partial)
    {
        const auto index = static_cast<std::size_t>(partial - 1);
        const double omega = stretched(partial);
        const double lossOmega = referenceLossOmega(omega, sampleRate);
        partials.omega[index] = omega;
        partials.fixedLag[index]
            = mixedOnePolePhase(broadCoefficient, broadMix, lossOmega)
            + mixedOnePolePhase(highCoefficient, highMix, lossOmega)
            + bendingLossLag(bendingA1, bendingA2, omega)
            - 2.0 * piDouble * static_cast<double>(partial);
        partials.cosine[index] = std::cos(omega);
        partials.sine[index] = std::sin(omega);
        partials.cosine2[index] = std::cos(2.0 * omega);
        partials.sine2[index] = std::sin(2.0 * omega);
        partials.weight[index] = partial == 1
            ? 30.0 : 1.0 / static_cast<double>(partial);
    }
    return partials;
}

// (1200 / ln 2) / (2 pi): cents per unit of a weighted residual.
constexpr double centsPerWeightedRadian
    = 1200.0 / (0.6931471805599453 * 2.0 * piDouble);

// The loop-phase residuals of a design, weighted, at the partials. The
// design is x = { delay, decayRatio0, poleRatio0, decayRatio1, poleRatio1 }
// read at tap anchor. When jacobian is given it receives the derivatives in
// the delay and in the first section's two ratios, the unknowns the fit
// moves: a section's lag is 2 omega + 2 arg A(e^{j omega}) for its
// denominator A, whose derivative in a coefficient is Im(z^-k / A), and its
// coefficients are smooth in the ratios, so only the delay is differenced.
constexpr int dispersionDesignValues = 1 + 2 * dispersionSectionCount;

void dispersionResiduals(
    const DispersionPartials& partials,
    const double x[dispersionDesignValues], int anchor,
    double residuals[DispersionPartials::capacity],
    double (*jacobian)[3]) noexcept
{
    const double omega0 = partials.omega0;
    double a1[dispersionSectionCount] {};
    double a2[dispersionSectionCount] {};
    for (int section = 0; section < dispersionSectionCount; ++section)
        secondOrderAllpassCoefficients(omega0, x[1 + 2 * section],
            x[2 + 2 * section], a1[section], a2[section]);
    // The first section's coefficients' derivatives in its two ratios.
    const double radius = std::exp(-omega0 * x[1]);
    const double angle = omega0 * x[2];
    const double a1ByDecay = 2.0 * omega0 * radius * std::cos(angle);
    const double a1ByPole = 2.0 * omega0 * radius * std::sin(angle);
    const double a2ByDecay = -2.0 * omega0 * radius * radius;
    constexpr double delayStep = 1.0e-3;
    double thiran[3][2] {};
    thiranCoefficients(x[0] - static_cast<double>(anchor),
                       thiran[0][0], thiran[0][1]);
    thiranCoefficients(x[0] + delayStep - static_cast<double>(anchor),
                       thiran[1][0], thiran[1][1]);
    thiranCoefficients(x[0] - delayStep - static_cast<double>(anchor),
                       thiran[2][0], thiran[2][1]);
    for (int index = 0; index < partials.highest; ++index)
    {
        const auto item = static_cast<std::size_t>(index);
        const double omega = partials.omega[item];
        const double c1 = partials.cosine[item];
        const double s1 = partials.sine[item];
        const double c2 = partials.cosine2[item];
        const double s2 = partials.sine2[item];
        const double weight = partials.weight[item];
        // Tap plus Thiran allpass: the lag thiranDelayPhase returns.
        const auto read = [&] (const double* coefficients)
        {
            return static_cast<double>(anchor + 2) * omega + 2.0 * std::atan2(
                -coefficients[0] * s1 - coefficients[1] * s2,
                1.0 + coefficients[0] * c1 + coefficients[1] * c2);
        };
        double lag = read(thiran[0]) + partials.fixedLag[item];
        for (int section = 0; section < dispersionSectionCount; ++section)
        {
            // An unused second section is bypassed.
            if (section == 1 && !(x[3] > 0.0))
                continue;
            const double real = 1.0 + a1[section] * c1 + a2[section] * c2;
            const double imaginary = -a1[section] * s1 - a2[section] * s2;
            lag += 2.0 * omega + 2.0 * std::atan2(imaginary, real);
            if (jacobian != nullptr && section == 0)
            {
                const double norm = real * real + imaginary * imaginary;
                const double byA1 = (-s1 * real - c1 * imaginary) / norm;
                const double byA2 = (-s2 * real - c2 * imaginary) / norm;
                jacobian[index][1] = 2.0 * weight
                    * (byA1 * a1ByDecay + byA2 * a2ByDecay);
                jacobian[index][2] = 2.0 * weight * byA1 * a1ByPole;
            }
        }
        residuals[index] = weight * lag;
        if (jacobian != nullptr)
            jacobian[index][0] = weight * (read(thiran[1]) - read(thiran[2]))
                               / (2.0 * delayStep);
    }
}

double worstPartialCents(const DispersionPartials& partials,
                         const double residuals[DispersionPartials::capacity])
    noexcept
{
    double worst = 0.0;
    for (int index = 1; index < partials.highest; ++index)
        worst = std::max(worst, exact::abs(residuals[index]));
    return worst * centsPerWeightedRadian;
}

// Levenberg-Marquardt on the weighted residuals for the delay and the first
// section's two ratios, from x, at the tap its delay falls on, re-derived
// from the answer as in collocateDispersion. The ratios are held inside the
// box the pole pair is meaningful in (a decay ratio of 0.1 to 40, a pole
// ratio of 0.05 to 20 fundamentals) and pulled towards where they started,
// with a weight of `pull` per unit of relative change: where the stiffness
// leaves one section a whole valley of equally good fits, the pull picks the
// one nearest a start that moves smoothly with B, so a bend or a vibrato,
// which moves B, moves the design smoothly too instead of jumping between
// fits, each jump a step in the tuned delay the loop would have to slew
// across. For the same reason it runs to convergence - until a step improves
// the fit by less than a part in 1e10 - not to a tolerance: the answer is a
// function of the arguments, not of how far the iteration happened to get.
void refineDispersionDesign(const DispersionPartials& partials,
                            double x[dispersionDesignValues],
                            double pull) noexcept
{
    constexpr int capacity = DispersionPartials::capacity;
    const int count = partials.highest;
    const double startDecay = x[1];
    const double startPole = x[2];
    const auto bound = [] (double values[dispersionDesignValues])
    {
        values[0] = std::clamp(values[0], 3.0,
            static_cast<double>(localMaximumDelaySamples - 3));
        values[1] = std::clamp(values[1], 0.1, 40.0);
        values[2] = std::clamp(values[2], 0.05, 20.0);
    };
    const auto cost = [&] (const double residuals[capacity],
                           const double values[dispersionDesignValues])
    {
        double sum = 0.0;
        for (int index = 0; index < count; ++index)
            sum += residuals[index] * residuals[index];
        const double decayOffset = pull * (values[1] - startDecay) / startDecay;
        const double poleOffset = pull * (values[2] - startPole) / startPole;
        return sum + decayOffset * decayOffset + poleOffset * poleOffset;
    };
    const double pullScale[3] { 0.0, pull / startDecay, pull / startPole };
    for (int round = 0; round < 4; ++round)
    {
        const int anchor = delayAnchor(x[0]);
        double residuals[capacity] {};
        double jacobian[capacity][3] {};
        dispersionResiduals(partials, x, anchor, residuals, jacobian);
        double current = cost(residuals, x);
        double damping = 1.0e-4;
        for (int iteration = 0; iteration < 60; ++iteration)
        {
            double normal[3][3] {};
            double gradient[3] {};
            for (int row = 0; row < 3; ++row)
            {
                for (int column = 0; column < 3; ++column)
                    for (int index = 0; index < count; ++index)
                        normal[row][column] += jacobian[index][row]
                                             * jacobian[index][column];
                for (int index = 0; index < count; ++index)
                    gradient[row] -= jacobian[index][row] * residuals[index];
                const double start = row == 1 ? startDecay : startPole;
                normal[row][row] += pullScale[row] * pullScale[row];
                if (row > 0)
                    gradient[row] -= pullScale[row] * pullScale[row]
                                   * (x[row] - start);
            }
            bool improved = false;
            double improvement = 0.0;
            for (int attempt = 0; attempt < 10 && !improved; ++attempt)
            {
                double damped[3][3] {};
                for (int row = 0; row < 3; ++row)
                    for (int column = 0; column < 3; ++column)
                        damped[row][column] = normal[row][column]
                            + (row == column
                                ? damping * normal[row][row] + 1.0e-15 : 0.0);
                double step[3] {};
                if (!solveThreeByThree(damped, gradient, step))
                {
                    damping *= 8.0;
                    continue;
                }
                double candidate[dispersionDesignValues] {};
                std::copy(x, x + dispersionDesignValues, candidate);
                for (int item = 0; item < 3; ++item)
                    candidate[item] += step[item];
                bound(candidate);
                double candidateResiduals[capacity] {};
                dispersionResiduals(partials, candidate, anchor,
                                    candidateResiduals, nullptr);
                const double candidateCost = cost(candidateResiduals,
                                                  candidate);
                if (candidateCost < current)
                {
                    improvement = (current - candidateCost) / current;
                    std::copy(candidate, candidate + dispersionDesignValues,
                              x);
                    current = candidateCost;
                    damping = std::max(damping * 0.2, 1.0e-12);
                    improved = true;
                }
                else
                    damping *= 8.0;
            }
            if (!improved || improvement < 1.0e-10)
                break;
            dispersionResiduals(partials, x, anchor, residuals, jacobian);
        }
        if (delayAnchor(x[0]) == anchor)
            break;
    }
}

// The loop's dispersion: two second-order allpass sections in cascade,
// fitted to the stiff-string law f_n = n f0 sqrt((1 + B n^2) / (1 + B)) at
// the integer partials from H1 to H12 below 0.42 of the host rate
// (DispersionPartials), the error in each counted in cents. The
// fundamental is then re-tuned exactly by tunedLoopDelay at every
// configuration.
//
// The design before it was one section fitted by collocation at H1, H7 and
// H11.5 (collocateDispersion), which met the law exactly there and dipped
// between: E2 at the 20th fret, B N^2 near 0.12 at N = 12 partials, had
// H3-H5 16-18 cents flat and H9 6 cents sharp at every rate, and every
// wound string high on the neck missed some partial by more than 3 cents.
// No single section does much better on those notes - a least-squares or
// minimax fit of one still misses E2's 20th fret by over 6 cents - because
// the stiff string's group delay falls smoothly across the whole band and
// one resonant pole pair bends it in one place. A second pair spreads that
// bend over the band.
//
// Where the collocation was already right the design is left as it was.
// Up to B N^2 = 0.015 - in the standard tuning at 44.1 kHz and up, every
// string below its 3rd (low E), 7th (A), 9th (D), 12th (G), 8th (B) or
// 18th (high E) fret - the collocated section is the design, unchanged,
// and the second section is unused and bypassed, so the loop is exactly
// the loop it was. From there to B N^2 = 0.05 the second section is
// switched in by a smoothstep share of its pole radius - at a vanishing
// share a two-sample delay, which the loop takes over from its delay line
// without a step (StringLoop::switchSecondDispersion) - and the first
// section and the delay are refitted by least squares
// (refineDispersionDesign) from the collocated section, held to it by a
// pull that falls with the fourth power of the share left to go, to 0.01
// from there on. So the design moves continuously from the collocation to
// the two-section fit as the string stiffens: a bend or a vibrato, which
// moves B, cannot make it jump. The second section is not fitted but
// placed from the stiffness alone, at a decay ratio 1.05 D and a pole ratio
// 2.95 D^0.48 N / 12 for D = 1.45 / (N sqrt B), where full four-ratio fits
// across the fretboard put both sections' decays near D and the second
// pole a little above half-way up the band; fitting it too left shallow
// valleys and separate minima that a small move of B jumped between. If a
// design still misses some partial by more than 3 cents (at 32 kHz and up
// that never happens), a spread of starts with the second section fully in
// is tried and the best of everything kept.
//
// Every note of the rate/fret matrix (six strings, frets 0-20, 32 to
// 384 kHz, every tuning) then places H2 to H12 within 2.0 cents of the law,
// and within 1.1 cents in the standard tuning at 44.1 to 96 kHz, measured as
// the loop's own resonances with the bridge off. Below 32 kHz fewer partials
// fit under Nyquist and the loss filters' own phase dominates the top ones;
// there some trebles still miss by up to 9 cents at 11 kHz, none by more
// than the collocation alone missed them. A 0.2% move of B, the step at
// which the cache re-solves under a bend or a vibrato, moves the tuned delay
// by at most 1 cent's worth at 44.1 to 96 kHz in every tuning, outside the
// stalls the collocation already had: the collocation alone moved it by up
// to 0.3 away from its tap band edge (delayAnchor) and 1 at it, and the rest
// is the second section switching in. The design costs about 55
// microseconds a note at 44.1 to 96 kHz, against 80 for the collocation
// alone before (whose solve now reads the phases it does not move once),
// and is cached (dispersionSolves_) and redone only when its inputs change.
DispersionCalibration calibrateDispersion(
    double inharmonicity, double fundamental, double sampleRate,
    double broadCoefficient, double broadMix,
    double highCoefficient, double highMix,
    double bendingA1 = 0.0, double bendingA2 = 0.0) noexcept
{
    DispersionCalibration calibration;
    const double omega0 = 2.0 * piDouble * fundamental / sampleRate;
    // Coefficients at the design frequency, and the delay that tunes the
    // fundamental exactly with them.
    const auto finish = [&] (DispersionCalibration& design)
    {
        DispersionSections sections;
        for (std::size_t section = 0; section < 2; ++section)
        {
            secondOrderAllpassCoefficients(omega0, design.decayRatio[section],
                design.poleRatio[section], sections.a1[section],
                sections.a2[section]);
        }
        sections.used[1] = design.decayRatio[1] > 0.0;
        design.a1 = sections.a1;
        design.a2 = sections.a2;
        design.delay = tunedLoopDelay(fundamental, sampleRate,
            broadCoefficient, broadMix, highCoefficient, highMix, sections,
            bendingA1, bendingA2);
    };
    if (!(inharmonicity > 1.0e-8) || !(omega0 > 1.0e-7))
    {
        // Nothing to disperse: the first section a plain two-sample delay
        // and the second bypassed.
        finish(calibration);
        return calibration;
    }

    const auto partials = dispersionPartials(inharmonicity, fundamental,
        sampleRate, broadCoefficient, broadMix, highCoefficient, highMix,
        bendingA1, bendingA2);
    const double fitted = static_cast<double>(partials.highest);
    const double stiffness = 1.45 / (fitted * exact::sqrt(inharmonicity));
    // The worst integer partial of a design, in cents, with the delay that
    // makes its fundamental exact, as the loop will run it.
    const auto worstOf = [&] (DispersionCalibration& design)
    {
        finish(design);
        const double x[] { design.delay, design.decayRatio[0],
            design.poleRatio[0], design.decayRatio[1], design.poleRatio[1] };
        double residuals[DispersionPartials::capacity] {};
        dispersionResiduals(partials, x, delayAnchor(design.delay),
                            residuals, nullptr);
        return worstPartialCents(partials, residuals);
    };
    // How far the second section is switched in: none up to B N^2 = 0.015,
    // all of it from 0.05, and a smoothstep between.
    const double stiffnessShare = [&]
    {
        const double position = std::clamp(
            (inharmonicity * fitted * fitted - 0.015) / (0.05 - 0.015),
            0.0, 1.0);
        return position * position * (3.0 - 2.0 * position);
    }();
    auto collocated = collocateDispersion(inharmonicity, fundamental,
        sampleRate, broadCoefficient, broadMix, highCoefficient, highMix,
        10.0, 4.0, bendingA1, bendingA2);
    constexpr double tolerance = 3.0;
    const double collocatedWorst = worstOf(collocated);
    if (!(stiffnessShare > 0.0) && collocatedWorst <= tolerance)
        return collocated;

    // The second section at a share of its full strength: its pole radius
    // scaled by the share, so a vanishing share is a two-sample delay, the
    // point at which the loop switches it in and out
    // (StringLoop::switchSecondDispersion).
    const auto fitFrom = [&] (double decay, double pole, double share,
                              double pull)
    {
        DispersionCalibration design;
        const double second = std::clamp(1.05 * stiffness, 0.5, 40.0);
        design.decayRatio = { decay, second - std::log(share) / omega0 };
        design.poleRatio = { pole,
                             2.95 * std::pow(second, 0.48) * fitted / 12.0 };
        finish(design);
        double x[] { design.delay, design.decayRatio[0], design.poleRatio[0],
                     design.decayRatio[1], design.poleRatio[1] };
        refineDispersionDesign(partials, x, pull);
        design.decayRatio[0] = x[1];
        design.poleRatio[0] = x[2];
        return design;
    };

    double worst = collocatedWorst;
    calibration = collocated;
    if (stiffnessShare > 0.0)
    {
        const double release = (1.0 - stiffnessShare) * (1.0 - stiffnessShare);
        calibration = fitFrom(collocated.decayRatio[0],
                              collocated.poleRatio[0], stiffnessShare,
                              0.01 + release * release);
        worst = worstOf(calibration);
    }
    // A fallback only, for a design still more than 3 cents out: a spread of
    // starts, with the second section fully in where the collocation was
    // the answer, and the best of everything, the collocation included,
    // kept.
    const double fallbackShare = stiffnessShare > 0.0 ? stiffnessShare : 1.0;
    constexpr double decayScales[] { 1.0, 0.6, 1.7, 0.35 };
    constexpr double poles[] { 0.19, 0.1, 0.3, 0.05 };
    for (int decayIndex = 0; decayIndex < 4 && worst > tolerance; ++decayIndex)
    for (int poleIndex = 0; poleIndex < 4 && worst > tolerance; ++poleIndex)
    {
        auto candidate = fitFrom(
            std::clamp(decayScales[decayIndex] * stiffness, 0.5, 35.0),
            poles[poleIndex] * fitted, fallbackShare, 0.01);
        const double candidateWorst = worstOf(candidate);
        if (candidateWorst < worst)
        {
            worst = candidateWorst;
            calibration = candidate;
        }
    }
    if (worst > tolerance && collocatedWorst < worst)
        calibration = collocated;
    finish(calibration);
    return calibration;
}
} // namespace

AcustraEngine::AcustraEngine() noexcept
{
    // -1 is the "nothing received yet" sentinel for both; see mpeTimbre_ /
    // mpePressure_'s declaration. reset() re-applies this on every prepare(),
    // but the sentinel has to hold from construction too, before the first
    // prepare() -- the {} default-member-initialiser above zero-inits them,
    // which is a different, meaningful value (0 is a real received CC74/
    // pressure of "none"/"no grip"), not this sentinel.
    mpeTimbre_.fill(-1.0f);
    mpePressure_.fill(-1.0f);

    parameters_ = sanitise(parameters_);
    targetParameters_ = parameters_;
    configurePiezoUnit();
    const auto notes = openNotes(parameters_.tuning);
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        voice.openMidi = notes[static_cast<std::size_t>(string)];
        voice.midiNote = voice.openMidi;
    }
    restartRandomDraws();
}

// Every random draw starts again from the constructor's seeds, and every
// bridge anchor from its constructed value, so a prepared engine plays as a
// new one does, whatever it played before or at whichever rate. The anchors
// belong here because reset() settles the open strings' delays from every
// string's anchor at that moment (see bridgePortMobility). reset() alone
// does not restart the draws: a panic is not a new performance, and the
// strums after it keep varying as repeated real strums do.
void AcustraEngine::restartRandomDraws() noexcept
{
    strumRandomState_ = 0x9e3779b9u;
    strumSpeedScale_ = 1.0f;
    strumParallelSign_ = 1.0f;
    pickingGestureRandom_ = 0x7f4a7c15u;
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        voice.randomState = 0x9e3779b9u
            ^ (0x85ebca6bu * static_cast<std::uint32_t>(string + 1));
        voice.repeatPluckState = 0xa511e9b3u
            ^ (0x63d83595u * static_cast<std::uint32_t>(string + 1));
        voice.contactNoiseState = 0x2545f491u
            ^ (0x9e3779b9u * static_cast<std::uint32_t>(string + 1));
        voice.releaseNoiseState = 0x68e31da4u
            ^ (0x85ebca6bu * static_cast<std::uint32_t>(string + 7));
        voice.legatoFrictionState = 0xd1b54a35u
            ^ (0x9e3779b9u * static_cast<std::uint32_t>(string + 1));
        voice.bridgeTailStiffness = initialBridgeTailStiffness;
        voice.appliedBridgeTailStiffness = initialBridgeTailStiffness;
        voice.bridgeTailStiffnessStep = 0.0f;
        voice.bridgeTailStiffnessSamples = 0;
    }
}

float AcustraEngine::clamp(float value, float low, float high) noexcept
{
    if (!exact::isfinite(value))
        return low;
    return std::max(low, std::min(high, value));
}

EngineParameters AcustraEngine::sanitise(const EngineParameters& source) noexcept
{
    EngineParameters result = source;
    const auto enumOr = [] (int value, int maximum, int fallback)
    {
        return value >= 0 && value <= maximum ? value : fallback;
    };
    result.shape = static_cast<BodyShape>(enumOr(
        static_cast<int>(source.shape), 3,
        static_cast<int>(EngineParameters {}.shape)));
    result.bodyMaterial = static_cast<BodyMaterial>(enumOr(
        static_cast<int>(source.bodyMaterial), 2,
        static_cast<int>(EngineParameters {}.bodyMaterial)));
    switch (source.capture)
    {
        case CaptureType::TrebleMic:
        case CaptureType::BassMic:
        case CaptureType::UpperMic:
        case CaptureType::MonoMic:
            result.capture = CaptureType::MonoMic;
            break;
        case CaptureType::SaddlePiezo:
        case CaptureType::Magnetic:
        case CaptureType::LoadedPiezo:
            result.capture = CaptureType::Piezo;
            break;
        default:
            result.capture = CaptureType::StereoMic;
            break;
    }
    result.tuning = static_cast<Tuning>(enumOr(
        static_cast<int>(source.tuning), 4,
        static_cast<int>(EngineParameters {}.tuning)));
    result.picking = static_cast<PickingTechnique>(enumOr(
        static_cast<int>(source.picking), 2,
        static_cast<int>(EngineParameters {}.picking)));
    // A retired model's value (2-4) plays Original.
    result.guitarModel = static_cast<GuitarModel>(enumOr(
        static_cast<int>(source.guitarModel), 1,
        static_cast<int>(GuitarModel::Original)));
    result.stringAge = clamp(source.stringAge, 0.0f, 1.0f);
    result.pluckPosition = clamp(source.pluckPosition, 0.0f, 1.0f);
    result.touch = clamp(source.touch, 0.0f, 1.0f);
    result.bodyAmount = clamp(source.bodyAmount, 0.0f, 1.0f);
    result.stereoWidth = clamp(source.stereoWidth, 0.0f, 1.0f);
    result.outputGain = clamp(source.outputGain, 0.0f, 4.0f);
    result.releaseNoise = clamp(source.releaseNoise, 0.0f, 1.0f);
    result.piezoMix = clamp(source.piezoMix, 0.0f, 1.0f);
    result.room = clamp(source.room, 0.0f, 1.0f);
    return result;
}

PhysicalCalibration AcustraEngine::sanitise(
    const PhysicalCalibration& source) noexcept
{
    const auto bounded = [] (float value, float low, float high,
                             float fallback) noexcept
    {
        return exact::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    const auto material = [&] (const MaterialCalibration& value,
                               const MaterialCalibration& fallback) noexcept
    {
        return MaterialCalibration {
            bounded(value.stiffnessScale, 0.25f, 4.0f,
                    fallback.stiffnessScale),
            bounded(value.fundamentalT60Scale, 0.4f, 2.0f,
                    fallback.fundamentalT60Scale),
            bounded(value.frequencyLossScale, 0.35f, 3.0f,
                    fallback.frequencyLossScale),
            bounded(value.apertureScale, 0.35f, 2.5f,
                    fallback.apertureScale),
            bounded(value.transientScale, 0.0f, 3.0f,
                    fallback.transientScale),
            bounded(value.pluckDistanceScale, 0.7f, 3.0f,
                    fallback.pluckDistanceScale),
            bounded(value.velocityBrightnessDepth, 0.0f, 1.2f,
                    fallback.velocityBrightnessDepth)
        };
    };

    return {
        bounded(source.bodyFrequencyScale, 0.96f, 1.04f,
                fittedPhysicalCalibration.bodyFrequencyScale),
        bounded(source.bodyQScale, 0.05f, 1.8f,
                fittedPhysicalCalibration.bodyQScale),
        bounded(source.bridgeMobilityScale, 0.25f, 4.0f,
                fittedPhysicalCalibration.bridgeMobilityScale),
        bounded(source.residueTiltDbPerOctave, -6.0f, 6.0f,
                fittedPhysicalCalibration.residueTiltDbPerOctave),
        bounded(source.directGain, 0.0f, 0.12f,
                fittedPhysicalCalibration.directGain),
        material(source.steel, fittedPhysicalCalibration.steel),
        bounded(source.apertureRegisterExponent, -1.0f, 1.0f,
                fittedPhysicalCalibration.apertureRegisterExponent),
        bounded(source.lowBodyModeGain, 0.25f, 32.0f,
                fittedPhysicalCalibration.lowBodyModeGain),
        bounded(source.steelDisplacementScaleMetres, 0.0f, 0.04f,
                fittedPhysicalCalibration.steelDisplacementScaleMetres),
        bounded(source.steelFretT60Slope, -0.06f, 0.05f,
                fittedPhysicalCalibration.steelFretT60Slope),
        bounded(source.highLossCutoffScale, 0.5f, 4.0f,
                fittedPhysicalCalibration.highLossCutoffScale),
        bounded(source.bridgeConductanceFloor, 0.0f, 0.02f,
                fittedPhysicalCalibration.bridgeConductanceFloor),
        bounded(source.bridgeConductanceCornerHz, 100.0f, 8000.0f,
                fittedPhysicalCalibration.bridgeConductanceCornerHz),
        bounded(source.bridgeTailLengthMetres, 0.00325f, 0.060f,
                fittedPhysicalCalibration.bridgeTailLengthMetres),
        bounded(source.longitudinalGain, 0.0f, 0.5f,
                fittedPhysicalCalibration.longitudinalGain),
        bounded(source.longitudinalQ, 10.0f, 400.0f,
                fittedPhysicalCalibration.longitudinalQ),
        bounded(source.polarisationEndCorrectionMetres, 0.0f, 0.82e-3f,
                fittedPhysicalCalibration.polarisationEndCorrectionMetres),
        bounded(source.pickReleaseVelocityShare, 0.0f, 2.0f,
                fittedPhysicalCalibration.pickReleaseVelocityShare),
        bounded(source.pickReleaseVelocityExponent, 0.0f, 4.0f,
                fittedPhysicalCalibration.pickReleaseVelocityExponent),
        bounded(source.pickTransientGain, 0.0f, 8.0f,
                fittedPhysicalCalibration.pickTransientGain),
        bounded(source.pickEdgeRadiusMetres, 0.0f, 1.0e-3f,
                fittedPhysicalCalibration.pickEdgeRadiusMetres),
        bounded(source.steelWoundBendingLoss, 0.0f, 2.0f,
                fittedPhysicalCalibration.steelWoundBendingLoss),
        bounded(source.steelPlainBendingLoss, 0.0f, 2.0f,
                fittedPhysicalCalibration.steelPlainBendingLoss),
        bounded(source.contactNoiseFinger, 0.0f, 4.0f,
                fittedPhysicalCalibration.contactNoiseFinger),
        bounded(source.contactNoisePick, 0.0f, 4.0f,
                fittedPhysicalCalibration.contactNoisePick),
        bounded(source.contactNoiseVelocityExponent, 0.0f, 4.0f,
                fittedPhysicalCalibration.contactNoiseVelocityExponent),
        bounded(source.contactNoiseCornerHz, 100.0f, 20000.0f,
                fittedPhysicalCalibration.contactNoiseCornerHz),
        bounded(source.pickContactNoiseCornerHz, 100.0f, 20000.0f,
                fittedPhysicalCalibration.pickContactNoiseCornerHz),
        bounded(source.contactNoiseDecaySeconds, 0.0005f, 0.05f,
                fittedPhysicalCalibration.contactNoiseDecaySeconds),
        bounded(source.contactClickFinger, 0.0f, 64.0f,
                fittedPhysicalCalibration.contactClickFinger),
        bounded(source.contactClickPick, 0.0f, 64.0f,
                fittedPhysicalCalibration.contactClickPick)
    };
}

std::array<int, AcustraEngine::stringCount>
AcustraEngine::openNotes(Tuning tuning) noexcept
{
    switch (tuning)
    {
        case Tuning::DropD:        return { 38, 45, 50, 55, 59, 64 };
        case Tuning::Dadgad:       return { 38, 45, 50, 55, 57, 62 };
        case Tuning::OpenG:        return { 38, 43, 50, 55, 59, 62 };
        case Tuning::HalfStepDown: return { 39, 44, 49, 54, 58, 63 };
        case Tuning::Standard:     return { 40, 45, 50, 55, 59, 64 };
    }
    return { 40, 45, 50, 55, 59, 64 };
}

float AcustraEngine::midiFrequency(int midiNote) noexcept
{
    return 440.0f * std::exp2((static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

float AcustraEngine::phaseDelayForOnePoleMix(float coefficient, float mix,
                                             float omega) noexcept
{
    if (!(omega > 1.0e-7f))
        return mix * coefficient / std::max(1.0f - coefficient, 1.0e-5f);

    // H = (1-m) + m(1-c)/(1-c z^-1), evaluated directly so the
    // fractional-delay target includes the loss filter's actual phase.
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float denominatorReal = 1.0f - coefficient * cosine;
    const float denominatorImag = coefficient * sine;
    const float denominatorNorm = denominatorReal * denominatorReal
                                + denominatorImag * denominatorImag;
    const float lowReal = (1.0f - coefficient) * denominatorReal
                        / denominatorNorm;
    const float lowImag = -(1.0f - coefficient) * denominatorImag
                        / denominatorNorm;
    const float real = (1.0f - mix) + mix * lowReal;
    const float imaginary = mix * lowImag;
    return -std::atan2(imaginary, real) / omega;
}

float AcustraEngine::magnitudeForOnePoleMix(float coefficient, float mix,
                                            float omega) noexcept
{
    const float cosine = std::cos(omega);
    const float sine = std::sin(omega);
    const float denominatorReal = 1.0f - coefficient * cosine;
    const float denominatorImag = coefficient * sine;
    const float denominatorNorm = denominatorReal * denominatorReal
                                + denominatorImag * denominatorImag;
    const float lowReal = (1.0f - coefficient) * denominatorReal
                        / denominatorNorm;
    const float lowImag = -(1.0f - coefficient) * denominatorImag
                        / denominatorNorm;
    return std::hypot((1.0f - mix) + mix * lowReal,
                      mix * lowImag);
}

float AcustraEngine::registeredPluckAperture(
    float apertureSamples, float apertureScale, float referenceDelay,
    float currentReferenceLength, float exponent) noexcept
{
    // Keep the promoted exponent-one model bit-for-bit on its original path.
    if (exponent == 1.0f)
        return apertureSamples * apertureScale
            / std::max(currentReferenceLength, 8.0f);

    const float boundedLength = std::max(currentReferenceLength, 8.0f);
    return apertureSamples * apertureScale / referenceDelay
        * std::pow(referenceDelay / boundedLength, exponent);
}

void AcustraEngine::OnePole::configureRate(float referencePole,
                                           double sampleRate) noexcept
{
    remapped = sampleRate != 48000.0;
    if (!remapped)
        return;
    // Preserve the calibrated 48 kHz transfer (1-c)/(1-c*z^-1), rather than
    // moving its cutoff to each host's Nyquist clamp. Inverse bilinear at
    // 48 kHz and bilinear at Fs substitutes z_ref^-1=(w+z^-1)/(1+w*z^-1),
    // w=(48000-Fs)/(48000+Fs). This retains its continuous shelf prototype;
    // it does not remove the bilinear transform's frequency warping.
    // https://www.mathworks.com/help/signal/ref/bilinear.html
    // The numerator is essential: discarding its delayed-input term creates
    // a different loss curve. The recurrence uses b0=1-p-b1 implicitly,
    // keeping DC gain exactly one instead of rounding three coefficients
    // independently. 1-c*w stays positive at every supported rate;
    // unlike solving for a new mix, this never divides by a vanishing pole.
    const double warp = (48000.0 - sampleRate) / (48000.0 + sampleRate);
    const double denominator = 1.0 - referencePole * warp;
    delayedInputGain = static_cast<float>(warp * (1.0 - referencePole) / denominator);
    ratePole = static_cast<float>((referencePole - warp) / denominator);
}

void AcustraEngine::StringLoop::reset() noexcept
{
    delay.fill(0.0f);
    writeIndex = 0;
    allpassY1 = 0.0f;
    allpassY2 = 0.0f;
    readDelayValid = false;
    broadLossFilter.reset();
    lossFilter.reset();
    bendingLossY1 = 0.0f;
    bendingLossY2 = 0.0f;
    bendingLossSeed = false;
    dispersion.reset();
    secondDispersion.reset();
    bridgeDerivative.reset();
    derivativeNeedsPriming = true;
    derivativeCrossesContact = false;
    appliedReleaseGain = 1.0f;
    requestedReleaseGain = 1.0f;
    releaseGainStep = 0.0f;
    gestureContact = {};
    if (loopGainTransitionSamples > 0)
        loopGain = targetLoopGain;
    targetLoopGain = loopGain;
    loopGainStep = 0.0f;
    loopGainTransitionSamples = 0;
    if (intrinsicCoefficientSamples > 0)
    {
        const std::array<float*, 7> applied { &bendingLossGain, &bendingLossA1,
            &bendingLossA2, &dispersionA1, &dispersionA2,
            &secondDispersionA1, &secondDispersionA2 };
        for (std::size_t index = 0; index < applied.size(); ++index)
            *applied[index] = intrinsicCoefficientTarget[index];
    }
    intrinsicCoefficientTarget = { bendingLossGain, bendingLossA1,
        bendingLossA2, dispersionA1, dispersionA2,
        secondDispersionA1, secondDispersionA2 };
    intrinsicCoefficientStep.fill(0.0f);
    intrinsicCoefficientSamples = 0;
}

void AcustraEngine::StringLoop::setIntrinsicCoefficients(
    const std::array<float, 7>& target, bool transition) noexcept
{
    // Repeated/forced configuration of the same target must not change its
    // applied values, step bits or remaining deadline.
    bool targetChanged = false;
    for (std::size_t index = 0; index < target.size(); ++index)
        targetChanged |= exact::bits(target[index])
            != exact::bits(intrinsicCoefficientTarget[index]);
    if (intrinsicCoefficientSamples > 0 && !targetChanged)
        return;
    const std::array<float*, 7> applied { &bendingLossGain, &bendingLossA1,
        &bendingLossA2, &dispersionA1, &dispersionA2,
        &secondDispersionA1, &secondDispersionA2 };
    bool changed = false;
    for (std::size_t index = 0; index < applied.size(); ++index)
        changed |= *applied[index] != target[index];
    if (transition && targetChanged && changed)
        intrinsicCoefficientSamples = std::max(1, static_cast<int>(
            std::ceil(currentDelay)));
    // Interpolation stays inside each section's Schur-stable coefficient
    // triangle. The bending numerator follows its unit-DC denominator.
    // This is a bounded change of the existing filters, not a claim of
    // passivity for arbitrary time-varying coefficients.
    intrinsicCoefficientTarget = target;
    for (std::size_t index = 0; index < applied.size(); ++index)
    {
        if (intrinsicCoefficientSamples > 0)
            intrinsicCoefficientStep[index] = (target[index] - *applied[index])
                / intrinsicCoefficientSamples;
        else
            *applied[index] = target[index];
    }
}

void AcustraEngine::StringLoop::setLoopGain(float gain, bool transition) noexcept
{
    // A forced configuration and a cache hit must follow the same physical
    // transition. Recomputing an unchanged target's step from its rounded
    // current gain changes that trajectory.
    if (loopGainTransitionSamples > 0
        && exact::bits(gain) == exact::bits(targetLoopGain))
        return;
    if (transition && gain != targetLoopGain && gain != loopGain)
        loopGainTransitionSamples = std::max(1, static_cast<int>(
            std::ceil(currentDelay)));
    targetLoopGain = gain;
    if (loopGainTransitionSamples > 0)
        // Continuous control revisions keep the remaining deadline; a new
        // discrete retune starts a whole round trip from the applied loss.
        // Even a step below one float ULP reaches its exact target at the end.
        loopGainStep = (gain - loopGain) / loopGainTransitionSamples;
    else
        loopGain = gain;
}

float AcustraEngine::StringLoop::bridgeVelocity(
    float incident, float sampleRateRatio) noexcept
{
    if (derivativeNeedsPriming)
    {
        bridgeDerivative.reset(incident);
        derivativeNeedsPriming = false;
        derivativeCrossesContact = false;
    }
    else if (derivativeCrossesContact)
    {
        derivativeCrossesContact = false;
        derivativeCrossesRelease = false;
        return bridgeDerivative.processAcrossStep(incident, sampleRateRatio);
    }
    else if (derivativeCrossesRelease)
    {
        // The release gain is a loss per round trip, but advance() applies it
        // to the sample it returns, so the moment it changes the whole wave
        // steps by that factor at once. The hand landing on a string damps it;
        // it does not move it, so the step is not motion either.
        derivativeCrossesRelease = false;
        derivativeCrossesContact = false;
        return bridgeDerivative.processAcrossRelease(
            incident, sampleRateRatio);
    }
    return bridgeDerivative.process(incident, sampleRateRatio);
}

float AcustraEngine::FixedDerivative::process(float input,
                                              float sampleRateRatio) noexcept
{
    history[static_cast<std::size_t>(index)] = input;
    // sampleRateRatio samples span exactly the 48 kHz reference period, so
    // this finite difference already has one host-rate-independent scale.
    const auto rateBits = exact::bits(sampleRateRatio);
    if (!geometryValid || rateBits != geometryRateBits)
    {
        const float historyDelay = AcustraEngine::clamp(
            sampleRateRatio, 0.1f, 8.0f);
        geometryWhole = static_cast<int>(historyDelay);
        geometryFraction = historyDelay - static_cast<float>(geometryWhole);
        geometryRateBits = rateBits;
        geometryValid = true;
    }
    const int whole = geometryWhole;
    const float fraction = geometryFraction;
    const auto at = [&] (int samplesAgo)
    {
        const unsigned readIndex = (static_cast<unsigned>(index)
            - static_cast<unsigned>(samplesAgo)) & historyMask;
        return history[readIndex];
    };
    const float first = at(whole);
    const float delayed = first + fraction * (at(whole + 1) - first);
    index = static_cast<int>((static_cast<unsigned>(index) + 1) & historyMask);
    return input - delayed;
}

float AcustraEngine::FixedDerivative::processAcrossRelease(
    float input, float sampleRateRatio) noexcept
{
    int previous = index - 1;
    while (previous < 0)
        previous += static_cast<int>(history.size());
    const float shift = input - history[static_cast<std::size_t>(previous)];
    for (auto& value : history)
        value += shift;
    return process(input, sampleRateRatio);
}

float AcustraEngine::FixedDerivative::processAcrossStep(
    float input, float sampleRateRatio) noexcept
{
    int previous = index - 1;
    while (previous < 0)
        previous += static_cast<int>(history.size());
    int earlier = previous - 1;
    while (earlier < 0)
        earlier += static_cast<int>(history.size());
    // Where the wave would have been had nothing stepped: the last two
    // samples' line carried one sample on.
    const float extrapolated = 2.0f * history[static_cast<std::size_t>(previous)]
        - history[static_cast<std::size_t>(earlier)];
    const float shift = input - extrapolated;
    for (auto& value : history)
        value += shift;
    return process(input, sampleRateRatio);
}

float AcustraEngine::StringLoop::readDelay(float samples) noexcept
{
    const auto sampleBits = exact::bits(samples);
    if (!readDelayValid || sampleBits != readDelayBits)
    {
        const float bounded = AcustraEngine::clamp(
            samples, 3.0f, static_cast<float>(maximumDelaySamples - 3));
        readDelayWhole = delayAnchor(bounded);
        const float fraction = bounded - static_cast<float>(readDelayWhole);
        if (exact::bits(fraction) != exact::bits(thiranFraction))
        {
            double a1 = 0.0;
            double a2 = 0.0;
            thiranCoefficients(static_cast<double>(fraction), a1, a2);
            thiranFraction = fraction;
            thiranFirst = static_cast<float>(a1);
            thiranSecond = static_cast<float>(a2);
        }
        readDelayBits = sampleBits;
        readDelayValid = true;
    }
    const int whole = readDelayWhole;
    const auto at = [&] (int samplesAgo)
    {
        return delay[static_cast<std::size_t>(
            wrapDelayIndex(writeIndex - samplesAgo))];
    };

    // Second-order Thiran allpass, exactly lossless at every frequency (see
    // delayAnchor). Direct form I: the only state is the two previous
    // outputs, and the previous inputs are read from the line at whichever
    // tap is current, so when a slewing delay crosses a band edge and moves
    // the tap and the coefficients together, the filter's memory of its input
    // is already the memory it would have had at the new tap - the transient
    // elimination of Valimaki, Laakso and Mackenzie, "Elimination of
    // transients in time-varying allpass fractional delay filters with
    // application to digital waveguide modeling", ICMC 1995, 327-334, which
    // for a line-read section is exactly this signal-valued state.
    const float first = thiranFirst;
    const float second = thiranSecond;
    const float output = second * at(whole) + first * at(whole + 1)
                       + at(whole + 2)
                       - first * allpassY1 - second * allpassY2;
    allpassY2 = allpassY1;
    allpassY1 = output;
    return output;
}

float AcustraEngine::StringLoop::displacementAt(float fraction) const noexcept
{
    const auto tap = [&] (float samplesAgo)
    {
        const int whole = static_cast<int>(samplesAgo);
        const float part = samplesAgo - static_cast<float>(whole);
        const float a = delay[static_cast<std::size_t>(
            wrapDelayIndex(writeIndex - 1 - whole))];
        const float b = delay[static_cast<std::size_t>(
            wrapDelayIndex(writeIndex - 2 - whole))];
        return a + part * (b - a);
    };
    // A full-round-trip line folds the nut's inversion into its stored wave.
    // Unfold the outgoing and returning waves at x: y(x) = s(D-x/c)-s(x/c),
    // with D=2L/c. This has sin(n*pi*x/L) nodes, unlike a bridge-force tap.
    // Remaggi et al., DAFx-12, Sec. 3.1, Eq. (1): the pickup observes two
    // oppositely signed travelling waves separated by twice the travel time.
    // https://dafx.de/paper-archive/2012/papers/dafx12_submission_62.pdf
    // Read after write(): zero delay is the newest sample at writeIndex-1.
    // Linear interpolation affects observation only, never loop losses.
    const float travel = 0.5f * currentDelay * fraction;
    return tap(currentDelay - travel) - tap(travel);
}

// The second dispersion section switches in at a vanishing share of its
// strength (calibrateDispersion), where it is a two-sample delay, and the
// tuned delay gives those two samples up to it; out again at the same
// point. It runs directly after the line read, so the handover is exact
// there: switching in, the read moves two samples nearer and the section
// is given the two reads it would have made, so it passes on the very
// samples the old read would have produced, and the Thiran read's own two
// outputs are advanced to the new tap by running it the two samples ahead
// the line already holds; switching out is the same backwards. The loop
// then sounds on without a step, where the section switched in from rest
// would drop its output for two samples and the delay's slew across the
// two samples would bend the pitch. A switch anywhere else (a fret change
// under a sounding wave) is one of many steps that change already makes.
void AcustraEngine::StringLoop::switchSecondDispersion(bool active) noexcept
{
    if (active == secondDispersionActive)
        return;
    secondDispersionActive = active;
    const float bounded = AcustraEngine::clamp(
        currentDelay, 3.0f, static_cast<float>(maximumDelaySamples - 3));
    const int whole = delayAnchor(bounded);
    if (active)
    {
        // The next two outputs of the read at its present tap: the line
        // holds their inputs whenever the tap is at least two samples back.
        const auto at = [&] (int samplesAgo)
        {
            return delay[static_cast<std::size_t>(
                wrapDelayIndex(writeIndex - samplesAgo))];
        };
        const float first = thiranFirst;
        const float second = thiranSecond;
        float next = allpassY1;
        float following = allpassY2;
        if (whole >= 2)
        {
            next = second * at(whole) + first * at(whole + 1) + at(whole + 2)
                 - first * allpassY1 - second * allpassY2;
            following = second * at(whole - 1) + first * at(whole)
                      + at(whole + 1) - first * next - second * allpassY1;
        }
        secondDispersion.x1 = following;
        secondDispersion.x2 = next;
        secondDispersion.y1 = allpassY1;
        secondDispersion.y2 = allpassY2;
        allpassY1 = following;
        allpassY2 = next;
        currentDelay -= 2.0f;
    }
    else
    {
        allpassY1 = secondDispersion.y1;
        allpassY2 = secondDispersion.y2;
        secondDispersion.reset();
        currentDelay += 2.0f;
    }
}

float AcustraEngine::StringLoop::advance(float delaySmoothing,
                                         float releaseGain) noexcept
{
    if (loopGainTransitionSamples > 0)
    {
        if (--loopGainTransitionSamples == 0)
            loopGain = targetLoopGain;
        else
            loopGain = loopGainStep > 0.0f
                ? std::min(targetLoopGain, loopGain + loopGainStep)
                : std::max(targetLoopGain, loopGain + loopGainStep);
    }
    if (intrinsicCoefficientSamples > 0)
    {
        const std::array<float*, 7> applied { &bendingLossGain, &bendingLossA1,
            &bendingLossA2, &dispersionA1, &dispersionA2,
            &secondDispersionA1, &secondDispersionA2 };
        --intrinsicCoefficientSamples;
        for (std::size_t index = 0; index < applied.size(); ++index)
        {
            const float target = intrinsicCoefficientTarget[index];
            // Evaluate from the target and fixed step instead of accumulating
            // seven independently rounded additions. In particular, the
            // bending numerator must keep following the denominator's DC sum.
            const float next = static_cast<float>(static_cast<double>(target)
                - static_cast<double>(intrinsicCoefficientStep[index])
                    * intrinsicCoefficientSamples);
            *applied[index] = intrinsicCoefficientSamples == 0 ? target
                : intrinsicCoefficientStep[index] > 0.0f
                    ? std::min(target, std::max(*applied[index], next))
                    : std::max(target, std::min(*applied[index], next));
        }
    }
    currentDelay += delaySmoothing * (targetDelay - currentDelay);
    float delayed = readDelay(currentDelay);
    if (secondDispersionActive)
        delayed = secondDispersion.process(delayed, secondDispersionA1,
                                           secondDispersionA2);
    const float broad = broadLossFilter.process(
        delayed, broadLossCoefficient);
    float reflected = delayed + broadLossMix * (broad - delayed);
    const float low = lossFilter.process(reflected, lowpassCoefficient);
    reflected += highLossMix * (low - reflected);
    if (bendingLossActive)
    {
        if (bendingLossSeed)
        {
            // The section's unit-DC rest state for the wave it meets.
            bendingLossY1 = bendingLossY2 = reflected;
            bendingLossSeed = false;
        }
        const float bent = bendingLossGain * reflected
            - bendingLossA1 * bendingLossY1 - bendingLossA2 * bendingLossY2;
        bendingLossY2 = bendingLossY1;
        bendingLossY1 = bent;
        reflected = bent;
    }
    reflected = dispersion.process(reflected, dispersionA1, dispersionA2);
    if (releaseGain != requestedReleaseGain)
    {
        requestedReleaseGain = releaseGain;
        releaseGainStep = exact::abs(releaseGain - appliedReleaseGain)
                        / std::max(currentDelay, 1.0f);
    }
    if (appliedReleaseGain < requestedReleaseGain)
        appliedReleaseGain = std::min(requestedReleaseGain,
                                      appliedReleaseGain + releaseGainStep);
    else if (appliedReleaseGain > requestedReleaseGain)
        appliedReleaseGain = std::max(requestedReleaseGain,
                                      appliedReleaseGain - releaseGainStep);
    return reflected * loopGain * appliedReleaseGain;
}

void AcustraEngine::StringLoop::write(float value) noexcept
{
    if (!exact::isfinite(value) || exact::abs(value) < 1.0e-30f)
        value = 0.0f;
    delay[static_cast<std::size_t>(writeIndex)] = value;
    writeIndex = wrapDelayIndex(writeIndex + 1);
}

void AcustraEngine::StringLoop::GestureContact::configure(
    float strength, int relaxationSamples) noexcept
{
    const double r = AcustraEngine::clamp(strength, 0.0f, 0.22f);
    const double c = -std::exp(-1.0 / std::max(1, relaxationSamples));
    coupling = static_cast<float>(std::sqrt(r * (1.0 - c * c)));
    memoryGain = static_cast<float>(-c);
    offsetScale = static_cast<float>(0.5 * std::sqrt(r * (1.0 - c) / (1.0 + c)));
    withdrawalSamples = std::max(1, relaxationSamples);
    withdrawalInverseSamples = 1.0f / static_cast<float>(withdrawalSamples);
    withdrawalScale = 1.0f;
    active = r > 0.0;
}

void AcustraEngine::StringLoop::GestureContact::withdraw(int remainingSamples) noexcept
{
    // The bridge can keep driving a released string, so its contact storage
    // need not be zero at the fixed return-to-open deadline. Lift the hand
    // during its final relaxation interval, reaching zero offset on the last
    // sample before that deadline. Smoothstep has zero endpoint velocity.
    const float x = AcustraEngine::clamp(
        static_cast<float>(remainingSamples - 1) * withdrawalInverseSamples,
        0.0f, 1.0f);
    withdrawalScale = x * x * (3.0f - 2.0f * x);
}

void AcustraEngine::StringLoop::GestureContact::scatter(
    float& first, float& second) noexcept
{
    if (!seeded)
    {
        previousIncoming = { first, second };
        seeded = true;
        return;
    }
    const float incomingFirst = first - previousIncoming[0];
    const float incomingSecond = second - previousIncoming[1];
    // The nut inversion is folded into this line: the physical waves at
    // the contact have opposite signs. A resistive equal-impedance shunt
    // consequently acts on their difference. A relaxing contact's scattering
    // matrix on that velocity difference and its internal state is
    // [[1-r+r*c, sqrt(r*(1-c*c))], [sqrt(r*(1-c*c)), -c]].
    // Its eigenvalues are 1 and -r-(1-r)*c, both bounded by one. Thus wave
    // velocity power plus stored contact energy cannot increase. Unlike a
    // fixed viscous clamp, this has unity DC transfer: its displacement
    // offset relaxes to zero rather than storing a late release pluck.
    // Scaling both coefficients by f gives the same instantaneous passive
    // matrix with strength r*f*f. Moving the hand also contributes the
    // bounded velocity term -delta(K*f)*oldMemory to the first wave (and its
    // opposite to the second). That physical withdrawal work is not covered
    // by the fixed-contact passivity proof above.
    memory = (coupling * withdrawalScale) * (incomingFirst - incomingSecond)
        + memoryGain * memory;
    previousIncoming = { first, second };
    const float offset = (offsetScale * withdrawalScale) * memory;
    // This is the integral of the matrix's outgoing velocity, expressed
    // without a accumulating displacement integrator's rounding drift.
    first -= offset;
    second += offset;
}

void AcustraEngine::StringLoop::beginGestureContact(
    float position, float strength, int relaxationSamples) noexcept
{
    gestureContact = {};
    const int length = std::clamp(static_cast<int>(std::lround(currentDelay)),
                                  8, maximumDelaySamples - 2);
    const int first = std::clamp(static_cast<int>(std::lround(
        0.5f * length * AcustraEngine::clamp(position, 0.02f, 0.98f))),
        1, length / 2 - 1);
    gestureContact.firstAge = first;
    gestureContact.secondAge = length - first;
    gestureContact.configure(strength, relaxationSamples);
}

void AcustraEngine::StringLoop::applyGestureContact() noexcept
{
    auto& first = delay[static_cast<std::size_t>(wrapDelayIndex(
        writeIndex - 1 - gestureContact.firstAge))];
    auto& second = delay[static_cast<std::size_t>(wrapDelayIndex(
        writeIndex - 1 - gestureContact.secondAge))];
    gestureContact.scatter(first, second);
}

double AcustraEngine::BridgeMode::processPast(double input) noexcept
{
    const double output = numerator1 * input + numerator2 * input1
                        - denominator1 * output1
                        - denominator2 * output2;
    input1 = input;
    output2 = output1;
    output1 = output;
    return output;
}

void AcustraEngine::BridgeLoad::reset() noexcept
{
    pastHeave = 0.0f;
    pastRock = 0.0f;
    tailIntegratedForce = 0.0f;
    tailIntegratedMoment = 0.0f;
    previousDisplacement = 0.0f;
    previousRotation = 0.0f;
    displacement = 0.0f;
    rotation = 0.0f;
    mainIntegratedForce = 0.0f;
    mainIntegratedMoment = 0.0f;
    bodyIntegratedForce = 0.0f;
    bodyIntegratedMoment = 0.0f;
    for (auto& mode : heaveModes)
        mode.reset();
    for (auto& mode : rockModes)
        mode.reset();
}

void AcustraEngine::BridgeLoad::advanceModes(float bodyForce,
                                             float bodyMoment) noexcept
{
    double nextPastHeave = 0.0;
    double nextPastRock = 0.0;
    for (int active = 0; active < activeModeCount; ++active)
    {
        const std::size_t index = activeModes[static_cast<std::size_t>(active)];
        double heaveState, rockState;
        // Pack this mode's heave and rock with their own coefficients and history.
        // Preserve each lane's arithmetic and the serial residue sums below.
#if defined(__SSE2__) && defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
        if (rocking[index])
        {
            typedef double Vector __attribute__((vector_size(16)));
            auto& h = heaveModes[index];
            auto& r = rockModes[index];
            const Vector input = { bodyForce, bodyMoment };
            const Vector n1 = { h.numerator1, r.numerator1 };
            const Vector n2 = { h.numerator2, r.numerator2 };
            const Vector d1 = { h.denominator1, r.denominator1 };
            const Vector d2 = { h.denominator2, r.denominator2 };
            const Vector i1 = { h.input1, r.input1 };
            const Vector o1 = { h.output1, r.output1 };
            const Vector o2 = { h.output2, r.output2 };
            const Vector output = n1 * input + n2 * i1 - d1 * o1 - d2 * o2;
            h.input1 = bodyForce;
            r.input1 = bodyMoment;
            h.output2 = h.output1;
            r.output2 = r.output1;
            h.output1 = output[0];
            r.output1 = output[1];
            heaveState = output[0];
            rockState = output[1];
        }
        else
        {
            heaveState = heaveModes[index].processPast(bodyForce);
            rockState = 0.0;
        }
#else
        heaveState = heaveModes[index].processPast(bodyForce);
        rockState = rocking[index] ? rockModes[index].processPast(bodyMoment) : 0.0;
#endif
        nextPastHeave += residueHeave[index] * heaveState;
        if (!rocking[index])
            continue;
        nextPastHeave += residueCross[index] * rockState;
        nextPastRock += residueCross[index] * heaveState
                      + residueRock[index] * rockState;
    }
    pastHeave = exact::isfinite(nextPastHeave)
        ? static_cast<float>(nextPastHeave) : 0.0f;
    pastRock = exact::isfinite(nextPastRock)
        ? static_cast<float>(nextPastRock) : 0.0f;
}

void AcustraEngine::BridgeLoad::process(const BridgeDrive& drive,
                                        float samplePeriod,
                                        BridgeLoad& fading,
                                        float weight) noexcept
{
    // One junction with the two mode sets' mobilities mixed: modal
    // mobilities add as positive-real sums, so each share is passive. The
    // anchor stubs and the junction's own state are this load's.
    const float keep = 1.0f - weight;
    const float heave = immediateHeave, cross = immediateCross,
                rock = immediateRock, pastH = pastHeave, pastR = pastRock;
    immediateHeave = weight * heave + keep * fading.immediateHeave;
    immediateCross = weight * cross + keep * fading.immediateCross;
    immediateRock = weight * rock + keep * fading.immediateRock;
    pastHeave = weight * pastH + keep * fading.pastHeave;
    pastRock = weight * pastR + keep * fading.pastRock;
    process(drive, samplePeriod);
    // process advanced this load's modes on the body force; advance the
    // fading ones on the same, and restore this load's own immediates.
    immediateHeave = heave;
    immediateCross = cross;
    immediateRock = rock;
    fading.advanceModes(bodyIntegratedForce, bodyIntegratedMoment);
}

void AcustraEngine::BridgeLoad::process(const BridgeDrive& drive,
                                        float samplePeriod) noexcept
{
    // The saddle is approximated by heave and normalized rock. The archive's
    // accelerometers sit behind the saddle rather than at the hammer points;
    // treating the two-end responses as collocated is a spatial assumption
    // (Method.pdf 2b, Fig. 3; GenerateMeasuredBridge.py). Here theta is the
    // legacy name for linear r=a*physical_angle, not an angle in radians.
    // A string at lever arm u ends on x_u = x + u*theta and
    // pushes F_u = Z(2a_u - x_u) there, so the strings contribute the force
    // sum and its first moment, and the same for the anchor stubs, each of
    // which sits at its own string's u. Solving
    //     [x; theta] = Y (b - G [x; theta]),  G = string + anchor moments,
    // is one 2x2 per sample and stays algebraic-loop-free because Y here is
    // only the immediate part of the modal bank.
    //
    // DAFx-26 attaches the measured body a short distance from the string's
    // end, leaving a fixed-end tail. Below its first resonance that segment
    // is the passive spring K=T/L_t; trapezoidal integration gives its
    // current-step impedance K*dt/2. Its three moments are the anchor's
    // stiffness matrix in the same two coordinates.
    const float half = 0.5f * samplePeriod;
    const float c0 = half * drive.stiffness0;
    const float c1 = half * drive.stiffness1;
    const float c2 = half * drive.stiffness2;
    const float historyForce = tailIntegratedForce
        + c0 * previousDisplacement + c1 * previousRotation;
    const float historyMoment = tailIntegratedMoment
        + c1 * previousDisplacement + c2 * previousRotation;

    const float g00 = drive.impedance0 + c0;
    const float g01 = drive.impedance1 + c1;
    const float g11 = drive.impedance2 + c2;
    const float b0 = drive.incidentHeave - historyForce;
    const float b1 = drive.incidentRock - historyMoment;

    // (I + Y G) [x; theta] = Y b + past
    const float m00 = 1.0f + immediateHeave * g00 + immediateCross * g01;
    const float m01 = immediateHeave * g01 + immediateCross * g11;
    const float m10 = immediateCross * g00 + immediateRock * g01;
    const float m11 = 1.0f + immediateCross * g01 + immediateRock * g11;
    const float r0 = immediateHeave * b0 + immediateCross * b1 + pastHeave;
    const float r1 = immediateCross * b0 + immediateRock * b1 + pastRock;
    const float determinant = m00 * m11 - m01 * m10;
    if (!(exact::abs(determinant) > 1.0e-12f) || !exact::isfinite(b0)
        || !exact::isfinite(b1))
    {
        reset();
        return;
    }

    const float nextDisplacement = (r0 * m11 - r1 * m01) / determinant;
    const float nextRotation = (r1 * m00 - r0 * m10) / determinant;
    if (!exact::isfinite(nextDisplacement) || !exact::isfinite(nextRotation))
    {
        reset();
        return;
    }
    displacement = nextDisplacement;
    rotation = nextRotation;

    const float nextTailForce = historyForce
        + c0 * displacement + c1 * rotation;
    const float nextTailMoment = historyMoment
        + c1 * displacement + c2 * rotation;
    // The string force less what the anchor takes, in both coordinates.
    const float bodyForce = b0 - g00 * displacement - g01 * rotation;
    const float bodyMoment = b1 - g01 * displacement - g11 * rotation;

    advanceModes(bodyForce, bodyMoment);
    previousDisplacement = displacement;
    previousRotation = rotation;
    tailIntegratedForce = nextTailForce;
    tailIntegratedMoment = nextTailMoment;
    mainIntegratedForce = drive.incidentHeave
        - drive.impedance0 * displacement - drive.impedance1 * rotation;
    mainIntegratedMoment = drive.incidentRock
        - drive.impedance1 * displacement - drive.impedance2 * rotation;
    bodyIntegratedForce = bodyForce;
    bodyIntegratedMoment = bodyMoment;
}

void AcustraEngine::prepare(double sampleRate, int)
{
    // Only a rate that is no rate at all falls back to 48 kHz; a finite one
    // outside the modelled range is held at its nearer bound, so pitch moves
    // continuously across it instead of jumping at 8 kHz.
    if (!exact::isfinite(sampleRate) || sampleRate <= 0.0)
        sampleRate = 48000.0;
    sampleRate_ = std::clamp(sampleRate, 8000.0, 384000.0);
    ++voiceConfigurationGeneration_;
    inverseSampleRate_ = static_cast<float>(1.0 / sampleRate_);
    releaseStepPole_ = static_cast<float>(std::exp(-1.0 / (0.010 * sampleRate_)));
    releaseStepSamples_ = static_cast<int>(std::ceil(0.4 * sampleRate_));
    // The piezo chain (renderPiezo; PiezoDesign has the parts and their
    // sources, Tools/PiezoReference.py simulates the same circuit).
    {
        using D = PiezoDesign;
        double impedance = 0.0;
        for (int string = 0; string < stringCount; ++string)
            impedance += static_cast<double>(stringImpedance(
                string, standardOpenMidi[static_cast<std::size_t>(string)]));
        piezoSaddle_ = designPiezoSaddle(sampleRate_, impedance);
        // The input section as deviations from its operating point. The jack
        // node carries only capacitors, so its charge stays where it was and
        // V_J = (Cp V_oc + C1 V_IN) / (Cs + C1); the IN node then sees C1 in
        // series with the jack's shunt, beside its own capacitance to ground.
        // With w = V_IN - kin V_oc, V_B from its KCL and U1A's output at
        // V_IN + e (e is nonzero only while its input range stops it):
        //   w'    = -(V_IN - V_B) / (R3 Ceq)
        //   V_C2' = (V_B - V_C2 - O1) / (R4 C2)
        const double shunt = D::elementCapacitance + D::cableCapacitance
            + D::strayCapacitance;
        const double atInput = D::inputCapacitance + D::strayCapacitance;
        const double total = shunt + D::c1;
        const double series = D::c1 * shunt + atInput * total;
        piezoInputShare_ = D::c1 * D::elementCapacitance / series;
        piezoJackElement_ = D::elementCapacitance / total;
        piezoJackInput_ = D::c1 / total;
        const double inputCapacitance = series / total;
        piezoDiodeScale_ = inputCapacitance * sampleRate_ / D::diodeSaturation;
        // The diodes' dump z over one sample at a drive x past U1A's range:
        // Ceq fs z / IS = e^((x - z) / N Vt) - 1, zero at zero, by bisection
        // (z lies in [0, x], the right side falling in z) and Newton; and
        // its slope dz/dx = (Kz + 1) / (N Vt K + Kz + 1), K = Ceq fs / IS.
        // Cubic Hermite between the points is within 5 nV of the solution.
        for (std::size_t point = 0; point < piezoDiodeDump_.size(); ++point)
        {
            const double drive = piezoDiodeStep * static_cast<double>(point);
            double low = 0.0, high = drive;
            for (int step = 0; step < 200 && high - low > 1.0e-15; ++step)
            {
                const double middle = 0.5 * (low + high);
                const double balance = piezoDiodeScale_ * middle
                    - std::expm1((drive - middle) / D::diodeThermalVoltage);
                (balance > 0.0 ? high : low) = middle;
            }
            const double dump = 0.5 * (low + high);
            piezoDiodeDump_[point] = dump;
            piezoDiodeSlope_[point] = (piezoDiodeScale_ * dump + 1.0)
                / (D::diodeThermalVoltage * piezoDiodeScale_ + piezoDiodeScale_ * dump + 1.0);
        }
        const double bias = D::r1 * D::r2 / (D::r1 + D::r2);
        const double node = 1.0 / D::r3 + 1.0 / D::r4 + 1.0 / bias;
        const double share = 1.0 / (bias * node);
        const double back = 1.0 / (D::r4 * node);
        const double inputRate = 1.0 / (D::r3 * inputCapacitance);
        const double bootstrapRate = 1.0 / (D::r4 * D::c2);
        const double a00 = -share * inputRate, a01 = back * inputRate;
        const double a10 = -share * bootstrapRate, a11 = (back - 1.0) * bootstrapRate;
        const double bv0 = a00 * piezoInputShare_, bv1 = a10 * piezoInputShare_;
        const double be0 = a01, be1 = a11;
        // Trapezoidal: x[n] = (I - hA)^-1 ((I + hA) x[n-1] + h B (u[n] + u[n-1])).
        const double h = 0.5 / sampleRate_;
        const double m00 = 1.0 - h * a11, m01 = h * a01;
        const double m10 = h * a10, m11 = 1.0 - h * a00;
        const double determinant = m11 * m00 - m01 * m10;
        const auto solve = [&] (double v0, double v1, double& out0, double& out1)
        {
            out0 = (m00 * v0 + m01 * v1) / determinant;
            out1 = (m10 * v0 + m11 * v1) / determinant;
        };
        solve(1.0 + h * a00, h * a10, piezoFrontA00_, piezoFrontA10_);
        solve(h * a01, 1.0 + h * a11, piezoFrontA01_, piezoFrontA11_);
        solve(h * bv0, h * bv1, piezoFrontB0_, piezoFrontB1_);
        solve(h * be0, h * be1, piezoFrontE0_, piezoFrontE1_);
        // C3 into R5 || R6; C4 always sees U1B's output through R7 + R8
        // (R8 alone while U1B follows its input, since then N = Y and
        // O2 - V_C4 = (1 + R7/R8)(Y - V_C4)); C5 into R9 and the pot || DI.
        const auto onePole = [h] (double timeConstant, double& pole, double& gain)
        {
            const double k = h / timeConstant;
            pole = (1.0 - k) / (1.0 + k);
            gain = k / (1.0 + k);
        };
        const double load = D::volume * D::diInput / (D::volume + D::diInput);
        onePole(D::r5 * D::r6 / (D::r5 + D::r6) * D::c3, piezoC3Pole_, piezoC3Gain_);
        onePole((D::r7 + D::r8) * D::c4, piezoC4Pole_, piezoC4Gain_);
        onePole((D::r9 + load) * D::c5, piezoC5Pole_, piezoC5Gain_);
        // The mid-band gain from the element's open-circuit voltage to the
        // DI, which the level match divides out with the element's
        // sensitivity: an engine force unit through the flat band leaves at
        // the trim alone, as the microphones' reference does.
        piezoMidbandGain_ = piezoInputShare_ * (1.0 + D::r7 / D::r8) * load / (D::r9 + load);
        configurePiezoUnit();
    }
    piezoStringWeights_ = PiezoDesign::stringWeights;
    delaySmoothing_ = 1.0f - std::exp(-1.0f
        / (0.006f * static_cast<float>(sampleRate_)));
    parameterSmoothing_ = 1.0f - std::exp(-1.0f
        / (0.020f * static_cast<float>(sampleRate_)));
    // The bridge hand is reconfigured at control rate, so its follower runs
    // there too; 15 ms is a hand arriving, not a step.
    palmMuteSmoothing_ = 1.0f - std::exp(-static_cast<float>(controlPeriod)
        / (0.015f * static_cast<float>(sampleRate_)));
    // Preserve the former 48 kHz pole as an 8.323 ms physical-time follower.
    levelSmoothing_ = -std::expm1(std::log1p(-0.0025f)
        * 48000.0f / static_cast<float>(sampleRate_));
    bodyModelFadeStep_ = 1.0f
        / (0.040f * static_cast<float>(sampleRate_));
    bridgeLoadFadeStep_ = 1.0f
        / (0.020f * static_cast<float>(sampleRate_));
    room_.prepare(sampleRate_);
    prepared_ = true;
    restartRandomDraws();
    reset();
}

void AcustraEngine::reset() noexcept
{
    resetSoundState();
    palmMute_ = targetPalmMute_;
    pitchBendSemitones_.fill(0.0f);
    mpeTimbre_.fill(-1.0f);
    mpePressure_.fill(-1.0f);
    sustainPedals_.fill(false);
    vibrato_ = 0.0f;
    vibratoPhase_ = 0.0f;
    vibratoOnset_ = 0.0f;
    noteOrder_ = 0;
    sampleClock_ = 0;
    pickingGesture_ = {};
    strumGesture_ = {};
    pickingGestureSeen_ = false;
    pickingGestureSample_ = 0;
    hand_.fill({});
    lastNoteOnSample_.fill(0);
    chordStartSample_.fill(0);
    noteOnSeen_.fill(false);
    plannedCount_ = 0;
    controlCounter_ = 0;
    parameters_ = sanitise(targetParameters_);
    ++voiceConfigurationGeneration_;
    bodyAmount_ = parameters_.bodyAmount;
    width_ = parameters_.stereoWidth;
    outputGain_ = parameters_.outputGain;
    piezoMix_ = parameters_.piezoMix;
    roomAmount_ = parameters_.room;
    roomSendFor_ = -1.0f;
    outputReference_ = outputReferenceFor(parameters_);
    monoReference_ = monoReferenceFor(parameters_);
    piezoTrim_ = PiezoDesign::trim * piezoReferenceFor(parameters_);
    captureMix_.fill(0.0f);
    captureMix_[static_cast<std::size_t>(parameters_.capture)] = 1.0f;
    bodyConfigured_ = false;
    bodyUpdatePending_ = false;
    // Both banks follow the model, which is only known here: prepare runs
    // before the pending parameters are adopted.
    configureBridge();
    configuredBridgeModel_ = parameters_.guitarModel;
    bridgeUpdatePending_ = false;
    configureBody();
    bodyBank_.reset();
    fadingBodyBank_.reset();
    const auto notes = openNotes(parameters_.tuning);
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        voice.openMidi = notes[static_cast<std::size_t>(string)];
        voice.lastPluckMidiNote = -1;
        voice.lastPluckMidiChannel = 0;
        voice.lastPluckVelocity = 0.0f;
        voice.lastPluckSample = 0;
        voice.repeatedPluckGain = 1.0f;
        voice.repluckForceGain = 1.0f;
        voice.ownerCount = 0;
        voice.played = false;
        voice.keyDown = false;
        voice.pedalHeld = false;
        voice.level = 0.0f;
        voice.returnSamples = 0;
        voice.bridgeTailStiffnessSamples = 0;
        returnToOpenString(voice, string, true);
    }
    // Initialise every reciprocal open-string loop. The second pass settles
    // tuning against all six configured ports and preserves the normal
    // post-pluck update order.
    for (int pass = 0; pass < 2; ++pass)
        for (int string = 0; string < stringCount; ++string)
            configureVoice(voices_[static_cast<std::size_t>(string)], string,
                           voices_[static_cast<std::size_t>(string)].midiNote,
                           false);
}

void AcustraEngine::resetSoundState() noexcept
{
    idleQuietSamples_ = 0;
    idleFlushed_ = true;
    resetPiezo();
    micDelayLeft_.fill(0.0f);
    micDelayRight_.fill(0.0f);
    micDelayMono_.fill(0.0f);
    micDelayIndex_ = 0;
    room_.reset();
    for (auto& voice : voices_)
    {
        voice.releaseStepAge = -1;
        voice.releaseStepRise = voice.releaseStepForce
            = voice.releaseStepLevel = 0.0f;
    }
    piezoForceDerivative_.reset();
    lastPiezoWave_ = lastPiezoForce_ = 0.0f;
    lastPiezoImpedanceSum_ = lastPiezoImpedanceMoment_ = 0.0f;
    bridgeLoad_.reset();
    for (auto& derivative : bridgePowerDerivatives_)
        derivative.reset();
    bridgeVelocityDerivative_.reset();
    bridgeRotationDerivative_.reset();
    bridgeForceDerivative_.reset();
    bridgeForceMomentDerivative_.reset();
    bridgeBodyForceDerivative_.reset();
    bridgeBodyMomentDerivative_.reset();
    bridgeTailForceDerivative_.reset();
    bridgeTailMomentDerivative_.reset();
    lastBridgeVelocity_ = 0.0f;
    lastBridgeReactionForce_ = 0.0f;
    lastBridgeBodyForce_ = 0.0f;
    lastBridgeTailForce_ = 0.0f;
    lastLongitudinalForce_ = 0.0f;
    lastBridgePower_ = 0.0f;
    lastBridgeBodyPower_ = 0.0f;
    lastBridgeTailPower_ = 0.0f;
    bridgeDerivativesNeedPriming_ = true;
    bridgeDerivativesCrossRelease_ = false;
    bridgeDerivativesCrossConfigure_ = false;
    bridgeLoadFade_ = 1.0f;
    // A cleared bridge has no fade left to wait for.
    if (bridgeUpdatePending_)
        applyPendingBridge(false);
    lastImpedanceSum_ = 0.0f;
    lastImpedanceMoment_ = 0.0f;
    lastImpedanceInertia_ = 0.0f;
    bodyBank_.reset();
    fadingBodyBank_.reset();
}

void AcustraEngine::setParameters(const EngineParameters& parameters) noexcept
{
    targetParameters_ = sanitise(parameters);
    if (prepared_)
        applyDiscreteParameters(false);
}

void AcustraEngine::setTempoBpm(double bpm) noexcept
{
    const double next = exact::isfinite(bpm) && bpm > 0.0 ? bpm : 120.0;
    if (next == tempoBpm_)
        return;
    // No accumulated floating-point beat clock: each pending note is
    // anchored to the exact sample clock, and only a tempo change converts
    // its elapsed samples to beats. An overdue release cannot be revived.
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        processPendingRelease(voice, string);
        if (!voice.releaseJoinPending)
            continue;
        const auto elapsed = sampleClock_ - voice.releaseJoinAnchorSample;
        const long double consumed = static_cast<long double>(elapsed)
            * (static_cast<long double>(tempoBpm_) / 60.0L) / sampleRate_;
        voice.releaseJoinRemainingBeats = static_cast<double>(std::max(
            0.0L, static_cast<long double>(voice.releaseJoinRemainingBeats) - consumed));
        voice.releaseJoinAnchorSample = sampleClock_;
    }
    tempoBpm_ = next;
    for (auto& voice : voices_)
        if (voice.releaseJoinPending)
            updateReleaseJoinWindow(voice);
}

void AcustraEngine::setPhysicalCalibration(
    const PhysicalCalibration& calibration) noexcept
{
    physicalCalibration_ = sanitise(calibration);
    // The piezo's force unit follows the strings' displacement unit.
    configurePiezoUnit();
    // Steel's own bridge follows the body's frequency and Q calibration,
    // which the mobility table's key does not hold.
    bridgeMobilityTable_.valid = false;
    ++voiceConfigurationGeneration_;
    if (!prepared_)
        return;
    reset();
}

void AcustraEngine::setPerformanceRealism(
    const PerformanceRealism& options) noexcept
{
    performanceRealism_ = options;
    bridgeMobilityTable_.valid = false;
    ++voiceConfigurationGeneration_;
    restartRandomDraws();
    if (prepared_)
        reset();
}

void AcustraEngine::applyDiscreteParameters(bool force) noexcept
{
    const auto next = sanitise(targetParameters_);
    const bool constructionChanged = force
        || !sameStringConstruction(next, parameters_);
    const bool modelChanged = force || next.guitarModel != parameters_.guitarModel;
    // The bridge belongs to its radiation's body, so Wood moves the bridge's
    // poles with the radiation's and exchanges the mechanical load as Shape
    // does.
    const bool shapeChanged = next.shape != parameters_.shape
        || next.bodyMaterial != parameters_.bodyMaterial;
    const bool bodyChanged = modelChanged || shapeChanged;
    const bool ageChanged = force
        || exact::abs(next.stringAge - parameters_.stringAge) > 1.0e-5f;
    const bool bridgeChanged = modelChanged || shapeChanged;
    const bool tuningChanged = force || next.tuning != parameters_.tuning;
    if (force || next.tuning != parameters_.tuning
        || next.guitarModel != parameters_.guitarModel
        || shapeChanged
        || exact::bits(next.stringAge) != exact::bits(parameters_.stringAge))
        ++voiceConfigurationGeneration_;
    // Shape and Wood retune the same measured bank; Model exchanges it for
    // another guitar's.
    const bool sameBridgeBank = !force && !modelChanged;
    // What each string presented to the junction, for a retune under a
    // ringing chord (below).
    std::array<float, stringCount> previousImpedance {};
    for (int string = 0; string < stringCount; ++string)
        previousImpedance[static_cast<std::size_t>(string)]
            = voices_[static_cast<std::size_t>(string)].characteristicImpedance;
    parameters_ = next;

    // The model selects which measured guitar the bridge and body banks come
    // from, so it reconfigures both. configureBody crossfades its radiation
    // over 40 ms; a live bridge rebuild crossfades its mobility from the
    // modes that were sounding over 20 ms (bridgeLoadFade_), and the same
    // bank retuned keeps its modes ringing. Rebuilding the bridge at once
    // used to zero every mode under a ringing chord, a tick 20-30 dB over
    // either steady sound above 5 kHz (audit F14).
    if (bodyChanged)
        configureBody();
    if (bridgeChanged)
    {
        const bool matchesTarget = configuredBridgeModel_ == parameters_.guitarModel
            && configuredBridgeShape_ == parameters_.shape
            && configuredBridgeMaterial_ == parameters_.bodyMaterial;
        if (!force && matchesTarget)
            bridgeUpdatePending_ = false;
        else if (!force && bridgeLoadFade_ > 0.0f && bridgeLoadFade_ < 1.0f)
            bridgeUpdatePending_ = true;
        else
        {
            bool keepModalState = sameBridgeBank;
            bool restoresSoundingBridge = false;
            if (!force)
            {
                if (bridgeLoadFade_ >= 1.0f)
                {
                    fadingBridgeLoad_ = bridgeLoad_;
                    fadingBridgeModel_ = configuredBridgeModel_;
                    fadingBridgeShape_ = configuredBridgeShape_;
                    fadingBridgeMaterial_ = configuredBridgeMaterial_;
                }
                else if (fadingBridgeModel_ == parameters_.guitarModel)
                {
                    // No sample has heard the target yet. Returning to the
                    // sounding guitar takes its own modal state, rather than
                    // the silent replacement's state or a second queued fade.
                    bridgeLoad_ = fadingBridgeLoad_;
                    keepModalState = true;
                    restoresSoundingBridge = fadingBridgeShape_ == parameters_.shape
                        && fadingBridgeMaterial_ == parameters_.bodyMaterial;
                }
                bridgeLoadFade_ = restoresSoundingBridge ? 1.0f : 0.0f;
            }
            configureBridge(keepModalState);
            bridgeUpdatePending_ = false;
        }
    }

    const auto notes = openNotes(parameters_.tuning);
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        const int newOpen = notes[static_cast<std::size_t>(string)];
        const bool openChanged = voice.openMidi != newOpen;
        voice.openMidi = newOpen;
        if (force)
        {
            voice.attackPitchCents = 0.0f;
            voice.attackPitchDecay = 1.0f;
            voice.attackSlopeEnergy = 0.0f;
            voice.observedSlopeEnergy = 0.0f;
        }
        if (!voice.played && tuningChanged && (force || openChanged))
            // An idle string is still physically vibrating through the shared
            // bridge. A tuning change moves its pitch and port, preserving
            // those waves; only reset/prepare clears them. Strings whose open
            // pitch stays unchanged retain their full sympathetic state.
            returnToOpenString(voice, string, force);
        else if (constructionChanged || ageChanged || shapeChanged)
            configureVoice(voice, string, voice.midiNote, false, false,
                           tuningChanged && !force);
    }
    // Switching the tuning under a ringing chord changes every string's
    // impedance at once, so the junction's wave variables step with the
    // port. That is the strings being retuned, not the bridge moving, and
    // differencing it made a click 26 times the chord it landed on. A bridge
    // rebuild can step them too. The derivatives carry the
    // bridge's own motion across that sample (processAcrossStep); reading it
    // as no motion at all, as a released shape is read, left a one-sample
    // hole in every bridge force.
    if (bridgeChanged || tuningChanged)
        bridgeDerivativesCrossConfigure_ = true;

    // A tail belongs to the string construction it was taken from, and its
    // loop is not redesigned below. Shape changes the body attached to that
    // string, not its ownership or construction, so it keeps the tail and its
    // still-connected junction port. Wood moves the radiation and the bridge
    // like Shape (above), and keeps the tail too. So does String Age: a host
    // automates it every block, and the tail already dies under the hand's
    // loss, which outweighs any change of the string's own; deleting it cut
    // the ringing re-plucked string off mid-wave, a click on every age step
    // over a re-struck note.
    if (constructionChanged || tuningChanged)
        for (auto& voice : voices_)
        {
            if (!voice.tailActive)
                continue;
            voice.tailActive = false;
            voice.tailRetiring = false;
            voice.tailLegatoContactTravel.active = false;
            voice.tailLegatoContactSamples = 0;
            voice.tailCharacteristicImpedance = 0.0f;
            voice.tailLevel = 0.0f;
            voice.tailQuietSamples = 0;
            voice.tailLoop.reset();
            voice.tailParallelLoop.reset();
        }

    if (constructionChanged || ageChanged || tuningChanged || shapeChanged)
        for (int pass = 0; pass < 2; ++pass)
            for (int string = 0; string < stringCount; ++string)
                configureVoice(voices_[static_cast<std::size_t>(string)], string,
                               voices_[static_cast<std::size_t>(string)].midiNote,
                               false, false, tuningChanged && !force);

    if (tuningChanged && !force)
        for (int string = 0; string < stringCount; ++string)
        {
            auto& voice = voices_[static_cast<std::size_t>(string)];
            const float before = previousImpedance[static_cast<std::size_t>(string)];
            const float after = voice.characteristicImpedance;
            if (!(before > 0.0f && after > 0.0f)
                || !exact::isfinite(before / after))
                continue;
            // A retune keeps its strings' waves as they are, and a retuned
            // string's port moves to its new impedance at the delay's own
            // rate, as a bend's does, instead of stepping under its ringing
            // wave.
            voice.appliedBendImpedanceScale *= before / after;
        }
}

void AcustraEngine::scaleStoredWaves(Voice& voice, float gain) noexcept
{
    for (auto& loop : voice.loops)
    {
        for (auto& sample : loop.delay)
            sample *= gain;
        loop.bendingLossY1 *= gain;
        loop.bendingLossY2 *= gain;
        loop.allpassY1 *= gain;
        loop.allpassY2 *= gain;
        for (auto* filter : { &loop.broadLossFilter, &loop.lossFilter })
        {
            filter->state *= gain;
            filter->previousInput *= gain;
        }
        for (auto* section : { &loop.dispersion, &loop.secondDispersion })
        {
            section->x1 *= gain;
            section->x2 *= gain;
            section->y1 *= gain;
            section->y2 *= gain;
        }
        // What the junction read last from this loop moves with it.
        for (auto& value : loop.bridgeDerivative.history)
            value *= gain;
    }
}

void AcustraEngine::updateControlState() noexcept
{
    palmMute_ += palmMuteSmoothing_ * (targetPalmMute_ - palmMute_);
    if (exact::abs(targetPalmMute_ - palmMute_) < 1.0e-6f)
        palmMute_ = targetPalmMute_;
    if (vibrato_ > 0.0f)
    {
        // Erkut's two measured rates as the wheel's endpoints, in the
        // direction he measured them: the fast vibrato is the shallow one.
        const float rate = 4.9f + vibrato_ * (1.4f - 4.9f);
        vibratoPhase_ += twoPi * rate
            * static_cast<float>(controlPeriod) * inverseSampleRate_;
        if (vibratoPhase_ >= twoPi)
            vibratoPhase_ -= twoPi;
        vibratoOnset_ = std::min(1.0f, vibratoOnset_
            + static_cast<float>(controlPeriod) * inverseSampleRate_ / 0.5f);
    }
    else
    {
        vibratoPhase_ = 0.0f;
        vibratoOnset_ = 0.0f;
    }
    applyDiscreteParameters(false);
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        updateAttackPitch(voice, string);
        configureVoice(voice, string, voice.midiNote, false);
        updateTailHandLoss(voice);
    }
}

std::array<float, 2> AcustraEngine::radiationModePole(
    const EngineParameters& parameters, const PhysicalCalibration& calibration,
    int index, bool playerBodyLoading) noexcept
{
    const auto bank = measuredBodyBank(parameters.guitarModel);
    if (index < 0 || static_cast<std::size_t>(index) >= bank.size())
        return { 0.0f, 0.0f };
    const AnchorTransform& anchor = anchorTransformFor(parameters.guitarModel);
    const auto morph = bodyShapeMorph(
        bank, anchor, anchorBodyFor(parameters.guitarModel),
        targetBodyFor(parameters.guitarModel, parameters.shape));
    const auto pole = radiationPole(bank, index, anchor, morph,
        woodFactorsFor(parameters.bodyMaterial, parameters.guitarModel),
        parameters.guitarModel != GuitarModel::Original, calibration);
    return { pole.frequency, detail::playerLoadedBodyQ(pole.frequency, pole.q,
        playerBodyLoading && parameters.guitarModel == GuitarModel::Bellido1978) };
}

std::array<float, 4> AcustraEngine::bodyWoodFactors(
    const EngineParameters& parameters) noexcept
{
    const auto wood = woodFactorsFor(parameters.bodyMaterial,
                                     parameters.guitarModel);
    return { wood.frequency, wood.q, wood.brightness, wood.radiation };
}

// The Original's close microphones as a recording hears them
// (CaptureVoicingData.h): the amplitude gain at one frequency, the product
// of each section's analog RBJ prototype read at that frequency, with the
// level that keeps the default construction's loudness.
float AcustraEngine::captureVoicingGain(float frequency) noexcept
{
    double power = 1.0;
    for (const auto& section : detail::captureVoicingSections)
    {
        const double a = std::pow(10.0, static_cast<double>(section.gainDb) / 40.0);
        const double w = static_cast<double>(frequency)
            / static_cast<double>(section.frequencyHz);
        const double q = static_cast<double>(section.q);
        const double w2 = w * w;
        double numerator = 1.0, denominator = 1.0;
        if (section.kind == detail::CaptureVoicingKind::Peak)
        {
            const double edge = (1.0 - w2) * (1.0 - w2);
            numerator = edge + (w * a / q) * (w * a / q);
            denominator = edge + (w / (a * q)) * (w / (a * q));
        }
        else
        {
            // Low shelf A (s^2 + sqrt(A)/Q s + A) / (A s^2 + sqrt(A)/Q s + 1);
            // the high shelf swaps the two quadratics' outer terms.
            const double slope = std::sqrt(a) * w / q;
            const double lowEdge = (a - w2) * (a - w2);
            const double highEdge = (1.0 - a * w2) * (1.0 - a * w2);
            const bool low = section.kind == detail::CaptureVoicingKind::LowShelf;
            numerator = a * a * ((low ? lowEdge : highEdge) + slope * slope);
            denominator = (low ? highEdge : lowEdge) + slope * slope;
        }
        power *= numerator / denominator;
    }
    return static_cast<float>(std::sqrt(power)
        * std::pow(10.0, static_cast<double>(detail::captureVoicingLevelDb) / 20.0));
}

void AcustraEngine::configureBody() noexcept
{
    if (bodyConfigured_
        && configuredGuitarModel_ == parameters_.guitarModel
        && configuredBodyShape_ == parameters_.shape
        && configuredBodyMaterial_ == parameters_.bodyMaterial)
    {
        bodyUpdatePending_ = false;
        return;
    }
    // Preserve both sounding banks until this 40 ms fade finishes. Repeated
    // host updates coalesce into the latest request, delayed by at most the
    // remainder of that fade. At zero mix only the silent target is replaced.
    if (bodyConfigured_ && bodyModelFade_ > 0.0f && bodyModelFade_ < 1.0f)
    {
        bodyUpdatePending_ = true;
        return;
    }
    bodyUpdatePending_ = false;
    // Shape and Wood retune the same measured bank, whose modes keep ringing
    // from where they were, as the bridge's do: restarted from rest under a
    // ringing chord they beat against it while they settle, 30-45 dB over
    // the steady sound above 5 kHz (audit F14). Model exchanges it for
    // another guitar's, whose modes start from rest.
    const bool restoresSoundingBank = bodyConfigured_ && bodyModelFade_ == 0.0f
        && fadingBodyModel_ == parameters_.guitarModel;
    const bool restoresSoundingBody = restoresSoundingBank
        && fadingBodyShape_ == parameters_.shape
        && fadingBodyMaterial_ == parameters_.bodyMaterial;
    const bool sameBodyBank = bodyConfigured_
        && (configuredGuitarModel_ == parameters_.guitarModel || restoresSoundingBank);
    if (restoresSoundingBank)
        bodyBank_ = fadingBodyBank_;
    if (bodyConfigured_)
    {
        if (bodyModelFade_ >= 1.0f)
        {
            fadingBodyBank_ = bodyBank_;
            fadingBodyModel_ = configuredGuitarModel_;
            fadingBodyShape_ = configuredBodyShape_;
            fadingBodyMaterial_ = configuredBodyMaterial_;
        }
        bodyModelFade_ = restoresSoundingBody ? 1.0f : 0.0f;
    }
    else
    {
        bodyModelFade_ = 1.0f;
    }

    const AnchorTransform& anchor = anchorTransformFor(parameters_.guitarModel);
    const auto bank = measuredBodyBank(parameters_.guitarModel);
    // A named guitar is unwarped at its own family/wood setting. Moving Shape
    // or Wood away from that point is explicitly a construction variation.
    const auto morph = bodyShapeMorph(
        bank, anchor, anchorBodyFor(parameters_.guitarModel),
        targetBodyFor(parameters_.guitarModel, parameters_.shape));
    const bool named = parameters_.guitarModel != GuitarModel::Original;
    const auto woodFactors = woodFactorsFor(parameters_.bodyMaterial,
                                            parameters_.guitarModel);
    // lowBodyModeGain raises the air mode where steel's g21 bank under-hears
    // it: at the treble-bridge microphone, 10 cm over the bridge, 82 Hz
    // radiates 15 dB under 330 Hz where the upper-bout one hears it 2 dB
    // over.
    const bool steelBank = !named;
    // Every bank that ships keeps its measured phase with no observation
    // delay, so renderBody plays the banks as they are.

    // One radiation mode into the next slot: `index` is its place in the bank
    // it was fitted in and `layerMorph` that bank's Shape morph; `share` is
    // its part's share of the steel blend (exactly 1 outside it).
    int slot = 0;
    const auto place = [&] (const detail::MeasuredBodyMode& measured, int index,
                            const BodyShapeMorph& layerMorph, float share)
    {
        auto& mode = bodyModes_[static_cast<std::size_t>(slot++)];
        const auto& morph = layerMorph;
        const bool lowBodyMode = measured.frequency > 85.0f
            && measured.frequency < 145.0f;
        // The A0 group, T1 and the plate modes above it each take their own
        // level from the coupled pair; the anchor shape's are exactly 1.
        float shapeLevel = morph.plateLevel;
        if (measured.frequency < lowBodyGroupUpperHz)
            shapeLevel = morph.a0Level;
        else if (index <= morph.t1Index)
            shapeLevel = morph.t1Level;
        const auto engine = radiationPole(measured, index, anchor, morph,
            woodFactors, named, physicalCalibration_);
        float frequency = engine.frequency;
        const float highestMode = 0.46f * static_cast<float>(sampleRate_);
        const bool audibleAtThisRate = frequency < highestMode;
        frequency = clamp(frequency, 45.0f, highestMode);

        const float upper = clamp(std::log2(std::max(frequency, 120.0f)
            / 120.0f) / 6.0f, 0.0f, 1.0f);
        const float q = detail::playerLoadedBodyQ(frequency, engine.q,
            named && performanceRealism_.playerBodyLoading);
        const float radius = std::exp(-pi * frequency
                                      / (q * static_cast<float>(sampleRate_)));
        const std::complex<float> pole = std::polar(
            radius, twoPi * frequency * inverseSampleRate_);
        mode.poleReal = pole.real();
        mode.poleImaginary = audibleAtThisRate ? pole.imag() : 0.0f;

        const float bassTilt = 1.0f + (anchor.bass - 1.0f)
            * std::exp(-frequency / 520.0f);
        const float brilliance = std::pow(woodFactors.brightness, upper);
        const float residueTilt = std::exp2(
            physicalCalibration_.residueTiltDbPerOctave
            * std::log2(frequency / 1000.0f) / 6.02059991f);
        // The Original's close pair heard from where a recording hears it
        // (CaptureVoicingData.h); the Bellido keeps its own microphones.
        const float voicing = steelBank ? captureVoicingGain(frequency) : 1.0f;
        const float drive = audibleAtThisRate
            ? detail::guitarMicrophoneTrims[static_cast<std::size_t>(parameters_.guitarModel)]
                * anchor.volume * shapeLevel
                * woodFactors.radiation
                * bassTilt * brilliance
                * residueTilt * voicing
            : 0.0f;
        const float playedDrive = drive * share;
        // The stored residues drive unit-input discrete states fitted at
        // 48 kHz. Player contact changes the runtime pole, while the reference
        // retains its unloaded Q. A continuous input residue c gives the
        // discrete residue c*(exp(s*T)-1)/s, so a change of Q also requires
        // s_reference/s_loaded. At fixed Q the continuous pole cancels and
        // only (p_new-1)/(p_48k-1) remains. The former real 48k/rate
        // approximation lost phase and changed the summed response at
        // higher host rates.
        const float referenceRate = 48000.0f;
        const std::complex<float> referencePole = std::polar(
            std::exp(-pi * frequency / (engine.q * referenceRate)),
            twoPi * frequency / referenceRate);
        // That hold droops by sinc(pi f / rate) at the mode, which the 48 kHz
        // fit absorbed at 48 kHz; keep the 48 kHz level at other rates rather
        // than the 48 kHz droop (+0.36 dB at 9 kHz at 96 kHz). One exactly
        // at 48 kHz.
        const auto holdGain = [frequency] (double rate)
        {
            const double x = static_cast<double>(pi) * static_cast<double>(frequency) / rate;
            return x == 0.0 ? 1.0 : std::sin(x) / x;
        };
        const float holdDroop = sampleRate_ == 48000.0 ? 1.0f
            : static_cast<float>(holdGain(48000.0) / holdGain(sampleRate_));
        std::complex<float> residueRateScale
            = (pole - 1.0f) / (referencePole - 1.0f) * holdDroop;
        if (q != engine.q)
        {
            const std::complex<float> referenceContinuousPole(
                -pi * frequency / engine.q, twoPi * frequency);
            const std::complex<float> loadedContinuousPole(
                -pi * frequency / q, twoPi * frequency);
            residueRateScale *= referenceContinuousPole / loadedContinuousPole;
        }
        const auto scaledResidue = [playedDrive, residueRateScale]
            (float real, float imaginary)
        {
            return playedDrive * std::complex<float>(real, imaginary)
                * residueRateScale;
        };
        // The Stereo mic pair is the treble-bridge microphone on the left and
        // the upper-bout one on the right, the bridge/upper-bout placement
        // a blind listener chose over the bridge's own treble/bass pair
        // (2026-09-25). The bass-bridge microphone is not heard. The air-mode
        // gain reaches the bridge microphone alone: at x4, chosen by ear, a
        // picked E2 then stands against its 2nd and 3rd harmonics within
        // 1 dB of the Eastman dreadnought recording, and equally in both
        // channels.
        const float airGain = lowBodyMode && steelBank
            ? physicalCalibration_.lowBodyModeGain : 1.0f;
        const auto left = airGain * scaledResidue(
            measured.leftReal, measured.leftImaginary);
        const auto right = scaledResidue(
            measured.upperReal, measured.upperImaginary);
        mode.leftReal = left.real();
        mode.leftImaginary = left.imag();
        mode.rightReal = right.real();
        mode.rightImaginary = right.imag();
        const auto leftMoment = airGain * scaledResidue(
            measured.leftMomentReal, measured.leftMomentImaginary);
        const auto rightMoment = scaledResidue(
            measured.upperMomentReal, measured.upperMomentImaginary);
        mode.leftMomentReal = leftMoment.real();
        mode.leftMomentImaginary = leftMoment.imag();
        mode.rightMomentReal = rightMoment.real();
        mode.rightMomentImaginary = rightMoment.imag();
    };
    // Steel on its Original guitar plays the blend (SteelBodyBlend.h): g21's
    // bank, then the joint-pole body's kept modes on its own morph.
    const int ownCount = static_cast<int>(std::min(
        bank.size(), static_cast<std::size_t>(bodyModeCount)));
    for (int index = 0; index < ownCount; ++index)
        place(bank[static_cast<std::size_t>(index)], index, morph,
              steelBank ? steelBlendG21Share(bank[static_cast<std::size_t>(index)].frequency)
                        : 1.0f);
    if (steelBank && steelBlendJointShare > 0.0f)
    {
        const auto jointMorph = steelJointMorph(anchor, parameters_.shape);
        for (const auto index : steelJointKept)
            place(steelJointRadiationModes[index], static_cast<int>(index),
                  jointMorph, steelBlendJointShare);
    }
    // The measured banks stop where their fit stopped (10 kHz before the
    // anchor's x0.900 and the Shape's plate factor: 8.1 kHz on the Jumbo,
    // 11.7 on the Parlor), and above their last mode the radiation fell
    // 20-40 dB in a third of an octave. A plate's modal density is constant
    // in frequency and its high modes overlap, so above the fitted band the
    // radiation is continued statistically: modes on a 1/16-octave grid at
    // unit modal overlap, each residue carrying the power density the bank's
    // own top octave has (per microphone and per input, force and moment),
    // with a deterministic pseudo-random phase, since the fitted high modes'
    // phases are uncorrelated, falling at the bridge's mass law (-6 dB per
    // octave) from the band's edge. Passive and linear like the rest of the
    // bank. Every construction and rate keeps the band below its top as it
    // was; above it (up to 18 kHz or 0.45 fs) about 10-19 modes are added.
    {
        const float rate = static_cast<float>(sampleRate_);
        float top = 0.0f;
        for (int index = 0; index < ownCount; ++index)
        {
            const auto& mode = bodyModes_[static_cast<std::size_t>(index)];
            if (mode.poleImaginary == 0.0f)
                continue;
            top = std::max(top, std::atan2(mode.poleImaginary, mode.poleReal)
                * rate / twoPi);
        }
        // Power density of each residue set over the top octave, from the
        // bank's own response there (the fitted modes overlap with partly
        // cancelling residues, so their energies do not add).
        std::array<double, 8> density {};
        int referenceCount = 0;
        for (int index = 0; index < ownCount && top > 0.0f; ++index)
        {
            const auto& mode = bodyModes_[static_cast<std::size_t>(index)];
            if (mode.poleImaginary != 0.0f
                && std::atan2(mode.poleImaginary, mode.poleReal) * rate / twoPi
                    >= 0.5f * top)
                ++referenceCount;
        }
        constexpr int points = 96;
        for (int point = 0; point < points && referenceCount >= 8; ++point)
        {
            const double frequency = 0.5 * top * (1.0 + (point + 0.5) / points);
            const std::complex<double> inverseZ = std::polar(1.0,
                -2.0 * 3.14159265358979323846 * frequency / rate);
            std::array<std::complex<double>, 4> response {};
            for (int index = 0; index < ownCount; ++index)
            {
                const auto& mode = bodyModes_[static_cast<std::size_t>(index)];
                if (mode.poleImaginary == 0.0f)
                    continue;
                const std::complex<double> pole(mode.poleReal, mode.poleImaginary);
                const std::complex<double> direct = 1.0 / (1.0 - pole * inverseZ);
                const std::complex<double> mirror = 1.0 / (1.0 - std::conj(pole) * inverseZ);
                const float parts[8] { mode.leftReal, mode.leftImaginary,
                    mode.rightReal, mode.rightImaginary, mode.leftMomentReal,
                    mode.leftMomentImaginary, mode.rightMomentReal,
                    mode.rightMomentImaginary };
                for (int part = 0; part < 4; ++part)
                {
                    const std::complex<double> residue(parts[2 * part], parts[2 * part + 1]);
                    response[static_cast<std::size_t>(part)]
                        += residue * direct + std::conj(residue) * mirror;
                }
            }
            for (int part = 0; part < 4; ++part)
                density[static_cast<std::size_t>(part)]
                    += std::norm(response[static_cast<std::size_t>(part)]) / points;
        }
        // Mean |H|^2 over the octave is energy x rate / bandwidth for
        // uncorrelated modes: the density per hertz, in energy. The bank's
        // response is the same at every rate, so the density is taken as its
        // 48 kHz states would hold it, and every continuation mode is built
        // at 48 kHz and converted to this rate as the measured modes are
        // (place): a zero-order-held continuous mode, with the 48 kHz hold's
        // level. Built at this rate instead, the continuation's phases stood
        // still while the measured modes' turned with the rate, and their
        // overlap near the band's edge moved 0.17 dB between 48 and 96 kHz.
        constexpr float referenceRate = 48000.0f;
        for (auto& value : density)
            value *= 0.5 * top / referenceRate;
        const double referenceWidth = 0.5 * top;
        const float step = std::exp2(1.0f / radiationContinuationStepsPerOctave);
        const float q = 1.0f / (step - 1.0f);
        const float highest = std::min(radiationContinuationTopHz, 0.45f * rate);
        const float tilt = radiationContinuationDbPerOctave;
        std::uint32_t seed = 0x9e3779b9u;
        const auto random = [&seed] ()
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return static_cast<float>(seed) * (1.0f / 4294967296.0f);
        };
        // A bank's own modes are summed in index order and the parts after
        // them four lanes at a time from a whole group of four
        // (BodyBank::render), so a bank that does not end on one (the
        // Bellido's 134) is padded to it with silent slots; otherwise the
        // lanes after its last group were advanced and never heard.
        if (slot == ownCount)
            while (slot % BodyBank::lanes != 0 && slot < bodyModeCount)
                bodyModes_[static_cast<std::size_t>(slot++)] = {};
        const int continuationEnd = std::min(slot + radiationContinuationSlots,
                                             bodyModeCount);
        for (float frequency = top * step;
             referenceCount >= 8 && frequency < highest && slot < continuationEnd;
             frequency *= step)
        {
            auto& mode = bodyModes_[static_cast<std::size_t>(slot++)];
            const float radius = std::exp(-pi * frequency / (q * rate));
            const std::complex<float> pole = std::polar(radius,
                twoPi * frequency / rate);
            mode.poleReal = pole.real();
            mode.poleImaginary = pole.imag();
            const float referenceRadius = std::exp(-pi * frequency
                / (q * referenceRate));
            const std::complex<float> referencePole = std::polar(
                referenceRadius, twoPi * frequency / referenceRate);
            const auto holdGain = [frequency] (double at)
            {
                const double x = static_cast<double>(pi)
                    * static_cast<double>(frequency) / at;
                return std::sin(x) / x;
            };
            const std::complex<float> rateScale = rate == referenceRate
                ? std::complex<float>(1.0f, 0.0f)
                : (pole - 1.0f) / (referencePole - 1.0f)
                    * static_cast<float>(holdGain(referenceRate) / holdGain(rate));
            // This mode's bandwidth share of the density, as energy.
            const double spacing = frequency * (step - 1.0f / step) * 0.5;
            const double level = std::pow(10.0, tilt / 20.0
                * std::log2(frequency / top));
            const double energyToResidue = 1.0
                - static_cast<double>(referenceRadius) * referenceRadius;
            float* parts[8] { &mode.leftReal, &mode.leftImaginary,
                &mode.rightReal, &mode.rightImaginary, &mode.leftMomentReal,
                &mode.leftMomentImaginary, &mode.rightMomentReal,
                &mode.rightMomentImaginary };
            for (int part = 0; part < 4; ++part)
            {
                const double magnitude = level * std::sqrt(
                    density[static_cast<std::size_t>(part)] / referenceWidth
                    * spacing * energyToResidue);
                const float phase = twoPi * random();
                const std::complex<float> residue = std::complex<float>(
                    static_cast<float>(magnitude) * std::cos(phase),
                    static_cast<float>(magnitude) * std::sin(phase)) * rateScale;
                *parts[2 * part] = residue.real();
                *parts[2 * part + 1] = residue.imag();
            }
        }
        while (slot < continuationEnd)
            bodyModes_[static_cast<std::size_t>(slot++)] = {};
    }
    const int count = slot;
    for (; slot < bodyModeCount; ++slot)
        bodyModes_[static_cast<std::size_t>(slot)] = {};
    // A quarter-step toward the other model's broad brightness. Apply once
    // to the completed bank: the continuation above takes its density from
    // the unvoiced measured modes, so none of its gain is counted twice.
    // A positive gain preserves every mode's complex phase and mic balance.
    for (int index = 0; index < count; ++index)
    {
        auto& mode = bodyModes_[static_cast<std::size_t>(index)];
        if (mode.poleImaginary == 0.0f)
            continue;
        const float frequency = std::atan2(mode.poleImaginary, mode.poleReal)
            * static_cast<float>(sampleRate_) / twoPi;
        const float gain = detail::modelConvergenceGain(frequency, named);
        for (auto* residue : { &mode.leftReal, &mode.leftImaginary,
                &mode.rightReal, &mode.rightImaginary,
                &mode.leftMomentReal, &mode.leftMomentImaginary,
                &mode.rightMomentReal, &mode.rightMomentImaginary })
            *residue *= gain;
    }
    // g21's own modes are summed in index order, as ever; the parts after
    // them in vector accumulators (BodyBank::render). Shape and Wood keep
    // the same modes in the same slots ringing (sameBodyBank, audit F14).
    bodyBank_.load(bodyModes_, count, ownCount,
                   bodyConfigured_ && !sameBodyBank);
    bodyBank_.captureFilter.configure(sampleRate_,
        parameters_.guitarModel == GuitarModel::Bellido1978);
    configuredGuitarModel_ = parameters_.guitarModel;
    configuredBodyShape_ = parameters_.shape;
    configuredBodyMaterial_ = parameters_.bodyMaterial;
    bodyConfigured_ = true;
}

void AcustraEngine::configureBridge(bool keepModalState) noexcept
{
    ++voiceConfigurationGeneration_;
    bridgeLoad_.immediateHeave = 0.0f;
    bridgeLoad_.immediateCross = 0.0f;
    bridgeLoad_.immediateRock = 0.0f;
    const float rate = static_cast<float>(sampleRate_);
    // Each mode's residue matrix multiplies the continuous mobility
    // s/(s^2 + 2 damping s + omega^2), which is shared by both coordinates.
    // A per-mode prewarped bilinear transform preserves its measured centre
    // frequency, and a positive semidefinite residue matrix keeps every
    // string's own combination of the three positive real.
    const auto configure = [&] (std::size_t index, float frequency, float q,
                                float heave, float cross, float rock)
    {
        auto& heaveMode = bridgeLoad_.heaveModes[index];
        auto& rockMode = bridgeLoad_.rockModes[index];
        bridgeLoad_.residueHeave[index] = 0.0f;
        bridgeLoad_.residueCross[index] = 0.0f;
        bridgeLoad_.residueRock[index] = 0.0f;
        bridgeLoad_.rocking[index] = false;
        // The plate conductance floor is broadband: it is kept at every
        // rate, from its 48 kHz design (below), rather than dropped with the
        // modes above 0.45 fs, which took it away below a 13 kHz host.
        const bool plateFloor = index == static_cast<std::size_t>(bridgeModeCount);
        const bool active = (heave > 0.0f || rock > 0.0f)
                          && (plateFloor || frequency < 0.45f * rate);
        if (!active)
        {
            for (auto* mode : { &heaveMode, &rockMode })
            {
                mode->denominator1 = mode->denominator2 = 0.0;
                mode->numerator1 = mode->numerator2 = 0.0;
                mode->reset();
            }
            return;
        }
        const double rateD = sampleRate_;
        const double bilinearD = 2.0 * rateD;
        // Each mode is prewarped at the host rate to keep its centre. The
        // over-damped floor has no centre to keep: its plateau was fitted as
        // 48 kHz renders it, so it keeps its 48 kHz analog prototype at every
        // rate (prewarped at the host rate its plateau moved +0.2-0.5 dB at
        // 96 kHz, 5-7% faster decay above 1 kHz).
        const double prewarpRate = plateFloor ? 48000.0 : rateD;
        const double omega = 2.0 * prewarpRate * std::tan(
            static_cast<double>(pi) * frequency / prewarpRate);
        // The same additional modal loss as the radiating measured body;
        // retain the broadband conductance floor's calibrated prototype.
        const float loadedQ = detail::playerLoadedBodyQ(frequency, q,
            !plateFloor && parameters_.guitarModel == GuitarModel::Bellido1978
                && performanceRealism_.playerBodyLoading);
        const double damping = omega / (2.0 * loadedQ);
        const double denominator0 = bilinearD * bilinearD
            + 2.0 * damping * bilinearD + omega * omega;
        const double denominator1 = (-2.0 * bilinearD * bilinearD
            + 2.0 * omega * omega) / denominator0;
        const double denominator2 = (bilinearD * bilinearD
            - 2.0 * damping * bilinearD + omega * omega) / denominator0;
        const float immediate = static_cast<float>(bilinearD / denominator0);
        for (auto* mode : { &heaveMode, &rockMode })
        {
            mode->denominator1 = denominator1;
            mode->denominator2 = denominator2;
            mode->numerator1 = -static_cast<double>(immediate) * denominator1;
            mode->numerator2 = -static_cast<double>(immediate)
                * (1.0 + denominator2);
            // The same bank retuned (Shape, Wood) keeps ringing as it was;
            // the new coefficients carry the state on from here.
            if (!keepModalState)
                mode->reset();
        }
        bridgeLoad_.residueHeave[index] = heave;
        bridgeLoad_.residueCross[index] = cross;
        bridgeLoad_.residueRock[index] = rock;
        bridgeLoad_.rocking[index] = rock > 0.0f || cross != 0.0f;
        bridgeLoad_.immediateHeave += heave * immediate;
        bridgeLoad_.immediateCross += cross * immediate;
        bridgeLoad_.immediateRock += rock * immediate;
    };

    const auto bank = measuredBridgeBank(parameters_.guitarModel);
    // The bridge belongs to the same body as the radiation, so Shape moves
    // its modes by the coupled model's factors (see shapeBridgeMode).
    const auto morph = bodyShapeMorph(
        measuredBodyBank(parameters_.guitarModel),
        anchorTransformFor(parameters_.guitarModel),
        anchorBodyFor(parameters_.guitarModel),
        targetBodyFor(parameters_.guitarModel, parameters_.shape));
    bridgeShapeA0_ = morph.a0Frequency;
    bridgeShapeT1_ = morph.t1Frequency;
    bridgeShapePlate_ = morph.plateFrequency;
    bridgeShapeT1UpperHz_ = morph.t1UpperHz;
    // Additional bodies already have a qualified absolute mobility. The old
    // corpus compensation belongs to Original; the high-band conductance
    // floor is every modal fit's (below).
    // Keep the fitting control as a relative multiplier around the new body's
    // measured response, with unit gain at the shipped calibration.
    // Steel's own bridge is the flamenca's: its top is about 3.6 times as
    // compliant as a steel-string guitar's, so its residues are brought to a
    // measured steel-string guitar's level (steelTopMobilityRatio, the Fylde
    // Falstaff's) under the same fitted scale. The plate floor below is not
    // scaled. That ratio corrects a proxy - a flamenca standing in for a
    // steel-string guitar - and nothing else: the Bellido is deliberately
    // steel on its own measured classical top, at that top's full mobility
    // (about 1.8 times the level steel's own bridge is brought to). It
    // drains the strings faster (E4 at 11.7 dB/s against 10.0 on the
    // Original Auditorium) and is not scaled toward a steel-string guitar
    // no measurement of this pair describes.
    const bool ownBridge = parameters_.guitarModel == GuitarModel::Original;
    const float scale = physicalCalibration_.bridgeMobilityScale
        / (ownBridge ? 1.0f : fittedPhysicalCalibration.bridgeMobilityScale)
        * (ownBridge ? detail::steelTopMobilityRatio : 1.0f);
    const auto anchor = anchorTransformFor(parameters_.guitarModel);
    const auto wood = woodFactorsFor(parameters_.bodyMaterial,
                                     parameters_.guitarModel);
    if (ownBridge)
    {
        // The steel blend's bridge (SteelBodyBlend.h), part after part: B's
        // modes first, in their slots as ever, the parallel parts after them.
        std::size_t slot = 0;
        visitSteelBlendBridge(bridgeShapeA0_, bridgeShapeT1_, bridgeShapePlate_,
            bridgeShapeT1UpperHz_, anchor, morph,
            steelJointMorph(anchor, parameters_.shape), wood,
            physicalCalibration_,
            [&] (const detail::MeasuredBridgeMode& source,
                 const detail::MeasuredBridgeMode& measured, float level, bool)
            {
                const bool include = includeMeasuredBridgeMode(source);
                const float heave = include ? measured.heave * scale : 0.0f;
                const float cross = include ? measured.cross * scale : 0.0f;
                const float rock = include ? measured.rock * scale : 0.0f;
                configure(slot++, measured.frequency, measured.q,
                          heave * level, cross * level, rock * level);
            });
        for (; slot < static_cast<std::size_t>(bridgeModeCount); ++slot)
            configure(slot, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else
    {
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(bridgeModeCount); ++index)
        {
            if (index >= bank.size())
            {
                configure(index, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f);
                continue;
            }
            const bool include = includeMeasuredBridgeMode(bank[index]);
            auto measured = shapeBridgeMode(bank[index], bridgeShapeA0_,
                bridgeShapeT1_, bridgeShapePlate_, bridgeShapeT1UpperHz_);
            const auto pole = bellidoBridgePole(index, bank[index], measured,
                anchor, morph, wood, physicalCalibration_);
            measured.frequency = pole.frequency;
            measured.q = pole.q;
            configure(index, measured.frequency, measured.q,
                      include ? measured.heave * scale : 0.0f,
                      include ? measured.cross * scale : 0.0f,
                      include ? measured.rock * scale : 0.0f);
        }
    }

    // The plate conductance floor is the dense overlap of a plate's own
    // driving-point response, which the archive never resolves into a
    // rocking pair, so it enters as heave alone.
    // The Bellido's modal fit loses the same high-band conductance
    // between its overlapping modes as g21's does, so it takes the same
    // floor: without it the Bellido's strings kept 4-10 kHz partials a
    // third of the Original's drain would leave, and it played 11-15 dB
    // over the recordings at 5-10 kHz against the Original's 1-5.
    const auto plate = plateConductanceMode(physicalCalibration_);
    configure(static_cast<std::size_t>(bridgeModeCount), plate.frequency,
              plate.q, plate.weight, 0.0f, 0.0f);
    if (!keepModalState)
    {
        bridgeLoad_.pastHeave = 0.0f;
        bridgeLoad_.pastRock = 0.0f;
    }
    // A slot whose sections and residues are all zero returns +-0 and adds
    // +-0 to sums that start at +0, so leaving it out changes no bit.
    configuredBridgeModel_ = parameters_.guitarModel;
    configuredBridgeShape_ = parameters_.shape;
    configuredBridgeMaterial_ = parameters_.bodyMaterial;
    bridgeLoad_.activeModeCount = 0;
    for (std::size_t index = 0; index < bridgeLoad_.heaveModes.size(); ++index)
    {
        const auto zero = [] (const BridgeMode& mode)
        {
            return mode.denominator1 == 0.0 && mode.denominator2 == 0.0
                && mode.numerator1 == 0.0 && mode.numerator2 == 0.0;
        };
        if (zero(bridgeLoad_.heaveModes[index]) && zero(bridgeLoad_.rockModes[index])
            && bridgeLoad_.residueHeave[index] == 0.0f
            && bridgeLoad_.residueCross[index] == 0.0f
            && bridgeLoad_.residueRock[index] == 0.0f)
            continue;
        bridgeLoad_.activeModes[static_cast<std::size_t>(
            bridgeLoad_.activeModeCount++)] = static_cast<std::uint8_t>(index);
    }
}

// The rebuild a construction change asked for while the bridge was still
// fading, now that the fade has ended: it fades from the bank that settled,
// or, when the bridge was cleared instead, is simply built.
void AcustraEngine::applyPendingBridge(bool fade) noexcept
{
    bridgeUpdatePending_ = false;
    if (fade)
    {
        fadingBridgeLoad_ = bridgeLoad_;
        fadingBridgeModel_ = configuredBridgeModel_;
        fadingBridgeShape_ = configuredBridgeShape_;
        fadingBridgeMaterial_ = configuredBridgeMaterial_;
        bridgeLoadFade_ = 0.0f;
        bridgeDerivativesCrossConfigure_ = true;
    }
    configureBridge(configuredBridgeModel_ == parameters_.guitarModel);
    configuredBridgeModel_ = parameters_.guitarModel;
}

float AcustraEngine::bridgePhaseDelay(float frequency,
                                      int stringIndex) const noexcept
{
    return bridgePhaseDelay(bridgePortMobility(frequency, stringIndex),
                            frequency, stringIndex);
}

float AcustraEngine::bridgePhaseDelay(const PortMobility& port, float frequency,
                                      int stringIndex) const noexcept
{
    if (!port.valid)
        return 0.0f;
    const auto notes = openNotes(parameters_.tuning);
    const float impedance = stringImpedance(
        stringIndex, notes[static_cast<std::size_t>(stringIndex)]);
    // This estimates one string's return phase from the body and anchors.
    // Other strings' frequency-dependent loopback impedances are omitted here,
    // although the runtime junction includes their returning waves. It is an
    // isolated-port tuning approximation, not the coupled instrument's poles:
    // zero loop phase at the note is not where the damped string-body pole
    // sits beside a strong lossy mode, whose reflection loss changes fastest
    // there. Measured (audit F15, README Known gaps): up to about 4 cents
    // (G3 on the Dreadnought then measured), 5 with the other strings
    // silenced; a one-step pole correction recovers at most 2 of those, so
    // it is not applied.
    const float characteristicAdmittance = 1.0f / impedance;
    // This is the folded full-round-trip multiplier -b/a.  Its phase is the
    // phase contributed by both measured body motion and the saddle anchor; the
    // speaking-string delay is shortened by exactly that amount when tuned.
    const std::complex<float> selfReflection
        = (characteristicAdmittance - port.normal)
        / (characteristicAdmittance + port.normal);
    const float digitalOmega = twoPi * frequency
        / static_cast<float>(sampleRate_);
    return -std::arg(selfReflection) / digitalOmega;
}

const AcustraEngine::BridgeMobilityTable&
AcustraEngine::bridgeMobilityTable() const noexcept
{
    // Every input the terms below are computed from, as exact bits.
    const float rate = static_cast<float>(sampleRate_);
    const auto bank = measuredBridgeBank(parameters_.guitarModel);
    const bool ownBridge = parameters_.guitarModel == GuitarModel::Original;
    const float scale = physicalCalibration_.bridgeMobilityScale
        / (ownBridge ? 1.0f : fittedPhysicalCalibration.bridgeMobilityScale)
        * (ownBridge ? detail::steelTopMobilityRatio : 1.0f);
    const auto plate = plateConductanceMode(physicalCalibration_);
    const auto wood = woodFactorsFor(parameters_.bodyMaterial,
                                     parameters_.guitarModel);
    const std::array<std::uint32_t, 13> key {
        exact::bits(rate), exact::bits(bridgeShapeA0_), exact::bits(bridgeShapeT1_),
        exact::bits(bridgeShapePlate_), exact::bits(bridgeShapeT1UpperHz_),
        exact::bits(scale), exact::bits(plate.frequency), exact::bits(plate.q),
        exact::bits(plate.weight),
        exact::bits(wood.frequency), exact::bits(wood.q),
        // The joint-pole body's own Shape morph (visitSteelBlendBridge).
        static_cast<std::uint32_t>(parameters_.shape),
        static_cast<std::uint32_t>(performanceRealism_.playerBodyLoading) };
    auto& table = bridgeMobilityTable_;
    if (table.valid && table.bank == bank.begin() && table.key == key)
        return table;
    // The terms as bridgePortMobility computed them in its mode loop.
    const float bilinear = 2.0f * rate;
    const auto anchor = anchorTransformFor(parameters_.guitarModel);
    const auto morph = bodyShapeMorph(measuredBodyBank(parameters_.guitarModel),
        anchor, anchorBodyFor(parameters_.guitarModel),
        targetBodyFor(parameters_.guitarModel, parameters_.shape));
    table.count = 0;
    // One mode's terms; `level` multiplies its residues relative to scale.
    const auto add = [&] (const detail::MeasuredBridgeMode& source,
                          const detail::MeasuredBridgeMode& measured, float level)
    {
        if (measured.frequency >= 0.45f * rate
            || !includeMeasuredBridgeMode(source))
            return;
        auto& mode = table.modes[static_cast<std::size_t>(table.count++)];
        mode.omega = bilinear * std::tan(
            pi * measured.frequency / rate);
        const float q = detail::playerLoadedBodyQ(measured.frequency, measured.q,
            !ownBridge && performanceRealism_.playerBodyLoading);
        mode.damping = mode.omega / (2.0f * q);
        mode.heave = measured.heave * level;
        mode.cross = measured.cross * level;
        mode.rock = measured.rock * level;
    };
    table.ordered = 0;
    if (ownBridge)
        visitSteelBlendBridge(bridgeShapeA0_, bridgeShapeT1_, bridgeShapePlate_,
            bridgeShapeT1UpperHz_, anchor, morph,
            steelJointMorph(anchor, parameters_.shape), wood,
            physicalCalibration_,
            [&] (const detail::MeasuredBridgeMode& source,
                 const detail::MeasuredBridgeMode& measured, float level, bool own)
            {
                add(source, measured, level);
                // B's own modes come first and keep their evaluation.
                if (own)
                    table.ordered = table.count;
            });
    else
    {
        for (std::size_t index = 0; index < bank.size(); ++index)
        {
            const auto& source = bank[index];
            auto measured = shapeBridgeMode(source, bridgeShapeA0_,
                bridgeShapeT1_, bridgeShapePlate_, bridgeShapeT1UpperHz_);
            const auto pole = bellidoBridgePole(index, source, measured,
                anchor, morph, wood, physicalCalibration_);
            measured.frequency = pole.frequency;
            measured.q = pole.q;
            add(source, measured, 1.0f);
        }
        table.ordered = table.count;
    }
    table.scale = scale;
    table.plate = plate.weight > 0.0f;
    if (table.plate)
    {
        // The floor's 48 kHz prototype, as configureBridge builds it.
        table.plateOmega = 96000.0f * std::tan(pi * plate.frequency / 48000.0f);
        table.plateDamping = table.plateOmega / (2.0f * plate.q);
        table.plateWeight = plate.weight;
    }
    table.bank = bank.begin();
    table.key = key;
    table.valid = true;
    return table;
}

AcustraEngine::PortMobility AcustraEngine::bridgePortMobility(
    float frequency, int stringIndex) const noexcept
{
    PortMobility result {};
    if (!bridgeCouplingEnabled_ || !(frequency > 0.0f))
        return result;

    const float rate = static_cast<float>(sampleRate_);
    const float bilinear = 2.0f * rate;
    const float digitalOmega = twoPi * frequency / rate;
    const std::complex<float> s(
        0.0f, bilinear * std::tan(0.5f * digitalOmega));
    // Each string ends at its own point on the saddle, so it is its own
    // combination of the heaving and rocking banks that folds into its tuned
    // delay, not one shared driving point.
    const float arm = saddleLeverArm(stringIndex);
    std::complex<float> mobilityHeave {};
    std::complex<float> mobilityCross {};
    std::complex<float> mobilityRock {};
    const auto& table = bridgeMobilityTable();
    // Both are the same numbers for every mode.
    const std::complex<float> scaledS = table.scale * s;
    const std::complex<float> sSquared = s * s;
    for (int index = 0; index < table.ordered; ++index)
    {
        const auto& mode = table.modes[static_cast<std::size_t>(index)];
        const float omega = mode.omega;
        const float damping = mode.damping;
        const std::complex<float> shape = exact::divide(scaledS,
            sSquared + 2.0f * damping * s + omega * omega);
        mobilityHeave += mode.heave * shape;
        mobilityCross += mode.cross * shape;
        mobilityRock += mode.rock * shape;
    }
    if (table.ordered < table.count)
    {
        // The same section at s = jw in real arithmetic: j k / (a + j b) with
        // k = scale w, a = omega^2 - w^2 and b = 2 damping w is
        // k (b + j a) / (a^2 + b^2), one real division per mode.
        const float w = s.imag();
        const float k = table.scale * w;
        float heaveRe = 0.0f, heaveIm = 0.0f, crossRe = 0.0f, crossIm = 0.0f;
        float rockRe = 0.0f, rockIm = 0.0f;
        for (int index = table.ordered; index < table.count; ++index)
        {
            const auto& mode = table.modes[static_cast<std::size_t>(index)];
            const float a = mode.omega * mode.omega - w * w;
            const float b = 2.0f * mode.damping * w;
            const float gain = k / (a * a + b * b);
            const float re = gain * b;
            const float im = gain * a;
            heaveRe += mode.heave * re;
            heaveIm += mode.heave * im;
            crossRe += mode.cross * re;
            crossIm += mode.cross * im;
            rockRe += mode.rock * re;
            rockIm += mode.rock * im;
        }
        mobilityHeave += std::complex<float>(heaveRe, heaveIm);
        mobilityCross += std::complex<float>(crossRe, crossIm);
        mobilityRock += std::complex<float>(rockRe, rockIm);
    }

    if (table.plate)
    {
        const float omega = table.plateOmega;
        const float damping = table.plateDamping;
        mobilityHeave += exact::divide(table.plateWeight * s,
            sSquared + 2.0f * damping * s + omega * omega);
    }

    // Body and anchor are in parallel at the saddle, but on a bridge with two
    // degrees of freedom that parallel has to be taken as matrices and only
    // then read at this string's own point: the anchor a string finds is
    // softer at the ends, where it can rock the bridge against the others,
    // than in the middle.
    float stiffness0 = 0.0f;
    float stiffness1 = 0.0f;
    float stiffness2 = 0.0f;
    bridgeAnchorMoments(stiffness0, stiffness1, stiffness2);
    // (Y^-1 + K/s)^-1 = det(Y) * (adj(Y) + det(Y) K/s)^-1, which needs no
    // division by a determinant that goes to zero wherever the bank has no
    // rocking residue.
    const std::complex<float> determinant
        = mobilityHeave * mobilityRock - mobilityCross * mobilityCross;
    const std::complex<float> ratio = determinant / s;
    const std::complex<float> a00 = mobilityRock + ratio * stiffness0;
    const std::complex<float> a01 = -mobilityCross + ratio * stiffness1;
    const std::complex<float> a11 = mobilityHeave + ratio * stiffness2;
    const std::complex<float> inner = a00 * a11 - a01 * a01;
    std::complex<float> effectiveMobility {};
    if (std::abs(inner) > 0.0f)
    {
        effectiveMobility = determinant
            * (a11 - 2.0f * arm * a01 + arm * arm * a00) / inner;
        // The same parallel read at the parallel polarisation's port, which
        // is (h/a) times the rocking (saddleHeightRatio): its own mobility
        // and its transfer mobility to this string's normal port.
        const float eta = saddleHeightRatio();
        if (eta != 0.0f)
        {
            const std::complex<float> rockRock = determinant * a00 / inner;
            const std::complex<float> heaveRock = -determinant * a01 / inner;
            result.transfer = -eta * (heaveRock + arm * rockRock);
            result.parallel = eta * eta * rockRock;
        }
    }
    else
    {
        // A bank with no rocking residue anywhere leaves the determinant, and
        // with it the whole adjugate above, exactly zero.  The rocking
        // coordinate is then immovable, every string sees the same heave port
        // with the anchor springs in parallel, and the load is the scalar
        // (1/Yhh + k0/s)^-1; no unmeasured rocking response is added to it.
        const std::complex<float> denominator
            = s + stiffness0 * mobilityHeave;
        if (!(std::abs(denominator) > 0.0f))
            return result;
        effectiveMobility = mobilityHeave * s / denominator;
    }
    result.normal = effectiveMobility;
    result.valid = true;
    return result;
}

// The normal loop is tuned against its own port and the parallel loop shares
// its length (so the pair splits by the bridge's pull on the normal member,
// README "Both polarisations are one string"), and where the saddle rocks
// the two are also coupled through it (the transfer mobility) and
// ring as one pair of modes. Near the fundamental each loop's round trip is
// its gain and phase times the saddle's 2x2 reflection R = (Y0 - Y)(Y0 + Y)^-1,
// so the pair's modes are the eigenvalues of diag(G_normal, G_parallel) R: a
// mode's phase over 2 pi is how far it sits from the requested pitch, as a
// fraction of it, and its magnitude per round trip how fast it decays. The
// normal loop's own compensation already puts its uncoupled mode on the
// request; what the coupling adds is read here as the shift of the note's
// sustained, energy-weighted centre - the pitch a tuner or a player's ear
// reads once the attack has passed. Each mode is weighted by its
// normal-polarisation share (the normal polarisation radiates through the
// heave, far more strongly than the parallel one through the rocking) times
// the energy it still carries after the first 100 ms,
// |lambda|^(2n)/(1 - |lambda|^2) per unit start with n the periods in
// 100 ms. The pluck's own division between the planes is not in the weight,
// which is why a steel open B, plucked mostly parallel to the top, settles
// 2.5 cents from its request. That matters where the rocking is strong: on
// the flamenca's bridge the open B's normal-dominated mode sheds 0.24 dB a
// period while its parallel-dominated partner sheds 0.011, so the note's
// sustain is the partner's and its attack briefly sits apart from it. When
// the pair is weakly coupled the centre is the normal member's own shift.
// Both loops are lengthened by that fraction; the doublet's split and its
// beat are left as the coupling makes them. Zero wherever the saddle's
// rocking was not measured.
float AcustraEngine::coupledPolarisationDetune(
    const PortMobility& port, float impedance, float bentImpedance,
    float frequency, float parallelExtraDelay, float normalGain,
    float parallelGain) const noexcept
{
    if (!port.valid || port.transfer == std::complex<float>{})
        return 0.0f;
    using Complex = std::complex<double>;
    // The junction reads the string's port at its bent impedance; the normal
    // loop's own compensation (bridgePhaseDelay) was taken at the unbent one.
    const double admittance = 1.0 / static_cast<double>(bentImpedance);
    const double unbentAdmittance = 1.0 / static_cast<double>(impedance);
    const Complex normal = port.normal;
    const Complex transfer = port.transfer;
    const Complex parallel = port.parallel;
    const Complex b00 = admittance + normal;
    const Complex b11 = admittance + parallel;
    const Complex inverseDeterminant = 1.0 / (b00 * b11 - transfer * transfer);
    // R = (Y0 I - Y)(Y0 I + Y)^-1 with Y symmetric.
    const Complex c00 = admittance - normal;
    const Complex c11 = admittance - parallel;
    const Complex r00 = (c00 * b11 + transfer * transfer) * inverseDeterminant;
    const Complex r01 = (-c00 * transfer - transfer * b00) * inverseDeterminant;
    const Complex r10 = (-transfer * b11 - c11 * transfer) * inverseDeterminant;
    const Complex r11 = (transfer * transfer + c11 * b00) * inverseDeterminant;
    // The normal loop's delay cancels its own uncoupled reflection's phase at
    // the request; the parallel loop shares that bare length plus its end
    // correction.
    const double ownPhase = std::arg(Complex((unbentAdmittance - normal)
                                             / (unbentAdmittance + normal)));
    const double digitalOmega = 2.0 * piDouble
        * static_cast<double>(frequency) / sampleRate_;
    const Complex gainNormal = std::polar(static_cast<double>(normalGain), -ownPhase);
    const Complex gainParallel = std::polar(static_cast<double>(parallelGain),
        -ownPhase - digitalOmega * static_cast<double>(parallelExtraDelay));
    const Complex a00 = gainNormal * r00;
    const Complex a01 = gainNormal * r01;
    const Complex a10 = gainParallel * r10;
    const Complex a11 = gainParallel * r11;
    const Complex halfTrace = 0.5 * (a00 + a11);
    const Complex root = std::sqrt(halfTrace * halfTrace - (a00 * a11 - a01 * a10));
    constexpr double onsetSeconds = 0.1;
    const double onsetPeriods = onsetSeconds * static_cast<double>(frequency);
    double weighted = 0.0;
    double weights = 0.0;
    for (const Complex eigenvalue : { halfTrace + root, halfTrace - root })
    {
        const double normalPart = std::norm(a01);
        const double parallelPart = std::norm(eigenvalue - a00);
        if (!(normalPart + parallelPart > 0.0))
            continue;
        const double share = normalPart / (normalPart + parallelPart);
        const double perPeriod = std::norm(eigenvalue);
        const double energy = share * std::pow(perPeriod, onsetPeriods)
            / std::max(1.0 - perPeriod, 1.0e-6);
        weighted += energy * std::arg(eigenvalue);
        weights += energy;
    }
    if (!(weights > 0.0))
        return 0.0f;
    const double detune = weighted / (weights * 2.0 * piDouble);
    return exact::isfinite(detune)
        ? static_cast<float>(std::clamp(detune, -0.02, 0.02)) : 0.0f;
}

// A string's two transverse polarisations do not reach the body alike. The
// one normal to the soundboard pushes the saddle down; the one parallel to it
// pushes the saddle crown sideways at the crown's height h over the top, a
// moment about the string's own axis - the rocking the archive's two
// bridge-end impacts measure. In the normalized rocking coordinate r = a*theta
// that moment is (h/a) times the horizontal force, and the crown moves
// (h/a)*r sideways, so the parallel polarisation's port is (h/a)^2 times the
// measured rocking mobility and its load reaches the microphones through the
// measured moment paths. That is how a guitar partial becomes the doublet
// Woodhouse measures (Acta Acustica 90 (2004) 945-965, Sec. 4.3: the normal
// pluck excites the upper member, the parallel pluck the lower, and parallel
// plucks are markedly quieter). h is the published height of the strings over
// the top at the bridge, to the string's lower bound (R. Mores, "List of
// guitars measured", 2021, https://zenodo.org/records/4604577, column HSaT):
// 8.1 mm on g21, the Original, and 8.6 mm on the 1978 Bellido, g35. a is the 23.2 mm
// half-spacing the archive's impacts are placed at (saddleLeverArm).
float AcustraEngine::saddleHeightRatio() const noexcept
{
    constexpr float impactHalfSpacing = 0.0232f;
    switch (parameters_.guitarModel)
    {
        case GuitarModel::Original:
            return 0.0081f / impactHalfSpacing;
        case GuitarModel::Bellido1978:
            return 0.0086f / impactHalfSpacing;
        default:
            return 0.0f;
    }
}

void AcustraEngine::bridgeAnchorMoments(float& stiffness0,
                                        float& stiffness1,
                                        float& stiffness2) const noexcept
{
    // Every string is anchored behind the saddle whether or not it is being
    // played, but each stub stands at its own point on it, so the six springs
    // are one stiffness matrix rather than one sum. The stub holds the crown
    // sideways as well as down, so it also stiffens the rocking the parallel
    // polarisation drives, by (h/a)^2 of its own stiffness.
    const float eta = saddleHeightRatio();
    stiffness0 = stiffness1 = stiffness2 = 0.0f;
    for (int string = 0; string < stringCount; ++string)
    {
        const float arm = saddleLeverArm(string);
        const float stiffness
            = voices_[static_cast<std::size_t>(string)].bridgeTailStiffness;
        stiffness0 += stiffness;
        stiffness1 += arm * stiffness;
        stiffness2 += arm * arm * stiffness;
        if (eta != 0.0f)
            stiffness2 += eta * eta * stiffness;
    }
}

void AcustraEngine::configureVoice(Voice& voice, int stringIndex,
                                    int midiNote, bool clearDelay,
                                    bool refreshPickReference,
                                    bool transitionRetune) noexcept
{
    const auto& physical = physicalCalibration_.steel;
    const auto index = static_cast<std::size_t>(stringIndex);
    if (clearDelay)
    {
        voice.contactTravelEnabled = false;
        voice.contactTravel.active = false;
        voice.contactNoiseTravel.active = false;
        voice.contactNoiseSamples = 0;
        voice.legatoContactTravel.active = false;
        voice.legatoContactSamples = 0;
    }
    constexpr float scaleLength = 0.648f;
    // A natural harmonic is the open string vibrating in its nth mode, so the
    // waveguide runs at the open pitch and the mode number comes from the
    // released shape. voice.midiNote stays the requested note, which is what
    // note-off and ownership match on.
    const int stoppedMidi = voice.harmonic > 1 ? voice.openMidi : midiNote;
    const int fret = std::max(0, stoppedMidi - voice.openMidi);
    const auto bend = voiceBend(voice);
    const float performedBend = bend.performed;
    const float memberBendSemitones = bend.member;
    const float vibratoInterval = vibratoSemitones(voice, fret);
    const float axialRigidity = stringAxialRigidity(stringIndex);
    const float bendingDiameter = steelBendingDiameter[index];
    const PitchGeometryKey pitchKey {
        stoppedMidi, voice.openMidi, stringIndex,
        exact::bits(static_cast<float>(sampleRate_)), exact::bits(performedBend),
        exact::bits(memberBendSemitones), exact::bits(vibratoInterval),
        exact::bits(voice.attackPitchCents) };
    auto& geometry = voice.pitchGeometry;
    if (!geometry.valid || !(pitchKey == geometry.key))
    {
        const float fretLength = scaleLength * std::exp2(-static_cast<float>(fret) / 12.0f);
        const float unbentFrequency = midiFrequency(stoppedMidi);
        // The bounded Kirchhoff-Carrier surrogate follows the waveguide's inferred
        // slope energy; its delay target slews on the existing 6 ms time constant.
        // A conventional wheel or MPE manager slides the fretting point; its
        // interval therefore shortens the same physical string that the delay
        // retunes. B = pi^2 EI/(T L^2), the axial modes and the end correction
        // must all follow that length. A member's lateral bend changes tension
        // at its fixed fret and contributes no shortening. Keep the exact fret
        // geometry at zero slide, and bound extreme wheels by the same band as
        // the finite waveguide before taking a physical reciprocal.
        const float slideInterval = clamp(performedBend - memberBendSemitones,
                                           -192.0f, 192.0f);
        const float slideFrequency = clamp(unbentFrequency
            * std::exp2(slideInterval / 12.0f),
            static_cast<float>(sampleRate_) / (maximumDelaySamples - 3.0f),
            0.24f * static_cast<float>(sampleRate_));
        const float soundingLength = slideInterval == 0.0f ? fretLength
            : fretLength * unbentFrequency / slideFrequency;
        // Apply the existing fret-decay calibration to the same fretting point,
        // including a fractional slide. Outside the playable fretboard retain
        // its nearest supported target rather than extrapolating the fit.
        const float speakingFret = slideInterval == 0.0f ? static_cast<float>(fret)
            : clamp(static_cast<float>(fret)
                + 12.0f * std::log2(slideFrequency / unbentFrequency),
                0.0f, static_cast<float>(fretCount));
        // The wheel's vibrato is the fretting hand modulating the string's
        // tension at a fixed length (see vibratoSemitones), and a tension
        // modulation is heard as a pitch modulation: the same excursion goes into
        // the performed interval, so the tuned delay carries it, and into the
        // tension below, so the inharmonicity and the junction port carry it too.
        // The wheel down is + 0.0f, which is exact.
        const float performedSemitones = clamp(performedBend, -192.0f, 192.0f)
            + 0.01f * voice.attackPitchCents + vibratoInterval;
        // An eight-octave RPN range is legal even when the requested pitch is
        // outside this finite waveguide's representable band. Keep the performed
        // interval, then bound the physical frequency at the existing delay-line
        // limits so hostile wheels remain finite.
        const float frequency = clamp(
            unbentFrequency * std::exp2(performedSemitones / 12.0f),
            static_cast<float>(sampleRate_) / (maximumDelaySamples - 3.0f),
            0.24f * static_cast<float>(sampleRate_));
        const float contactPeriodSamples = static_cast<float>(sampleRate_) / frequency;
        // Frequency of the physical gesture before the few-cent transient
        // from the string's own transverse energy. A loss realised once per
        // round trip must be designed at the pitch being played: probing the
        // old-fundamental section higher on its curve over-damps bent partials.
        const float lossDesignFrequency = clamp(unbentFrequency * std::exp2(
            (clamp(performedBend, -192.0f, 192.0f) + vibratoInterval) / 12.0f),
            static_cast<float>(sampleRate_) / (maximumDelaySamples - 3.0f),
            0.24f * static_cast<float>(sampleRate_));

        const float openFrequency = midiFrequency(voice.openMidi);
        const float openWaveSpeed = 2.0f * scaleLength * openFrequency;
        const float standardWaveSpeed = 2.0f * scaleLength
            * midiFrequency(standardOpenMidi[index]);
        const float linearMass = steelTensionNewtons[index]
            / (standardWaveSpeed * standardWaveSpeed);
        const float tension = voice.openMidi == standardOpenMidi[index]
            ? steelTensionNewtons[index]
            : std::max(linearMass * openWaveSpeed * openWaveSpeed, 1.0f);
        // Slide or bend, settled. A channel's pitch bend is a slide: the fretting
        // hand moves along the neck, the sounding length changes and the tension
        // does not. That is what the frequency and delay above have always done
        // and what the README documents, so it is left exactly as it was. An MPE
        // member channel's own bend is the other gesture - one finger pushing one
        // string across the fret, at a length the fret fixes - so it goes through
        // the string's tension: the frequency it asks for is still reached by the
        // delay, but the inharmonicity that goes as 1/T and the impedance the
        // junction reads move with the tension that would produce it. The
        // manager's zone-wide bend stays a slide, because a whole zone bending is
        // a hand moving rather than six fingers pushing. Grimes (see
        // bentStringTension) notes a lateral bend can only raise pitch; a
        // downward member bend is the release of a pre-bend, the same law run
        // backwards. The attack glide stays out of it: it is the model's own
        // few-cent tension transient (updateAttackPitch), already carried as a
        // pitch, and routing it here would modulate the junction port on every
        // note's attack, which no measurement asks for.
        const float tensionSemitones = memberBendSemitones + vibratoInterval;
        const float bentTension = tensionSemitones != 0.0f
            ? bentStringTension(tension, axialRigidity,
                                std::exp2(tensionSemitones / 12.0f))
            : tension;
        geometry.value = { unbentFrequency, soundingLength, speakingFret,
            contactPeriodSamples, frequency, lossDesignFrequency, linearMass,
            tension, tensionSemitones, bentTension };
        geometry.key = pitchKey;
        geometry.valid = true;
    }
    const auto& pitch = geometry.value;
    const float unbentFrequency = pitch.unbentFrequency;
    const float soundingLength = pitch.soundingLength;
    const float frequency = pitch.frequency;
    const float lossDesignFrequency = pitch.lossDesignFrequency;
    const float linearMass = pitch.linearMass;
    const float tension = pitch.tension;
    const float tensionSemitones = pitch.tensionSemitones;
    const float bentTension = pitch.bentTension;
    // These pre-configuration outputs are refreshed even when the original
    // full configuration key hits. No filter/reset/reference path is skipped.
    voice.speakingLengthMetres = soundingLength;
    voice.speakingFret = pitch.speakingFret;
    voice.contactPeriodSamples = pitch.contactPeriodSamples;
    // Every value below is a function of this key (VoiceConfigurationKey),
    // and configureVoice is the only writer of what it sets from here on, so
    // an unchanged key - an idle string, or a held one once its attack glide
    // has settled below a float step of its pitch - keeps what it has. Only
    // the two assignments that depend on arguments outside the key remain.
    // clearDelay always runs: it also resets the loops.
    const VoiceConfigurationKey configurationKey {
        voiceConfigurationGeneration_, stoppedMidi, voice.openMidi,
        exact::bits(frequency), exact::bits(tensionSemitones),
        exact::bits(parameters_.stringAge), exact::bits(palmMute_) };
    const bool usePickReference = (clearDelay || refreshPickReference)
        && sampleRate_ != 48000.0 && parameters_.picking == PickingTechnique::Pick;
    const bool referenceTuning = usePickReference
        && !(configurationKey == voice.referencePickConfigurationKey);
    if (!clearDelay && configurationKey == voice.configurationKey && !referenceTuning)
    {
        voice.midiNote = midiNote;
        if (voice.keyDown || voice.pedalHeld || voice.releaseJoinPending
            || voice.releaseAfterPluck || !voice.played)
            voice.releaseDamping = 1.0f;
        return;
    }
    // The E*I = E*(pi*d^4/64) solid-cylinder model on the fitted effective
    // bending diameter and stiffnessScale.
    const float stiffness = pi * pi * pi * steelYoungsModulus
        * bendingDiameter * bendingDiameter
        * bendingDiameter * bendingDiameter / 64.0f;
    const float inharmonicity = clamp(stiffness * physical.stiffnessScale
        / (bentTension * soundingLength * soundingLength), 0.0f, 0.004f);
    const float age = parameters_.stringAge;
    // Preserve the material/age law while allowing one shared fitted cutoff
    // scale to reduce excess upper-partial damping without changing the
    // fundamental T60 target below. With the fitted 2.29 the scaled cutoff
    // is past the 0.44 x 48 kHz clamp up to a String Age of about 0.24, so
    // at the default age it does nothing.
    // The plain strings' age cutoff falls as exp(-0.9 age) (it was -1.25):
    // at age 1 the high E above the 12th fret lost its upper partials four
    // times faster than the wound strings gained any loss at all. It sits
    // past the clamp below at the default age either way.
    const float cutoff = 12500.0f * std::exp(-0.9f * age)
        * physicalCalibration_.highLossCutoffScale;
    const float lowpassCoefficient = std::exp(
        -twoPi * clamp(cutoff, 1200.0f,
                       0.44f * 48000.0f)
        * (1.0f / 48000.0f));
    const float highLoss = clamp((0.035f
        + 0.42f * age
        + 0.018f * static_cast<float>(stringCount - 1 - stringIndex))
        * physical.frequencyLossScale, 0.0f, 0.95f);
    // The heel of the picking hand resting by the saddle is a soft lossy
    // absorber in parallel with the string's own loss, so the rates add:
    // 1/T60 = 1/T60_string + 1/T60_hand. The hand's mapped time and the 0.62
    // ratio by which it shortens the top relative to the fundamental are
    // Electry's calibrated bridge-hand endpoints, measured in what is now the
    // protocodus/virtual-instrument-electry repository. They transfer because
    // the absorber is the player's hand, not the instrument's
    // string set; they are not refitted here, since the reference corpus holds
    // no muted notes. Pressure scales the rate, so zero is an exact no-op
    // rather than a four-second floor.
    const float handRate = palmMute_ > 0.0f
        ? palmMute_ / std::exp(std::log(4.0f)
            + palmMute_ * (std::log(0.080f) - std::log(4.0f)))
        : 0.0f;
    // A soft contact damps the top faster than the fundamental. Adding exactly
    // the extra per-round-trip loss that a 0.62 high-to-fundamental T60 ratio
    // implies keeps the shelf's shape and leaves it untouched at zero pressure.
    // handRate is a 1/T60 rate, so a round trip of 1/f decays by 0.001^(R/f).
    const float mutedHighLoss = handRate > 0.0f
        ? clamp(1.0f - (1.0f - highLoss) * std::pow(0.001f,
              (1.0f / 0.62f - 1.0f) * handRate
              / std::max(unbentFrequency, 1.0f)), 0.0f, 0.95f)
        : highLoss;

    // This broad one-pole loss slope and its 72x scale are authored and
    // calibrated, not a per-string realization of Woodhouse's measured loss
    // table. DAFx-26 Eq. 25 prints seconds for eta_f, but the coefficient of
    // its angular-frequency damping term is dimensionless. That printed
    // unit does not justify treating the constants below as measured times.
    const float viscousLoss = 1.65e-4f * (1.0f + 1.35f * age);
    const float broadLoss = clamp(72.0f * viscousLoss
        * physical.frequencyLossScale, 0.0f, 0.95f);
    const float broadLossCutoff = 14.3f * frequency;
    const float broadLossCoefficient = std::exp(-twoPi
        * clamp(broadLossCutoff, 500.0f,
                0.44f * 48000.0f)
        * (1.0f / 48000.0f));
    // The expensive phase fit is useful across the fretboard and the
    // documented panel/Reason +/-12-semitone bend range. Beyond it, retain
    // a bounded fit and scale its stable allpasses as before, while the
    // inexpensive intrinsic-loss section still follows the actual pitch.
    // The fit also needs its three collocation partials inside its stated
    // 0.42 fs band (collocateDispersion), at every supported host rate.
    const float highestFitFrequency = std::min(
        midiFrequency(voice.openMidi + fretCount + 12),
        static_cast<float>(0.42 * sampleRate_
            / stretchedPartial(3.0, inharmonicity)));
    const float dispersionFitFrequency = std::min(lossDesignFrequency,
                                                   highestFitFrequency);
    const float designBroadLossCoefficient = std::exp(-twoPi
        * clamp(14.3f * dispersionFitFrequency, 500.0f,
                0.44f * 48000.0f)
        * (1.0f / 48000.0f));
    // Inside that supported band dispersion includes the bending section's
    // phase at the physical playing frequency. A 0.1% pitch change (1.73 cents)
    // invalidates the design at the same rate as the existing 0.2% B bound
    // for a tension bend; the tiny attack excursion still uses the scaled
    // allpass coefficients without rerunning the iterative solve. The hand
    // only adds loss and does not invalidate this intrinsic string design.
    const bool dispersionDesignChanged = clearDelay
        || exact::abs(voice.dispersionDesignFrequency - lossDesignFrequency)
               > std::max(1.0e-4f, 0.001f * lossDesignFrequency)
        // A bend or a vibrato moves B continuously, and this design is an
        // iterative solve, so B is only re-solved once it has moved 0.2%.
        // That relative tolerance applies to every caller, not only to a
        // moving bend: the one other continuous route into B is
        // physical.stiffnessScale, and a fitted move of it smaller than 0.2%
        // now leaves the allpass as designed until something else here
        // changes. 0.2% of B is 0.03 cents of H12 stretch on a low E, two
        // orders below the 3-cent tolerance the dispersion test holds, and
        // every discrete change that reaches here - fret, tuning, string set,
        // a calibration edit worth hearing - moves B by far more than that.
        || exact::abs(voice.dispersionDesignInharmonicity - inharmonicity)
               > std::max(1.0e-9f, 0.002f * inharmonicity)
        || exact::abs(voice.dispersionDesignAge - age) > 1.0e-5f
        || exact::abs(voice.dispersionDesignFrequencyLossScale
                    - physical.frequencyLossScale) > 1.0e-5f;
    // The string's own bending loss (bendingLossSection), for the physical
    // frequency and tension the dispersion is designed for. The four basses are the wound constructions
    // (steelBendingDiameter above says so).
    const bool wound = stringIndex <= 3;
    // A wound string goes dead first: grime and corrosion between its
    // windings are internal friction in its bending (the loss this section
    // models), where a plain string only dulls. The one-pole cutoff below
    // String Age acts in absolute frequency, so it reached a low E's
    // partials only above its 50th and aged a bass by 0.2 dB. The wound loss
    // now rises with age, pivoted on the default age so the shipped sound is
    // unchanged there: 0.2 of it on fresh strings, 7.8 times it at age 1.
    const float woundAgeing = std::max(0.2f, 1.0f + 8.0f * (age - 0.15f));
    const float bendingFactor = wound
        ? physicalCalibration_.steelWoundBendingLoss * woundAgeing
        : physicalCalibration_.steelPlainBendingLoss;
    // A dispersion design for these complete arguments, from the solves
    // already made when one matches exactly, otherwise solved and kept.
    const auto solvedDispersion = [this] (const std::array<double, 9>& arguments)
    {
        const auto solved = std::find_if(dispersionSolves_.begin(),
            dispersionSolves_.end(), [&arguments] (const DispersionSolve& solve)
            {
                return solve.valid && solve.arguments == arguments;
            });
        if (solved != dispersionSolves_.end())
            return std::pair { solved->decayRatios, solved->poleRatios };
        const auto calibration = calibrateDispersion(
            arguments[0], arguments[1], arguments[2], arguments[3],
            arguments[4], arguments[5], arguments[6], arguments[7], arguments[8]);
        auto& slot = dispersionSolves_[static_cast<std::size_t>(nextDispersionSolve_)];
        slot.arguments = arguments;
        for (std::size_t section = 0; section < 2; ++section)
        {
            slot.decayRatios[section] = static_cast<float>(calibration.decayRatio[section]);
            slot.poleRatios[section] = static_cast<float>(calibration.poleRatio[section]);
        }
        slot.valid = true;
        nextDispersionSolve_ = (nextDispersionSolve_ + 1)
            % static_cast<int>(dispersionSolves_.size());
        return std::pair { slot.decayRatios, slot.poleRatios };
    };
    if (dispersionDesignChanged)
    {
        const auto bending = bendingLossSection(
            static_cast<double>(bendingFactor),
            static_cast<double>(inharmonicity),
            static_cast<double>(lossDesignFrequency), sampleRate_);
        voice.bendingLossGain = static_cast<float>(bending.gain);
        voice.bendingLossA1 = static_cast<float>(bending.a1);
        voice.bendingLossA2 = static_cast<float>(bending.a2);
        const auto fitBending = dispersionFitFrequency == lossDesignFrequency
            ? bending : bendingLossSection(static_cast<double>(bendingFactor),
                static_cast<double>(inharmonicity),
                static_cast<double>(dispersionFitFrequency), sampleRate_);
        const std::array<double, 9> arguments {
            inharmonicity, dispersionFitFrequency, sampleRate_,
            designBroadLossCoefficient, broadLoss, lowpassCoefficient, highLoss,
            static_cast<float>(fitBending.a1), static_cast<float>(fitBending.a2)
        };
        // Resetting a wave does not change an otherwise identical design.
        // Keep the existing request tolerances and metadata updates, while
        // reusing only a solve with exactly the same complete input tuple.
        if (voice.dispersionDesignArguments != arguments)
        {
            const auto [decayRatios, poleRatios] = solvedDispersion(arguments);
            voice.dispersionDecayRatios = decayRatios;
            voice.dispersionPoleRatios = poleRatios;
            voice.dispersionDesignArguments = arguments;
        }
        voice.dispersionDesignFrequency = lossDesignFrequency;
        voice.dispersionDesignInharmonicity = inharmonicity;
        voice.dispersionDesignAge = age;
        voice.dispersionDesignFrequencyLossScale
            = physical.frequencyLossScale;
    }
    const float omega = twoPi * frequency * inverseSampleRate_;
    DispersionSections dispersion;
    for (std::size_t section = 0; section < 2; ++section)
    {
        secondOrderAllpassCoefficients(static_cast<double>(omega),
            voice.dispersionDecayRatios[section],
            voice.dispersionPoleRatios[section], dispersion.a1[section],
            dispersion.a2[section]);
    }
    dispersion.used[1] = voice.dispersionDecayRatios[1] > 0.0f;
    const float fretT60Factor = clamp(1.0f
        - physicalCalibration_.steelFretT60Slope * voice.speakingFret,
        0.10f, 2.0f);
    float fundamentalT60 = 5.4f
        * (1.0f - 0.12f * age)
        * fretT60Factor
        * physical.fundamentalT60Scale;
    if (handRate > 0.0f)
        fundamentalT60 = 1.0f / (1.0f / fundamentalT60 + handRate);
    const float rawDelay = static_cast<float>(tunedLoopDelay(
        frequency, sampleRate_, broadLossCoefficient, broadLoss,
        lowpassCoefficient, mutedHighLoss, dispersion,
        voice.bendingLossA1, voice.bendingLossA2));
    // The period this note is tuned to at 48 kHz, in 48 kHz samples, for
    // the Pick release's share solve (writePickRelease), which reads its
    // waves on that grid. The dispersion and bending sections are designed
    // per rate and their lag at the fundamental moves the tuned length by up
    // to half a 48 kHz sample (0.48 at MIDI 84 at 96 kHz), so the host
    // length rescaled lands on the other integer often, and the grid's
    // alignment with the release's sub-sample step then moves the solved
    // hump by up to 1.7 dB. The same design is run here at 48 kHz; the loss
    // shelves are already designed on the 48 kHz grid, and the bridge's lag
    // and the polarisation split are the host's rescaled. The period so
    // found is 48 kHz's own to 1.1e-3 samples (MIDI 40-100, both loops, at
    // 44.1, 88.2, 96 and 192 kHz). Only at a pluck, only for the pick.
    float referenceRawDelay = 0.0f;
    if (referenceTuning)
    {
        constexpr double referenceRate = 48000.0;
        const auto bending = bendingLossSection(
            static_cast<double>(bendingFactor),
            static_cast<double>(inharmonicity),
            static_cast<double>(lossDesignFrequency), referenceRate);
        const auto bendingA1 = static_cast<float>(bending.a1);
        const auto bendingA2 = static_cast<float>(bending.a2);
        const auto fitBending = dispersionFitFrequency == lossDesignFrequency
            ? bending : bendingLossSection(static_cast<double>(bendingFactor),
                static_cast<double>(inharmonicity),
                static_cast<double>(dispersionFitFrequency), referenceRate);
        const auto [decayRatios, poleRatios] = solvedDispersion({
            inharmonicity, dispersionFitFrequency, referenceRate,
            designBroadLossCoefficient, broadLoss, lowpassCoefficient, highLoss,
            static_cast<float>(fitBending.a1), static_cast<float>(fitBending.a2) });
        const float referenceOmega = twoPi * frequency
            * static_cast<float>(1.0 / referenceRate);
        DispersionSections referenceDispersion;
        for (std::size_t section = 0; section < 2; ++section)
            secondOrderAllpassCoefficients(static_cast<double>(referenceOmega),
                decayRatios[section], poleRatios[section],
                referenceDispersion.a1[section], referenceDispersion.a2[section]);
        referenceDispersion.used[1] = decayRatios[1] > 0.0f;
        referenceRawDelay = static_cast<float>(tunedLoopDelay(
            frequency, referenceRate, broadLossCoefficient, broadLoss,
            lowpassCoefficient, mutedHighLoss, referenceDispersion,
            bendingA1, bendingA2));
    }
    // The segment between saddle and anchor does not move when a string is
    // fretted and does not change tension, so its spring T/L is a constant of
    // the string rather than a fraction of the speaking length.
    const float bridgeTailStiffness = tension / std::max(
        physicalCalibration_.bridgeTailLengthMetres, 1.0e-5f);
    // Every string's port mobility reads all six anchors.
    const bool anchorTargetChanged = exact::bits(bridgeTailStiffness)
        != exact::bits(voice.bridgeTailStiffness);
    if (anchorTargetChanged)
        ++voiceConfigurationGeneration_;
    if (transitionRetune
        && anchorTargetChanged
        && bridgeTailStiffness != voice.appliedBridgeTailStiffness)
        voice.bridgeTailStiffnessSamples = std::max(1, static_cast<int>(
            std::ceil(voice.loops[0].currentDelay)));
    voice.bridgeTailStiffness = bridgeTailStiffness;
    if (voice.bridgeTailStiffnessSamples > 0)
    {
        if (anchorTargetChanged)
            voice.bridgeTailStiffnessStep = (bridgeTailStiffness
                - voice.appliedBridgeTailStiffness) / voice.bridgeTailStiffnessSamples;
    }
    else
        voice.appliedBridgeTailStiffness = bridgeTailStiffness;
    // The longitudinal wave speed is sqrt(E*A/mu) for a wound string, whose
    // axial load the core carries while the whole construction supplies the
    // mass; plain strings use the same expression with their own diameter.
    // Both come from the tables the transverse model already uses.
    {
        // The wound basses' axial load is carried by their published core.
        const float axialArea = 0.25f * pi * bendingDiameter * bendingDiameter;
        const float longitudinalSpeed = exact::sqrt(std::max(
            steelYoungsModulus * axialArea / std::max(linearMass, 1.0e-9f), 1.0f));
        const float longitudinal = clamp(
            longitudinalSpeed / (2.0f * soundingLength), 100.0f,
            0.45f * static_cast<float>(sampleRate_));
        for (int mode = 0; mode < Voice::longitudinalModeCount; ++mode)
        {
            const int harmonic = 2 * mode + 1;
            const float modeFrequency = std::min(
                longitudinal * static_cast<float>(harmonic),
                0.45f * static_cast<float>(sampleRate_));
            const float omegaLong = twoPi * modeFrequency * inverseSampleRate_;
            const float radius = std::exp(-omegaLong
                / (2.0f * std::max(physicalCalibration_.longitudinalQ, 1.0f)));
            voice.longitudinalA1[mode]
                = 2.0f * radius * std::cos(omegaLong);
            voice.longitudinalA2[mode] = -radius * radius;
            // Constant peak gain. Projecting the integrated extension drive
            // onto sin(n*pi*x/L) gives L*(1-(-1)^n)/(n*pi): even modes cancel
            // and the observable odd modes carry the fixed-fixed string's
            // 1/n participation. No modal weighting is fitted here.
            voice.longitudinalB0[mode] = (1.0f - radius * radius)
                * std::sin(omegaLong) / static_cast<float>(harmonic);
        }
        // DAFx-26's tension increase, in newtons, from the same displacement
        // scale the attack-pitch surrogate is calibrated with.
        const float displacement
            = physicalCalibration_.steelDisplacementScaleMetres;
        voice.longitudinalDrive = steelYoungsModulus * axialArea
            * displacement * displacement
            / (2.0f * soundingLength * soundingLength);
    }
    const auto bridgePort = bridgePortMobility(frequency, stringIndex);
    const float measuredBridgeDelay = bridgePhaseDelay(bridgePort, frequency,
                                                       stringIndex);
    const float desiredPeriodGain = std::pow(0.001f,
        1.0f / std::max(fundamentalT60 * frequency, 1.0f));
    const float unbentImpedance = stringImpedance(stringIndex, voice.openMidi);
    const float coupledDetune = coupledPolarisationDetune(
        bridgePort, unbentImpedance, unbentImpedance
            * exact::sqrt((bentTension / tension) / (1.0f
                + (bentTension - tension) / std::max(axialRigidity, 1.0f))),
        frequency, (rawDelay - measuredBridgeDelay)
            * physicalCalibration_.polarisationEndCorrectionMetres / soundingLength,
        desiredPeriodGain * 0.9995f, desiredPeriodGain * 0.9988f);
    voice.polarisationDetune = coupledDetune;
    const float lossOmega = static_cast<float>(referenceLossOmega(omega, sampleRate_));
    const float filterGain = magnitudeForOnePoleMix(
        broadLossCoefficient, broadLoss, lossOmega)
        * magnitudeForOnePoleMix(lowpassCoefficient, mutedHighLoss, lossOmega)
        * static_cast<float>(bendingLossMagnitude(voice.bendingLossGain,
            voice.bendingLossA1, voice.bendingLossA2, omega));
    const float loopGain = desiredPeriodGain / std::max(filterGain, 0.50f);

    for (int polarisation = 0; polarisation < 2; ++polarisation)
    {
        auto& loop = voice.loops[static_cast<std::size_t>(polarisation)];
        // The pair is split by an end correction, not by the body: see
        // polarisationEndCorrectionMetres in FittedPhysicalData.h for the
        // measurement and its bound. The whole difference lengthens the
        // parallel loop, so the normal one is the higher member as Woodhouse
        // measures it, and the normal loop keeps exactly the sounding length
        // that the tuning, the fret compensation and the bridge phase delay
        // are all built on. The previous split was an authored -0.32 / +0.41
        // cents with the opposite sign and a third of the measured size.
        const float endCorrection = polarisation == 0 ? 0.0f
            : physicalCalibration_.polarisationEndCorrectionMetres;
        // Both polarisations are one string, one length and one tension. The
        // normal loop is tuned so that, loaded by the bridge, it sounds the
        // requested pitch - a player tunes the note that radiates - and the
        // parallel one shares that bare length, lengthened by the end
        // correction; how far the bridge pulls the normal member away from
        // it is the doublet's width, which therefore varies note to note as
        // the bridge's phase does.
#if defined(ACUSTRA_ANALYSIS_BENDING_LOSS_NORMAL_ONLY)
        // The analysis-only parallel loop carries no section, so it is tuned
        // without the section's lag.
        const float planeDelay = polarisation == 0 ? rawDelay
            : static_cast<float>(tunedLoopDelay(frequency, sampleRate_,
                broadLossCoefficient, broadLoss, lowpassCoefficient,
                mutedHighLoss, dispersion));
#else
        const float planeDelay = rawDelay;
#endif
        const float polarisationDelay = coupledDetune != 0.0f
            ? (planeDelay - measuredBridgeDelay) * (1.0f + coupledDetune)
            : planeDelay - measuredBridgeDelay;
        loop.targetDelay = clamp(
            polarisationDelay * (1.0f + endCorrection / soundingLength),
            3.0f, static_cast<float>(maximumDelaySamples - 3));
        if (clearDelay)
            loop.currentDelay = loop.targetDelay;
        if (referenceTuning)
        {
            const float referenceBridgeDelay = measuredBridgeDelay
                * static_cast<float>(48000.0 / sampleRate_);
            const float referenceDelay = coupledDetune != 0.0f
                ? (referenceRawDelay - referenceBridgeDelay) * (1.0f + coupledDetune)
                : referenceRawDelay - referenceBridgeDelay;
            voice.referencePickDelay[static_cast<std::size_t>(polarisation)] = clamp(
                referenceDelay * (1.0f + endCorrection / soundingLength),
                3.0f, static_cast<float>(maximumDelaySamples - 3));
        }
        else if (clearDelay && !usePickReference)
            voice.referencePickDelay[static_cast<std::size_t>(polarisation)] = 0.0f;
        // Authored per-plane factors on top of the requested fundamental
        // T60: each round trip loses 0.05% (normal) or 0.12% (parallel) more
        // at every frequency, and the parallel plane's shelves are 6% and 8%
        // deeper, so the planes do not decay as one. A loss per round trip is
        // the constant-Q friction term the string-loss note above sets
        // aside; on the longest-ringing notes it leaves the realised T60 up
        // to about half the requested one (audit, 2026-09-30).
        if (clearDelay)
            loop.loopGainTransitionSamples = 0;
        loop.setLoopGain(clamp(loopGain
            * (polarisation == 0 ? 0.9995f : 0.9988f), 0.70f, 0.999995f),
            transitionRetune && !clearDelay);
        loop.broadLossMix = clamp(broadLoss
            * (polarisation == 0 ? 1.0f : 1.06f), 0.0f, 1.0f);
        loop.highLossMix = clamp(mutedHighLoss
            * (polarisation == 0 ? 1.0f : 1.08f), 0.0f, 1.0f);
        loop.broadLossCoefficient = broadLossCoefficient;
        loop.lowpassCoefficient = lowpassCoefficient;
        loop.broadLossFilter.configureRate(broadLossCoefficient, sampleRate_);
        loop.lossFilter.configureRate(lowpassCoefficient, sampleRate_);
        // Intrinsic to the string, so both planes lose it alike.
        const bool wasBending = loop.bendingLossActive;
        loop.bendingLossActive = voice.bendingLossA1 != 0.0f
                              || voice.bendingLossA2 != 0.0f;
        // A section switched on under a sounding wave - a string set with a
        // loss exchanged for one without - starts from the wave it meets,
        // not from rest, which would drop the loop's output to g x for a few
        // samples and click (StringLoop::advance).
        if (loop.bendingLossActive && !wasBending)
            loop.bendingLossSeed = true;
#if defined(ACUSTRA_ANALYSIS_BENDING_LOSS_NORMAL_ONLY)
        // Analysis only: the same loss confined to the plane normal to the
        // top, as a bridge-side loss would be. Not the intrinsic mechanism.
        if (polarisation != 0)
            loop.bendingLossActive = false;
#endif
        if (clearDelay || !performanceRealism_.retuneContinuity)
            loop.intrinsicCoefficientSamples = 0;
        loop.setIntrinsicCoefficients({ voice.bendingLossGain,
            voice.bendingLossA1, voice.bendingLossA2,
            static_cast<float>(dispersion.a1[0]), static_cast<float>(dispersion.a2[0]),
            static_cast<float>(dispersion.a1[1]), static_cast<float>(dispersion.a2[1]) },
            performanceRealism_.retuneContinuity && transitionRetune && !clearDelay);
        if (clearDelay)
            loop.secondDispersionActive = dispersion.used[1];
        else
            loop.switchSecondDispersion(dispersion.used[1]);
        if (clearDelay)
            loop.reset();
    }

    if (referenceTuning)
        voice.referencePickConfigurationKey = configurationKey;
    else if (clearDelay && !usePickReference)
        voice.referencePickConfigurationKey.generation = 0;

    voice.midiNote = midiNote;
    voice.fret = fret;
    voice.characteristicImpedance = stringImpedance(
        stringIndex, voice.openMidi);
    // Z = sqrt(T*mu) with Grimes' stretched mass per length is Z0 times the
    // frequency ratio the same tension produces - 12.3% for a whole tone -
    // and saturates with the tension where his law stops. The junction reads
    // characteristicImpedance times the applied scale; a tuning switched
    // under a ringing chord slews it too, from the old string's impedance
    // (applyDiscreteParameters), while a string set exchanged steps it.
    const float stretchedMass = 1.0f
        + (bentTension - tension) / std::max(axialRigidity, 1.0f);
    voice.tensionNewtons = bentTension;
    voice.bendImpedanceScale = exact::sqrt((bentTension / tension)
                                         / stretchedMass);
    if (clearDelay)
        voice.appliedBendImpedanceScale = voice.bendImpedanceScale;
    if (voice.keyDown || voice.pedalHeld || voice.releaseJoinPending
        || voice.releaseAfterPluck || !voice.played)
        voice.releaseDamping = 1.0f;
    else if (voice.releaseSeconds > 0.0f)
    {
        // A released string that slides on keeps its hand's T60 in time:
        // the loss per trip follows the loop's new period.
        voice.releaseDamping = handDamping(voice.releaseSeconds,
                                           loopFundamental(voice));
    }
    voice.configurationKey = configurationKey;
}

AcustraEngine::VoiceBend AcustraEngine::voiceBend(
    const Voice& voice) const noexcept
{
    // A channel's wheel is a slide; an MPE member adds its own bend, frozen
    // at key-up, to the manager's zone-wide one (see configureVoice).
    VoiceBend bend {};
    if (!voice.played)
        return bend;
    const auto channel = static_cast<std::size_t>(voice.midiChannel - 1);
    bend.performed = pitchBendSemitones_[channel];
    if (voice.mpeMember)
    {
        bend.member = voice.memberPitchBendFrozen
            ? voice.frozenMemberPitchBendSemitones
            : pitchBendSemitones_[channel];
        bend.performed = pitchBendSemitones_[0] + bend.member;
    }
    return bend;
}

// The fundamental the string's loop runs at, which is what a loss applied
// once per trip round it has to be timed by: a natural harmonic's loop is
// its open string's (configureVoice), and a slid or bent note's is at the
// bent pitch, bounded as the loop's own frequency is. The attack glide and
// the wheel's vibrato, a few cents, stay out of it. Unbent, it is exactly
// the note's own frequency.
float AcustraEngine::loopFundamental(const Voice& voice) const noexcept
{
    const float unbent = midiFrequency(
        voice.harmonic > 1 ? voice.openMidi : voice.midiNote);
    const float bend = voiceBend(voice).performed;
    if (bend == 0.0f)
        return unbent;
    return clamp(unbent * std::exp2(clamp(bend, -192.0f, 192.0f) / 12.0f),
                 static_cast<float>(sampleRate_) / (maximumDelaySamples - 3.0f),
                 0.24f * static_cast<float>(sampleRate_));
}

// The loss per trip round a loop at this fundamental that gives this T60.
float AcustraEngine::handDamping(float t60Seconds, float fundamental) noexcept
{
    return std::pow(0.001f, 1.0f / std::max(t60Seconds * fundamental, 1.0f));
}

void AcustraEngine::updateAttackPitch(Voice& voice, int stringIndex) noexcept
{
    if (!voice.played || !(voice.attackSlopeEnergy > 0.0f)
        || !exact::isfinite(voice.attackSlopeEnergy))
    {
        voice.attackPitchCents = 0.0f;
        return;
    }

    const auto& physical = physicalCalibration_.steel;
    const auto index = static_cast<std::size_t>(stringIndex);
    constexpr float scaleLength = 0.648f;
    const float soundingLength = voice.speakingLengthMetres;
    // Wound axial rigidity uses the same effective core proxy as bending;
    // treating its outside diameter as solid 200 GPa steel is less physical.
    const float diameter = steelBendingDiameter[index];
    constexpr float youngsModulus = steelYoungsModulus;
    const float area = 0.25f * pi * diameter * diameter;
    const float secondMoment = pi * diameter * diameter * diameter * diameter
                             / 64.0f;
    const float openWaveSpeed = 2.0f * scaleLength
                              * midiFrequency(voice.openMidi);
    const float tension = voice.tensionNewtons > 0.0f
        ? voice.tensionNewtons : voice.characteristicImpedance * openWaveSpeed;
    const float lengthSquared = soundingLength * soundingLength;
    const float displacement = physicalCalibration_.steelDisplacementScaleMetres;
    const float tensionIncrease = youngsModulus * area
        * displacement * displacement * voice.attackSlopeEnergy
        / (2.0f * lengthSquared);
    const float bendingStiffness = pi * pi * youngsModulus * secondMoment
        * physical.stiffnessScale / lengthSquared;
    const float ratio = tensionIncrease / (tension + bendingStiffness);
    const float cents = 1200.0f * std::log2(exact::sqrt(1.0f + ratio));
    voice.attackPitchCents = exact::isfinite(cents)
        ? clamp(cents, 0.0f, 20.0f) : 0.0f;
}

// The modulation wheel's vibrato. Grimes' Sec. 0.3 and Erkut et al. (AES
// 108th Conv. 2000, preprint 5114, Sec. 3.3) describe the same gesture: the
// classical player's vibrato is axial, the fretting hand modulating tension
// at a fixed bend angle, so it belongs on the tension route and not on the
// fret - and what a tension modulation is heard as is a modulation of the
// fundamental (Erkut: the string is repeatedly stretched to fluctuate the
// fundamental frequency), which is why configureVoice puts the interval this
// returns into the performed pitch as well as into the tension. What those
// measurements fix: the lowest frequency during a vibrato is the note's own
// unmodulated one (Erkut), so the modulation is one-sided upward and starts
// from the note's pitch; the depth converges over a transient of about 0.5 s
// from the moment the player begins it (Erkut's tt); the fitted rates are
// 1.4 Hz slow and 4.9 Hz fast (Erkut), the fast one just under Laurson et
// al.'s 5-6 Hz for the same instrument (CMJ 25(3):38-49, 2001), and the fast
// vibrato's depth is systematically the smaller, so the wheel runs from the
// shallow fast end to the deep slow one rather than raising both; and an
// open string gets none, because no finger is stopping it
// (Laurson's max-depth is zero at fret zero). What no source fixes is the
// wheel's top in cents - Erkut reports the depth varying with string and fret
// without a figure, Laurson scales it by an integer 1 to 9 - so that endpoint
// is authored, and set to the same 20 cents the attack-pitch surrogate is
// bounded at, which keeps a new magnitude out of the model.
float AcustraEngine::vibratoSemitones(const Voice& voice,
                                      int fret) const noexcept
{
    if (!(vibrato_ > 0.0f) || !voice.played || !voice.keyDown
        || voice.harmonic > 1 || fret <= 0)
        return 0.0f;
    constexpr float fullWheelSemitones = 0.20f;
    // MPE channel pressure biases how deep this one note's vibrato reaches,
    // a firmer grip letting more of the wheel's own travel through; it never
    // raises the wheel's own 20-cent ceiling above, only trims it down for a
    // lighter grip, so no new pitch magnitude enters the model. A pressure
    // that was never sent leaves the factor at 1, exactly as before.
    constexpr float pressureDepthFloor = 0.5f;
    const float pressure = mpePressureFor(voice);
    const float depthBias = pressure >= 0.0f
        ? pressureDepthFloor + (1.0f - pressureDepthFloor) * pressure
        : 1.0f;
    return fullWheelSemitones * depthBias * vibrato_ * vibratoOnset_
        * 0.5f * (1.0f - std::cos(vibratoPhase_));
}

float AcustraEngine::effectiveTouch(float velocity) const noexcept
{
    const auto& physical = physicalCalibration_.steel;
    const float touch = clamp(parameters_.touch + physical.velocityBrightnessDepth
        * (velocity - 0.5f), 0.0f, 1.0f);
    // Touch and velocity keep one displacement/noise law for all techniques.
    // Picking styles change the contact footprint and panel-based position
    // in initialisePluck; their distinction must survive saturated touch.
    return touch;
}

// -1: no lower zone, off a member channel, or no channel-pressure message
// received for this note yet -- every caller must treat that as "apply no
// bias", not as a pressure of zero.
float AcustraEngine::mpePressureFor(const Voice& voice) const noexcept
{
    if (!voice.mpeMember || voice.midiChannel < 1
        || voice.midiChannel > midiChannelCount)
        return -1.0f;
    return mpePressure_[static_cast<std::size_t>(voice.midiChannel - 1)];
}

namespace
{
float xorshiftNoise(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    state = state == 0 ? 0x6d2b79f5u : state;
    return static_cast<float>(static_cast<std::int32_t>(state))
         / static_cast<float>(std::numeric_limits<std::int32_t>::max());
}
} // namespace

float AcustraEngine::nextNoise(Voice& voice) noexcept
{
    return xorshiftNoise(voice.randomState);
}

AcustraEngine::PickingGesture AcustraEngine::nextPickingGesture() noexcept
{
    if (!performanceRealism_.coherentHand)
        return {};

    const bool rest = !pickingGestureSeen_
        || sampleClock_ < pickingGestureSample_
        || sampleClock_ - pickingGestureSample_
            > static_cast<std::uint64_t>(handMemorySeconds * sampleRate_);
    if (rest)
    {
        // The first contact keeps the previous model's independent draw.
        // Establish a hand posture for the following contacts without taking
        // anything away from the player's first note or explicit accent.
        pickingGesture_.position = xorshiftNoise(pickingGestureRandom_);
        pickingGesture_.pressure = xorshiftNoise(pickingGestureRandom_);
        pickingGesture_.active = false;
        pickingGestureSeen_ = true;
    }
    else if (sampleClock_ != pickingGestureSample_)
    {
        // Bounded diffusion with reflection, not a periodic modulation.
        // Symmetric reflected increments preserve a uniform marginal over
        // [-1,1]; elapsed seconds set movement, never host block size. The
        // 0.70/sqrt(s) mobility is an authored small-gesture map, not a
        // measured universal statistic of a player's hand.
        const float elapsed = static_cast<float>(
            static_cast<double>(sampleClock_ - pickingGestureSample_)
            / sampleRate_);
        const float step = 0.70f * std::sqrt(std::min(elapsed, 1.0f));
        const auto reflect = [] (float value) noexcept
        {
            return value > 1.0f ? 2.0f - value
                 : value < -1.0f ? -2.0f - value : value;
        };
        pickingGesture_.position = reflect(pickingGesture_.position
            + step * xorshiftNoise(pickingGestureRandom_));
        pickingGesture_.pressure = reflect(pickingGesture_.pressure
            + step * xorshiftNoise(pickingGestureRandom_));
        pickingGesture_.active = true;
    }
    pickingGestureSample_ = sampleClock_;
    return pickingGesture_;
}

void AcustraEngine::beginStrum(int strokeSpanSamples,
                              int repeatIntervalSamples,
                              bool upstroke) noexcept
{
    strumParallelSign_ = upstroke ? -1.0f : 1.0f;
    strumGesture_ = nextPickingGesture();
    // A stroke's own pick speed varies stroke to stroke (GuitarSet's
    // comping tracks, Tools/MeasureStrums.py -- see strumDelaySamples and
    // noteOn's strumMember path for the measured figures this scale is
    // fitted to). Drawn from the engine's own generator, not any one
    // voice's, so it is one shared value applied to every string of this
    // stroke regardless of which voice noteOn happens to land it on.
    const float speedDraw = xorshiftNoise(strumRandomState_);
    // A firmer shared stroke is faster. Its residual timing still varies;
    // the convex blend never exceeds the existing traversal bounds.
    const float speedVariation = strumGesture_.active
        ? 0.80f * speedDraw - 0.20f * strumGesture_.pressure : speedDraw;
    strumSpeedScale_ = 1.0f + strumSpeedJitterHalfWidth * speedVariation;
    // A return stroke cannot repeatedly replace the far strings' queued
    // attacks before reaching them. Use the last actual inter-stroke
    // interval as a causal rhythm estimate, applying one scale to the whole
    // traversal so physical order and skipped-string distance survive.
    // This timing constraint is separate from the GuitarSet speed fit; it
    // introduces no gain, independent jitter, or host lookahead/latency.
    if (strokeSpanSamples > 0 && repeatIntervalSamples > 0)
        strumSpeedScale_ = std::min(strumSpeedScale_,
            static_cast<float>(repeatIntervalSamples - 1)
                / static_cast<float>(strokeSpanSamples));
}

void AcustraEngine::initialisePluck(Voice& voice, int stringIndex,
                                    float velocity, bool merge) noexcept
{
    const float v = clamp(velocity, 0.001f, 1.0f);
    voice.repluckForceGain = 1.0f;
    voice.excitationParallelGain = 0.51f * voice.pluckParallelSign;
    const auto& physical = physicalCalibration_.steel;
    const float touch = effectiveTouch(voice.velocity);
    if (!voice.strumming)
        voice.pluckGesture = nextPickingGesture();
    const auto gesture = voice.pluckGesture;
    constexpr float scaleLength = 0.648f;
    // The fretting hand may already have slid when this stroke arrives.
    // configureVoice supplies the same physical length as the tuned string;
    // a member's tension bend keeps it fixed, and a harmonic uses the full
    // open string rather than its requested harmonic pitch.
    const float soundingLength = voice.speakingLengthMetres;
    // A player's hand stays at an approximately fixed physical distance from
    // the bridge.  Express that distance relative to each fretted string only
    // when constructing its initial condition.
    // Authored playing styles: a bridgeward narrow Pick, calibrated Finger,
    // and neckward broad Thumb. Finite width and pluck position change the
    // excited modes (Traube/Smith, DAFx00); these ratios are not measured
    // dimensions or universal laws of a plectrum/finger. The panel remains
    // the base hand position; explicit MPE position below takes precedence.
    const float pickingDistance = parameters_.picking == PickingTechnique::Pick
        ? 0.40f : parameters_.picking == PickingTechnique::Thumb ? 1.95f : 1.0f;
    const float distanceFromBridge = (0.045f
        + 0.135f * parameters_.pluckPosition) * physical.pluckDistanceScale
        * pickingDistance;
    // No two plucks land in the same place. The archtop's three takes of each
    // note put their pluck points a median 0.02 of the string length apart
    // (Traube-Smith comb on the soft rows, where the estimate is clean),
    // which for three draws from a uniform spread is the spread's half-width;
    // each pluck draws its own offset within it.
    const float positionDraw = nextNoise(voice);
    // MPE Timbre, CC74, on this note's own member channel says directly
    // where the string was met (0-1 across the same 0.05-0.46 band the
    // panel control reaches), in place of the panel's one hand position for
    // every string. -1 is "no CC74 received this note", the panel's own
    // distance-from-bridge law applies unchanged, and this is the only path
    // reachable without a lower zone.
    const std::size_t channelIndex
        = static_cast<std::size_t>(voice.midiChannel - 1);
    const bool hasTimbre = voice.mpeMember && voice.midiChannel >= 1
        && voice.midiChannel <= midiChannelCount
        && mpeTimbre_[channelIndex] >= 0.0f;
    const float takeOffset = 0.02f * (gesture.active && !hasTimbre
        ? 0.70f * gesture.position + 0.30f * positionDraw : positionDraw);
    float basePosition = hasTimbre
        ? 0.05f + 0.41f * mpeTimbre_[channelIndex]
        : distanceFromBridge / soundingLength;
    // A hand held at its distance from the bridge meets a short (stopped
    // high on the neck) string ever further toward its middle, and a Thumb
    // meets even the open strings there. No player lets a note sound from
    // its own midpoint, where every even partial is nulled: the flat-top
    // recordings' plain-string H2 stands 7 dB under H1 where renders plucked
    // at the old band edge 0.46 stood 20 under. So past a quarter of the
    // string the point bends smoothly toward 0.36 and never reaches it,
    // p' = h - w^2 / (p - k + w), w = h - k: the same point and slope at the
    // knee k = 0.25 and still rising wherever the hand moves. The hard
    // 0.46 clamp this replaces, and a fold about the midpoint with a
    // quarter-string floor, each left Pluck Position without effect on
    // whole frets (the clamp a Thumb from the 11th fret up, the fold a
    // Thumb at the 20th); the knee beat both, and a hard bound at 0.36, on
    // the benchmark (Docs/decisions.md, 2026-09-30). CC74 still names any
    // point of its published 0.05-0.46 band directly.
    constexpr float lowestPoint = 0.05f;
    constexpr float kneePoint = 0.25f;
    constexpr float handLimit = 0.36f;
    if (!hasTimbre && basePosition > kneePoint)
    {
        constexpr float width = handLimit - kneePoint;
        basePosition = handLimit
            - width * width / (basePosition - kneePoint + width);
    }
    // Past the band's edge the take's draw is reflected back inside it, so
    // a pluck near the edge still varies from take to take. Clamping the
    // drawn point instead put every such pluck on exactly the edge.
    const float highestPoint = hasTimbre ? 0.46f : handLimit;
    float position = clamp(basePosition, lowestPoint, highestPoint) + takeOffset;
    if (position > highestPoint)
        position = 2.0f * highestPoint - position;
    else if (position < lowestPoint)
        position = 2.0f * lowestPoint - position;
    voice.pluckPoint = position;
    voice.repluckContactPending = false;
    // Freeze the two transport paths at contact. D=2L/c, x=pL, hence the
    // direct arrival is pD/2 and the nut-reflected arrival is (1-p/2)D.
    // The source's two polarisations meet the same physical pluck point.
    // This held-note experiment excludes natural-harmonic touches.
    voice.contactTravelEnabled = voice.harmonic <= 1;
    if (voice.contactTravelEnabled)
        voice.contactTravel.reset(0.5f * position * voice.contactPeriodSamples,
            (1.0f - 0.5f * position) * voice.contactPeriodSamples);
    // Velocity response has two bounded parts: touch brightens with velocity,
    // while the displacement exponent moves from the legacy 1.32 toward the
    // reference-response 0.82 as the same fitted depth rises.
    const float velocityExponent = 1.32f
        - 0.50f * physical.velocityBrightnessDepth;
    // A strummed string's own level varies stroke to stroke too, at the
    // one nominal velocity a strum's mean gives every string: GuitarSet's
    // comping tracks put the pooled deviation of a repeated string's own
    // level, across 25 runs of >=3 repeats of one chord and direction, at
    // a 4.47 dB standard deviation, matched here by uniform jitter at a
    // half-width of std*sqrt(3). A single note never sets voice.strumming,
    // so it never draws this and stays exactly as it was.
    float strumLevelGain = 1.0f;
    if (voice.strumming)
    {
        const float forceDraw = nextNoise(voice);
        // One hand drives the chord, with smaller string-to-string contact
        // differences. The same bounds retain headroom; a convex blend
        // reduces the old marginal spread as it adds shared motion. The
        // new distribution must be checked in phrase-level calibration.
        const float forceVariation = gesture.active
            ? 0.35f * gesture.pressure + 0.65f * forceDraw : forceDraw;
        strumLevelGain = std::pow(10.0f, 7.74f * forceVariation / 20.0f);
    }
    // Repeated single-note sequencer strokes deserve modest force variety
    // too, below the spread of the real Eastman same-pitch/style takes:
    // their 20-250 ms levels have median within-group SD 1.09 dB (Finger)
    // and 1.47 dB (Pick). Their intended force/string/fret is unknown, so
    // +/-1 dB is a conservative authored listening candidate, not a fitted
    // universal law. The strum already has its own measured variation.
    // A first pluck, a changed MIDI velocity (including explicit accents),
    // a new note/channel, a duplicate on one sample, or a rest beyond the
    // existing hand-memory window keeps the exact nominal force. The
    // independent draw is linear-mean normalised: E[exp(a*U)] = sinh(a)/a.
    voice.repeatedPluckGain = 1.0f;
    const bool recentRepeat = !voice.strumming
        && voice.midiNote == voice.lastPluckMidiNote
        && voice.midiChannel == voice.lastPluckMidiChannel
        && v == voice.lastPluckVelocity
        && sampleClock_ > voice.lastPluckSample
        && sampleClock_ - voice.lastPluckSample <= static_cast<std::uint64_t>(
            handMemorySeconds * sampleRate_);
    if (recentRepeat)
    {
        constexpr double halfWidth = 0.1151292546497022842; // ln(10)/20: 1 dB
        voice.repeatedPluckGain = static_cast<float>(std::exp(halfWidth
            * xorshiftNoise(voice.repeatPluckState)) * halfWidth
            / std::sinh(halfWidth));
    }
    voice.lastPluckMidiNote = voice.midiNote;
    voice.lastPluckMidiChannel = voice.midiChannel;
    voice.lastPluckVelocity = v;
    voice.lastPluckSample = sampleClock_;
    // A hand lets the string go at the force it can hold, not at a set
    // displacement: a point force F at distance a from the bridge deflects a
    // string of tension T and speaking length L by F a (L - a) / (T L), so the
    // same stroke displaces a stopped string less than the open one it was
    // calibrated on, by (L - a)/L against (L0 - a)/L0 at the same hand
    // position.
    const float heldDistance = position * soundingLength;
    // Downward wheels can ask the finite model for a virtual string longer
    // than its open length. Refer that case to its own open length: the
    // ordinary fixed-force shortening law remains exact, while a hostile
    // wheel cannot turn the minimum contact-position bound into a gain.
    const float releaseScale = soundingLength <= scaleLength
        ? (1.0f - position)
            / clamp(1.0f - heldDistance / scaleLength, 0.05f, 1.0f)
        : 1.0f;
    const float amplitude = 0.24f
        * std::pow(v, velocityExponent) * (0.92f + 0.08f * touch)
        * strumLevelGain * voice.repeatedPluckGain;
    // The contact noise below follows the force; the shape it leaves, the
    // displacement.
    const float releasedAmplitude = amplitude * releaseScale;
    // The share of the pluck's energy released normal to the soundboard. A
    // steel-string player's finger stroke and pick both cross the strings
    // moving along the top, pressing in only partly, so most of a steel pluck
    // is parallel to it - which radiates only through the rocking saddle,
    // quieter and longer (saddleHeightRatio). When only the normal plane
    // reached the body the share sat at 0.91 - 0.08 Touch. With both planes
    // radiating, a sweep over the benchmark improves every steel split down
    // to about 0.25 at the default Touch, a release about 60 degrees from the
    // normal (0.86 -> 0.25: training -1.10%, development validation -2.78%,
    // flat-top -5.13%), and a blind listener preferred it on both steel pairs
    // of the 2026-09-24 set (Docs/decisions.md). It is a selected share, not
    // a measured angle; a higher Touch turns it further along the top, and
    // each pluck draws its own.
    const float angleDraw = nextNoise(voice);
    const bool pressureOwned = voice.mpeMember && mpePressureFor(voice) >= 0.0f;
    const float randomAngle = 0.025f * (gesture.active && !pressureOwned
        ? 0.65f * gesture.pressure + 0.35f * angleDraw : angleDraw);
    voice.polarisationMix = pluckNormalShare(touch, randomAngle);
    // The shared register law pivots at one fixed 48 kHz MIDI-61 period,
    // independent of material, string choice and host sample rate.
    const float apertureReferenceDelay = 48000.0f / midiFrequency(61);
    // The Pick technique's release velocity (FittedPhysicalData.h). Finger
    // and Thumb, and a pick at a zero share, take the legacy shape below.
    const bool pick = parameters_.picking == PickingTechnique::Pick;
    const float releaseShare = pick
        ? physicalCalibration_.pickReleaseVelocityShare
            * std::pow(v, physicalCalibration_.pickReleaseVelocityExponent)
        : 0.0f;
    // Touch is how fast the hand lets go: a firm touch releases the string
    // over a smaller effective edge, a soft one over a larger, two octaves
    // of radius across the control and none at its default.
    const float releaseTouch = std::exp2(2.0f * (0.58f - parameters_.touch));
    // A firmer contact is slightly less compliant. This small release-radius
    // change shares the gesture's angle/force state, not a new Touch draw.
    // Explicit member pressure remains the player's own contact expression.
    const float gestureCompliance = gesture.active && !pressureOwned
        ? 1.0f - 0.06f * gesture.pressure : 1.0f;
    double slipPole = 0.0;
    // The held force unloads in r/u, u the speed its own displacement gives
    // the string (plectrumSlipPole), so softer strokes release more slowly.
    // Finger takes that slip in full, including at full velocity: cancelling
    // its nominal slip made sustained H5-H12/H1-H4 7-10 dB brighter than
    // the identified Eastman Finger recordings. Full slip brings that balance
    // about 2.3 dB closer on both measured-body presets, with a documented
    // Martin spectral tradeoff (Docs/decisions.md, 2026-10-07). Thumb retains
    // its selected ratio to the full-velocity slip. Both keep the released
    // line's spread about its mean, preserving the displacement-to-level law.
    // The existing 0.2 mm effective radius is authored, not a newly measured
    // fingertip dimension. The nominal contact still bounds a firm Touch to
    // three times its release speed; no velocity law is fitted to Finger
    // recordings with unknown dynamics.
    constexpr float fingerReleaseRadius = 0.2e-3f;
    constexpr float releaseReferenceVelocity = 1.0f;
    constexpr double releaseBoostLimit = 3.0;
    double unslipPole = 0.0;
    voice.releaseSlipPole = 0.0;
    voice.releaseReferencePole = 0.0;
    if (pick)
        slipPole = plectrumSlipPole(voice, releasedAmplitude, heldDistance,
            soundingLength, scaleLength,
            physicalCalibration_.pickEdgeRadiusMetres * releaseTouch
                * gestureCompliance);
    else
    {
        // The nominal full-velocity contact bounds release speed. Thumb
        // also divides out its slip. Keep that reference independent of the
        // stroke's force draw, so weak and firm same-velocity strokes retain
        // their different actual slips. A non-strummed pluck divides by one.
        const double referencePole = plectrumSlipPole(voice, releasedAmplitude
                / (strumLevelGain * voice.repeatedPluckGain)
                * std::pow(releaseReferenceVelocity / v, velocityExponent),
            heldDistance, soundingLength, scaleLength, fingerReleaseRadius);
        const double pole = plectrumSlipPole(voice, releasedAmplitude,
            heldDistance, soundingLength, scaleLength,
            fingerReleaseRadius * releaseTouch * gestureCompliance);
        if (referencePole > 0.0 && pole > 0.0)
        {
            const double referenceTau = -1.0 / std::log(referencePole);
            const double tau = std::max(-1.0 / std::log(pole),
                                        referenceTau / releaseBoostLimit);
            const double bounded = std::exp(-1.0 / tau);
            if (bounded != referencePole
                || parameters_.picking == PickingTechnique::Finger)
            {
                slipPole = bounded;
                unslipPole = referencePole;
            }
        }
        voice.releaseSlipPole = unslipPole > 0.0 ? slipPole : 0.0;
        voice.releaseReferencePole = parameters_.picking == PickingTechnique::Finger
            ? 0.0 : unslipPole;
    }

    // The finger's contact width over the tool's, for the contact noise's
    // corner below (a thumb's broader pad slides off more slowly).
    float contactWidthRatio = 1.0f;
    // The caller has already retained any preceding wave. This full-period
    // triangle initializes a fresh pluck, but its time origin is not the
    // zero-velocity release: before smoothing it is phase-equivalent
    // to a rest pluck, with the same modal magnitudes. Unlike the fretting
    // primitives below, it is not combined with a prescribed velocity state.
    for (int polarisation = 0; polarisation < 2; ++polarisation)
    {
        auto& loop = voice.loops[static_cast<std::size_t>(polarisation)];
        loop.reset();
        loop.currentDelay = loop.targetDelay;
        const int length = std::clamp(
            static_cast<int>(std::round(loop.targetDelay)), 8,
            maximumDelaySamples - 3);
        // The two planes are released a little apart (-0.006 and +0.009 of
        // the string, authored), so their spectra are not one comb twice.
        // The shape is laid over the `length` samples written below, but the
        // string's period is the loop's whole round trip, contactPeriodSamples,
        // which the loss, dispersion and bending sections lengthen past the
        // delay line: 3-13% longer. A kink placed at p of the written line
        // then sat that much bridgeward of p on the string, and its partials
        // 6-9 dB rms off the rest pluck's law. Scaled by the period over the
        // written length it lands at p of the string, which a blind listener
        // preferred on 2026-09-30 (Docs/decisions.md); the bound leaves room
        // for that scale past the 0.48 the position itself stops at.
        const float frameScale = voice.contactPeriodSamples > 0.0f
            ? voice.contactPeriodSamples / static_cast<float>(length) : 1.0f;
        const float localPosition = clamp((position
            + (polarisation == 0 ? -0.006f : 0.009f)) * frameScale, 0.05f, 0.60f);
        voice.releaseShapePosition[static_cast<std::size_t>(polarisation)] = localPosition;
        const float polarisationGain = polarisation == 0
            ? exact::sqrt(voice.polarisationMix)
            : voice.pluckParallelSign * exact::sqrt(1.0f - voice.polarisationMix);
        // The hand held the string aside before it let go, so the saddle
        // carried the static force of the string's slope there, T y / a,
        // and the top stood deflected under it. The wave written here
        // carries what follows the release but not that the force existed
        // before it, so the top never sprang back: on the flat-top
        // recordings every note, picked or plucked, rings the air mode and
        // the low top modes (60-180 Hz at -17 dB re its first 100 ms on the
        // Eastman E1D, -12 on the Martin HD28, above a G string's pitch)
        // where the model stood 13-30 dB under. In the line's own terms the
        // force is the level of its steep flank, 2Z times the flank's rise
        // per sample, and the junction gets it back as the release's step
        // (process). Only the normal plane's share pushes on the top; the
        // parallel plane's acts along it. A natural harmonic is held aside
        // by the picking hand as any note is - the finger on its node damps
        // what the node does not share only once the string is let go - so
        // its saddle sheds the same force. The step is given back at 0.8 of
        // that force: a listener liked the thump and chose it at 80% of the
        // full release (Docs/decisions.md, 2026-10-01), chosen by ear. A
        // re-pluck adds its step to what an earlier one has still to give
        // back rather than cutting it off: cut, the earlier step's net
        // impulse is no longer zero and the bridge keeps a low kick. Two
        // plucks on one sample - a doubled note-on before any audio - are
        // one release: the first wave goes under the hand (captureTail)
        // before it has sounded, and a hand that let go and held the string
        // again in no time put back the force it had let go, so the rise
        // still waiting is replaced, not added to. Added, a doubled note
        // thumped 6 dB harder than the one pluck it sounds as.
        constexpr float releaseStepShare = 0.8f;
        if (polarisation == 0 && releaseStepEnabled_)
        {
            const float rise = releaseStepShare * releasedAmplitude
                * polarisationGain
                / (localPosition * static_cast<float>(length));
            if (rise != 0.0f)
            {
                voice.releaseStepRise = rise;
                voice.releaseStepAge = 0;
            }
        }
        const float currentReferenceLength = loop.targetDelay * 48000.0f
            / static_cast<float>(sampleRate_);
#if defined(ACUSTRA_ANALYSIS_APERTURE_MILLISECONDS)
        const float apertureSamples
            = ACUSTRA_ANALYSIS_APERTURE_MILLISECONDS * 48.0f;
#else
        // Authored terms, fitted with the rest of the pluck
        // (Docs/decisions.md): Touch narrows the contact; the three lowest
        // strings take a sample more (a nylon-era grouping of the wound
        // basses that the steel fit kept), and the short upper-register
        // string 1.5 more. Both were steps - the D to the G string, and the
        // 16th to the 17th fret - that changed a note's contact by up to 70%
        // between neighbours; they are ramps now, full at the same ends
        // (the low E and A, and from the 19th fret), half-way at the D and
        // the 16th fret.
        const float woundContact = clamp(
            (3.0f - static_cast<float>(stringIndex)) / 2.0f, 0.0f, 1.0f);
        const float upperContact = 1.5f * clamp(
            (voice.speakingFret - 13.0f) / 6.0f, 0.0f, 1.0f);
        const float apertureSamples = 0.70f + 3.60f * (1.0f - touch)
            + woundContact + upperContact;
#endif
        const int modes = std::max(voice.harmonic, 1);
        // A thumb's soft pad retains a finite contact footprint even at hard
        // velocities. Convolving the velocity-dependent Gaussian with that
        // pad adds their variances; it does not clamp away Touch response.
        // All widths use the same reference-rate/register conversion below.
        // The style ratios and 2.5-sample pad are authored, not measured tool
        // dimensions. Finger retains its calibrated arithmetic.
        const float contactSamples = parameters_.picking == PickingTechnique::Pick
            ? 0.35f * apertureSamples
            : parameters_.picking == PickingTechnique::Thumb
                ? exact::sqrt(4.0f * apertureSamples * apertureSamples + 6.25f)
                : apertureSamples;
        const float aperture = registeredPluckAperture(
            contactSamples, physical.apertureScale, apertureReferenceDelay,
            currentReferenceLength,
            physicalCalibration_.apertureRegisterExponent);
        if (polarisation == 0)
            contactWidthRatio = apertureSamples / std::max(contactSamples, 1.0e-3f);
        if (releaseShare > 0.0f)
        {
            writePickRelease(loop, length, releasedAmplitude * polarisationGain,
                             localPosition, aperture, modes, releaseShare,
                             slipPole, voice.referencePickDelay[
                                 static_cast<std::size_t>(polarisation)]);
            continue;
        }
        const auto triangleAt = [localPosition] (double phase)
        {
            return phase < localPosition
                ? phase / localPosition
                : (1.0 - phase) / (1.0 - localPosition);
        };
        // Continuous periodic Gaussian contact profile, sigma=aperture:
        // the same variance as the former [1,4,6,4,1]/16 spatial atoms.
        // This is a numerical profile candidate, not a measured finger shape.
        const double sigma = aperture;
        const double gaussianPi = std::acos(-1.0);
        const double cornerScale = 1.0
            / (static_cast<double>(localPosition) * (1.0 - localPosition));
        const double epsilon = std::numeric_limits<double>::epsilon();
        const bool uniformToPrecision = std::exp(
            -2.0 * gaussianPi * gaussianPi * sigma * sigma)
            * cornerScale / 6.0 <= epsilon;
        // The reset delay is already the exact endpoint-subtracted uniform
        // wave. Avoid evaluating the same constant at every sample.
        if (uniformToPrecision)
            continue;
        // q(z)=sigma*phi(|z|/sigma)-|z|*Phi(-|z|/sigma), q''=G-delta.
        // For R>=1 the four omitted image tails sum to no more than
        // 4*sigma*(1+sigma)*phi(R)*cornerScale. This R bounds that by epsilon.
        const double radius = sigma * exact::sqrt(-2.0 * std::log(
            epsilon / (4.0 * sigma * (1.0 + sigma) * cornerScale)));
        const double inverseSigma = 1.0 / sigma;
        const auto gaussianCorner = [] (double z)
        {
            // q(10) < 7.48e-25; farther tails are below float precision.
            if (z >= 10.0)
                return 0.0;
            const double scaled = z * 64.0;
            const int index = static_cast<int>(scaled);
            const double t = scaled - index;
            const auto& a = gaussianAperture::cornerTable[index];
            const auto& b = gaussianAperture::cornerTable[index + 1];
            const double da = a[1] / 64.0;
            const double db = b[1] / 64.0;
            const double difference = b[0] - a[0];
            return a[0] + t * (da + t * (3.0 * difference - 2.0 * da - db
                + t * (-2.0 * difference + da + db)));
        };
        const auto periodicCorner = [&] (double phase)
        {
            double sum = 0.0;
            const int first = static_cast<int>(std::ceil(phase - radius));
            const int last = static_cast<int>(exact::floor(phase + radius));
            for (int image = first; image <= last; ++image)
            {
                const double distance = exact::abs(phase - image);
                sum += sigma * gaussianCorner(distance * inverseSigma);
            }
            return sum;
        };
        const auto nearestCorner = [&] (double phase)
        {
            // The caller's phase is in (-1,1). With radius < 1/2, at most
            // the nearest periodic image contributes to the existing sum.
            const double absolute = exact::abs(phase);
            const double distance = std::min(absolute, 1.0 - absolute);
            return distance <= radius
                ? sigma * gaussianCorner(distance * inverseSigma) : 0.0;
        };
        // A light touch at the nth node leaves exactly the modes that have a
        // node there. Averaging the released shape over its n cyclic shifts is
        // that projection exactly: every harmonic that is a multiple of n
        // passes at unit gain and every other one cancels. It needs no filter
        // and no free constant, and the surviving modes keep precisely the
        // amplitude the pluck gave them - which is why a harmonic comes out
        // quieter than the stopped note, as one does on a guitar.
        const auto initialise = [&] (const auto& cornerAt)
        {
            const auto smoothedTriangleAt = [&] (double wrapped)
            {
                return static_cast<float>(triangleAt(wrapped) + cornerScale
                    * (cornerAt(wrapped) - cornerAt(wrapped - localPosition)));
            };
            const auto releasedAt = [&] (float phase)
            {
                // Ordinary pluck phases already lie in [0,1), so neither
                // triangle nor kernel requires another floor operation.
                if (modes <= 1)
                    return smoothedTriangleAt(phase);
                float sum = 0.0f;
                for (int shift = 0; shift < modes; ++shift)
                {
                    const float shifted = phase + static_cast<float>(shift)
                                                  / static_cast<float>(modes);
                    sum += smoothedTriangleAt(static_cast<double>(shifted)
                                              - exact::floor(shifted));
                }
                return sum / static_cast<float>(modes);
            };
            const float endpoint = releasedAt(0.0f);
            // Subtracting a constant changes only DC. Keep the signed wave:
            // rectification would introduce corners absent from the contact.
            const auto fillRange = [&] (auto smoothedTag, int first, int last)
            {
                constexpr bool smoothed = decltype(smoothedTag)::value;
                for (int sample = first; sample < last; ++sample)
                {
                    const float phase = static_cast<float>(sample)
                                      / static_cast<float>(length);
                    const float released = smoothed ? releasedAt(phase)
                        : static_cast<float>(triangleAt(phase));
                    const float triangle = released - endpoint;
                    // reset() establishes writeIndex=0. These samples never
                    // need the general circular-index wrap operation.
                    loop.delay[static_cast<std::size_t>(maximumDelaySamples - sample - 1)]
                        = releasedAmplitude * polarisationGain * triangle;
                }
            };
            if (modes == 1 && radius < 0.5)
            {
                // Away from the three corner images, the bounded kernel is
                // exactly zero and the original triangle is unchanged. Pad
                // each sample interval to cover float phase-rounding at its
                // edges; cornerAt retains the exact original radius check.
                const auto firstAt = [&] (double phase)
                {
                    return std::clamp(static_cast<int>(exact::floor(phase * length)) - 1,
                                      0, length);
                };
                const auto lastAt = [&] (double phase)
                {
                    return std::clamp(static_cast<int>(std::ceil(phase * length)) + 2,
                                      0, length);
                };
                const std::array<std::array<int, 2>, 3> supports {{
                    { 0, lastAt(radius) },
                    { firstAt(localPosition - radius), lastAt(localPosition + radius) },
                    { firstAt(1.0 - radius), length }
                }};
                int filled = 0;
                for (const auto& support : supports)
                {
                    if (support[0] > filled)
                        fillRange(std::false_type {}, filled, support[0]);
                    filled = std::max(filled, support[0]);
                    if (support[1] > filled)
                        fillRange(std::true_type {}, filled, support[1]);
                    filled = std::max(filled, support[1]);
                }
            }
            else
                fillRange(std::true_type {}, 0, length);
        };
        // Specialize outside the sample loop. Broad contacts retain every
        // periodic image selected by the same analytic tail bound.
        if (radius < 0.5)
            initialise(nearestCorner);
        else
            initialise(periodicCorner);
        if (unslipPole > 0.0)
        {
            const auto lineAt = [&loop] (int sample) -> float&
            {
                return loop.delay[static_cast<std::size_t>(
                    maximumDelaySamples - sample - 1)];
            };
            const auto spread = [&] ()
            {
                double mean = 0.0;
                for (int sample = 0; sample < length; ++sample)
                    mean += lineAt(sample);
                mean /= static_cast<double>(length);
                double sum = 0.0;
                for (int sample = 0; sample < length; ++sample)
                    sum += (lineAt(sample) - mean) * (lineAt(sample) - mean);
                return sum;
            };
            const double before = spread();
            applyPlectrumSlip(loop, length, slipPole);
            if (parameters_.picking != PickingTechnique::Finger)
            {
                // Thumb retains its reference slip's periodic inverse,
                // re-zeroed at the bridge sample as the slip itself is.
                const double b = unslipPole;
                double previous = lineAt(length - 1);
                double first = 0.0;
                for (int sample = 0; sample < length; ++sample)
                {
                    const double current = lineAt(sample);
                    const double value = (current - b * previous) / (1.0 - b);
                    previous = current;
                    if (sample == 0)
                        first = value;
                    lineAt(sample) = static_cast<float>(value - first);
                }
            }
            const double after = spread();
            if (before > 0.0 && after > 0.0)
            {
                const float scale = static_cast<float>(exact::sqrt(before / after));
                for (int sample = 0; sample < length; ++sample)
                    lineAt(sample) *= scale;
            }
        }
        else
            applyPlectrumSlip(loop, length, slipPole);
    }

    if (merge)
    {
        double freshEnergy = 0.0;
        double cross = 0.0;
        // Both fresh planes are complete: writePickRelease no longer needs
        // its scratch waves. Reuse them for exactly the aligned increments
        // whose work is measured here, then applied below. This avoids a
        // second fractional read/divide for every sample without changing
        // either pass's arithmetic or adding delay-sized storage.
        const std::array<float*, 2> increments {
            pickReleaseDisplacement_.data(), pickReleaseVelocity_.data() };
        for (int plane = 0; plane < 2; ++plane)
        {
            auto& previous = repluckOldLoops_[static_cast<std::size_t>(plane)];
            conditionRepluckContact(previous, position, true);
            const auto work = repluckIncrementWork(
                voice.loops[static_cast<std::size_t>(plane)], previous,
                increments[static_cast<std::size_t>(plane)]);
            freshEnergy += work[0];
            cross += work[1];
        }
        // A prescribed stroke can supply its fresh release's work, not an
        // extra positive interference term from a coherently ringing wave.
        // Reduce force only when that cross term is positive; cancellation
        // may dissipate work, and never boosts a softer stroke. This is a
        // conservative authored work bound, not a fitted hand-force law.
        if (cross > 0.0 && freshEnergy > 0.0)
            voice.repluckForceGain = static_cast<float>(freshEnergy
                / (cross + std::sqrt(cross * cross + freshEnergy * freshEnergy)));
        for (int plane = 0; plane < 2; ++plane)
            mergeRepluckLoop(voice.loops[static_cast<std::size_t>(plane)],
                repluckOldLoops_[static_cast<std::size_t>(plane)], voice.repluckForceGain,
                increments[static_cast<std::size_t>(plane)]);
        voice.releaseStepRise *= voice.repluckForceGain;
        // Restored memories carry the old coefficients too. Reapply current
        // controls after the merge even when the prior cache key was equal.
        voice.configurationKey.generation = 0;
    }

    double slopeEnergy = 0.0;
    for (auto& loop : voice.loops)
    {
        const int length = std::clamp(
            static_cast<int>(std::round(merge ? loop.currentDelay : loop.targetDelay)), 8,
            maximumDelaySamples - 3);
        const auto at = [&] (int sample)
        {
            return loop.delay[static_cast<std::size_t>(wrapDelayIndex(
                loop.writeIndex - sample))];
        };
        float previous = at(length);
        double squaredDifferences = 0.0;
        for (int sample = 1; sample <= length; ++sample)
        {
            const float current = at(sample);
            const double difference
                = static_cast<double>(current - previous);
            squaredDifferences += difference * difference;
            previous = current;
        }
        slopeEnergy += static_cast<double>(length) * squaredDifferences;
    }
    voice.attackSlopeEnergy = exact::isfinite(slopeEnergy)
        ? static_cast<float>(std::max(slopeEnergy, 0.0)) : 0.0f;
    voice.observedSlopeEnergy = voice.attackSlopeEnergy;
    updateAttackPitch(voice, stringIndex);

    voice.velocity = v;
    voice.excitationWhite = pick && physicalCalibration_.pickTransientGain > 0.0f;
    if (voice.excitationWhite)
    {
        // One speed law for the plectrum: the release share above goes as
        // the tip's speed squared, so that speed goes as v^(exponent/2), and
        // an impact's transient amplitude goes as the speed itself rather
        // than as the note it starts. Referenced to the Finger law's own
        // full-velocity burst, so a gain of one meets it there and the
        // fitted gain says how much louder a pick's click is.
        const float speedRatio = std::pow(
            v, 0.5f * physicalCalibration_.pickReleaseVelocityExponent);
        voice.excitationEnvelope = physicalCalibration_.pickTransientGain
            * 0.24f * 0.017f * physical.transientScale
            * speedRatio * strumLevelGain * voice.repeatedPluckGain;
    }
    else
        voice.excitationEnvelope = amplitude * (0.003f + 0.014f * touch)
            * physical.transientScale;
    voice.excitationEnvelope *= voice.repluckForceGain;
    // A natural harmonic's finger is still on the node when the pluck lets
    // go, so the release's broadband burst is filtered by it as every other
    // mode the node does not share is. The burst is written at the bridge,
    // where no node projection reaches it, and left in it put more energy
    // into the open string's other partials than into the harmonic: D#6's
    // loudest partial was the open B's sixth (F#6), E6's and E7's the open
    // E's twenty-fifth.
    if (voice.harmonic > 1)
        voice.excitationEnvelope = 0.0f;
    const float burstSeconds = 0.0046f - 0.0025f * touch;
    voice.excitationDecay = std::exp(-1.0f
        / (std::max(burstSeconds, 0.0004f) * static_cast<float>(sampleRate_)));
    voice.excitationColour = 0.10f + 0.62f * touch;
    voice.excitationLowpass = 0.0f;
    voice.excitationLowpass2 = 0.0f;
    // A fingertip or the thumb's pad is not a plectrum: over its first
    // 15 ms the finger-plucked flat-tops (the bank's flat-top rows and the
    // Eastman E1D's finger take) carry 10-25 dB less 2-16 kHz than the
    // burst's one-pole-plus-white spectrum gave them. The soft contacts'
    // burst is the same noise through the same corner twice, with no white
    // share: -12 dB per octave above the corner instead of -6 and a flat
    // floor. The plectrum keeps its law.
    voice.excitationSoft = parameters_.picking != PickingTechnique::Pick;
    // Contact roughness cannot shed force faster than the contact releases
    // it. Finger's existing two-stage burst therefore has at least the
    // actual release's sampled mean time. A one-pole at b has mean b/(1-b);
    // two stages at q have mean 2q/(1-q), equal for q=b/(2-b). Apply that
    // corner only when it is slower than the existing colour corner.
    // This is an authored coupling of the existing effective contact model,
    // not a measured fingertip trajectory. Preserve DC gain, noise draws,
    // envelope and all wave/held-force levels; do not normalize away the
    // resulting native noise-energy change. Pick and Thumb keep their law.
    // Freeze the corner at actual contact (including scheduled controls),
    // and cache only its force-independent colour part across attacks.
    const float burstRateRatio = static_cast<float>(sampleRate_) / 48000.0f;
    if (exact::bits(voice.excitationColour) != voice.excitationCoefficientColour
        || exact::bits(burstRateRatio) != voice.excitationCoefficientRate)
    {
        const float referenceCoefficient = 0.05f + 0.42f * voice.excitationColour;
        voice.excitationCoefficient = 1.0f - std::pow(
            1.0f - referenceCoefficient, 1.0f / burstRateRatio);
        voice.excitationCoefficientColour = exact::bits(voice.excitationColour);
        voice.excitationCoefficientRate = exact::bits(burstRateRatio);
    }
    voice.excitationReleaseCoefficient = voice.excitationCoefficient;
    if (performanceRealism_.contactRelease
        && parameters_.picking == PickingTechnique::Finger
        && voice.releaseSlipPole > 0.0)
        voice.excitationReleaseCoefficient = std::min(
            voice.excitationCoefficient,
            static_cast<float>(2.0 * (1.0 - voice.releaseSlipPole)
                               / (2.0 - voice.releaseSlipPole)));
    // The burst draws on from where this pluck's draws ended, as it always
    // has, but from its own copy: its length follows velocity, Touch and the
    // rate, so drawn from randomState it moved every later pluck's draws
    // with them. The next pluck draws from a scrambled continuation instead
    // (the murmur3 finaliser), so its draws are not this burst's samples.
    voice.excitationNoiseState = voice.randomState;
    {
        std::uint32_t mixed = voice.randomState;
        mixed ^= mixed >> 16;
        mixed *= 0x85ebca6bu;
        mixed ^= mixed >> 13;
        mixed *= 0xc2b2ae35u;
        mixed ^= mixed >> 16;
        voice.randomState = mixed == 0u ? 0x6d2b79f5u : mixed;
    }
    initialiseContactNoise(voice, v, position, heldDistance,
                           releasedAmplitude * voice.repluckForceGain, contactWidthRatio);
    voice.level = std::max(voice.level, 0.02f * v);
    voice.releaseDamping = 1.0f;
    voice.releaseSeconds = 0.0f;
    voice.returnSamples = 0;
    // A new pluck ends a key-up's touch still sounding: its drive stops and
    // what its stages and travel hold runs out on its own.
    voice.releaseNoiseTouch = voice.releaseNoiseBrush = 0.0f;
    voice.peakLevel = voice.level;
    voice.releaseVelocity = -1.0f;
}

void AcustraEngine::returnToOpenString(Voice& voice, int stringIndex,
                                       bool clearDelay) noexcept
{
    if (voice.keyDown || voice.pedalHeld)
        releaseFinger(stringIndex);
    voice.played = false;
    voice.keyDown = false;
    voice.pedalHeld = false;
    voice.releaseJoinPending = false;
    voice.releaseJoinRemainingBeats = 0.0;
    voice.releaseJoinAnchorSample = 0;
    voice.releaseJoinWindowSamples = 0;
    voice.mpeMember = false;
    voice.memberPitchBendFrozen = false;
    voice.ownerCount = 0;
    voice.midiNote = voice.openMidi;
    voice.midiChannel = 1;
    voice.fret = 0;
    voice.velocity = 0.0f;
    voice.legatoContactSamples = 0;
    voice.legatoContactAmplitude = 0.0f;
    if (clearDelay)
        voice.legatoContactTravel.active = false;
    voice.excitationEnvelope = 0.0f;
    voice.contactTravelEnabled = false;
    voice.contactTravel.active = false;
    voice.contactNoiseSamples = 0;
    voice.contactNoiseTravel.active = false;
    voice.tailContactNoiseTravel.active = false;
    voice.releaseVelocity = -1.0f;
    voice.peakLevel = 0.0f;
    // A string handed back after its release keeps the key-up's sound running
    // out (it ends before the hand's T60 and 80 ms do); a reset stops it.
    if (clearDelay)
    {
        voice.releaseNoiseSamples = 0;
        voice.releaseNoiseTouch = voice.releaseNoiseBrush = 0.0f;
        voice.releaseNoiseStage1 = voice.releaseNoiseStage2 = 0.0f;
        voice.releaseNoiseBand1 = voice.releaseNoiseBand2 = 0.0f;
        voice.releaseNoiseLaunched = 0.0f;
        voice.releaseNoiseTravel.active = false;
        voice.releaseStepAge = -1;
        voice.releaseStepRise = voice.releaseStepForce
            = voice.releaseStepLevel = 0.0f;
    }
    voice.attackPitchCents = 0.0f;
    voice.attackPitchDecay = 1.0f;
    voice.frozenMemberPitchBendSemitones = 0.0f;
    voice.attackSlopeEnergy = 0.0f;
    voice.observedSlopeEnergy = 0.0f;
    voice.longitudinalY1.fill(0.0f);
    voice.longitudinalY2.fill(0.0f);
    voice.harmonic = 1;
    voice.releaseDamping = 1.0f;
    voice.releaseSeconds = 0.0f;
    voice.returnSamples = 0;
    for (auto& loop : voice.loops)
        loop.gestureContact = {};
    voice.pluckDelay = 0;
    voice.repluckPending = false;
    voice.repluckContactPending = false;
    if (clearDelay)
        voice.repluckArrivals.clear();
    voice.releaseAfterPluck = false;
    voice.pedalHeldAtKeyUp = false;
    voice.pedalReleasedBeforePluck = false;
    // The string's retained tail is a port on the bridge. The hand-back
    // after a key-up lets it go the way a quiet tail goes (finishVoice),
    // fading its port out of the junction: dropped here in one sample, the
    // impedance every string's bridge reads stepped under whatever else was
    // sounding, a faint tick 240 ms after a fretted key-up and 1.33 s after
    // an open one. A reset still clears it at once.
    if (clearDelay || !voice.tailActive)
    {
        voice.tailActive = false;
        voice.tailRetiring = false;
        voice.tailContactTravel.active = false;
        voice.tailLegatoContactTravel.active = false;
        voice.tailLegatoContactSamples = 0;
        voice.tailCharacteristicImpedance = 0.0f;
        voice.tailLevel = 0.0f;
        voice.tailQuietSamples = 0;
        voice.tailLoop.reset();
        voice.tailParallelLoop.reset();
        voice.tailRepluckArrivals.clear();
    }
    else
        voice.tailRetiring = true;
    configureVoice(voice, stringIndex, voice.openMidi, clearDelay, false,
                   !clearDelay);
}

void AcustraEngine::conditionRepluckContact(StringLoop& loop,
                                            float position,
                                            bool stopVelocity) noexcept
{
    if (!(position > 0.0f && position < 1.0f))
        return;
    const float period = loop.currentDelay;
    const int length = std::clamp(static_cast<int>(std::ceil(period)) + 1,
                                  4, maximumDelaySamples - 2);
    const auto at = [&] (int age) -> float&
    {
        return loop.delay[static_cast<std::size_t>(
            wrapDelayIndex(loop.writeIndex - 1 - age))];
    };
    // The same fractional taps as displacementAt: include their complete
    // read prehistory rather than rounding the period/contact to a grid.
    const float travel = 0.5f * period * position;
    const float reflected = period - travel;
    const int directAge = static_cast<int>(travel);
    const int reflectedAge = static_cast<int>(reflected);
    const double directFraction = travel - directAge;
    const double reflectedFraction = reflected - reflectedAge;
    std::array<std::array<int, 8>, 2> ages {};
    std::array<std::array<double, 8>, 2> weights {};
    ages[0] = { reflectedAge, reflectedAge + 1, directAge, directAge + 1 };
    weights[0] = { 1.0 - reflectedFraction, reflectedFraction,
                   -(1.0 - directFraction), -directFraction };
    // One ideal cyclic advance of the same field, minus its current point
    // observation, is the local velocity functional in wave/sample units.
    for (int tap = 0; tap < 4; ++tap)
    {
        ages[1][static_cast<std::size_t>(tap)]
            = (ages[0][static_cast<std::size_t>(tap)] + length - 1) % length;
        weights[1][static_cast<std::size_t>(tap)]
            = weights[0][static_cast<std::size_t>(tap)];
        ages[1][static_cast<std::size_t>(tap + 4)]
            = ages[0][static_cast<std::size_t>(tap)];
        weights[1][static_cast<std::size_t>(tap + 4)]
            = -weights[0][static_cast<std::size_t>(tap)];
    }
    const auto contactWeight = [&] (int axis, int age)
    {
        double result = 0.0;
        for (int tap = 0; tap < (axis == 0 ? 4 : 8); ++tap)
            if (age == ages[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)])
                result += weights[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)];
        return result;
    };
    // The static point-force triangle is the ideal string's Green function.
    // Use its discrete counterpart h: cyclic Laplacian(h) = contactWeight,
    // so <Ds,Dh> = displacementAt(p). Removing its slope-orthogonal component
    // makes the retained displacement zero under the hand and gives exactly
    // E_after = E_before - cross^2/norm before float rounding. The wrap-edge
    // difference belongs to this norm too. Integrating the sparse contact
    // weights twice solves h without allocating a delay-sized scratch array.
    // This is a preceding-hold approximation, not a resolved tool trajectory
    // or a full string/body energy ledger. Fractional-period discretisation
    // and copied filter memories remain outside that physical claim. The
    // ordinary intrinsic string loss still damps uncoupled modes; there is
    // no second string branch or artificial post-release tail on a merge.
    const int axes = stopVelocity ? 2 : 1;
    std::array<double, 2> firstSlope {}, cross {}, norm {}, cumulativeWeight {},
                          wave {}, waveSum {};
    for (int axis = 0; axis < axes; ++axis)
        for (int tap = 0; tap < (axis == 0 ? 4 : 8); ++tap)
            firstSlope[static_cast<std::size_t>(axis)]
                += (length - 1 - ages[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)])
                    * weights[static_cast<std::size_t>(axis)][static_cast<std::size_t>(tap)] / length;
    double mixedNorm = 0.0;
    double previous = at(length - 1);
    for (int age = 0; age < length; ++age)
    {
        const double current = at(age);
        for (int axis = 0; axis < axes; ++axis)
        {
            const auto a = static_cast<std::size_t>(axis);
            const double slope = firstSlope[a] - cumulativeWeight[a];
            cross[a] += (current - previous) * slope;
            norm[a] += slope * slope;
            wave[a] += slope;
            waveSum[a] += wave[a];
        }
        mixedNorm += (firstSlope[0] - cumulativeWeight[0])
                   * (firstSlope[1] - cumulativeWeight[1]);
        for (int axis = 0; axis < axes; ++axis)
            cumulativeWeight[static_cast<std::size_t>(axis)] += contactWeight(axis, age);
        previous = current;
    }
    if (!(norm[0] > 0.0) || !exact::isfinite(cross[0]))
        return;
    std::array<double, 2> removed { cross[0] / norm[0], 0.0 };
    const double determinant = norm[0] * norm[1] - mixedNorm * mixedNorm;
    if (stopVelocity && determinant > 1.0e-12 * norm[0] * norm[1])
    {
        removed[0] = (cross[0] * norm[1] - cross[1] * mixedNorm) / determinant;
        removed[1] = (cross[1] * norm[0] - cross[0] * mixedNorm) / determinant;
    }
    if (removed[0] == 0.0 && removed[1] == 0.0)
        return;
    cumulativeWeight = {};
    wave = {};
    for (int age = 0; age < length; ++age)
    {
        double correction = 0.0;
        for (int axis = 0; axis < axes; ++axis)
        {
            const auto a = static_cast<std::size_t>(axis);
            wave[a] += firstSlope[a] - cumulativeWeight[a];
            correction += removed[a] * (wave[a] - waveSum[a] / length);
            cumulativeWeight[a] += contactWeight(axis, age);
        }
        at(age) = static_cast<float>(at(age) - correction);
    }
    // The observer should retain the incoming wave's velocity trend rather
    // than count the imposed displacement constraint as an impact.
    loop.derivativeCrossesContact = true;
}

float AcustraEngine::alignedRepluckIncrement(const StringLoop& fresh,
    const StringLoop& previous, int age) noexcept
{
    const float sourceAge = static_cast<float>(age)
        * fresh.currentDelay / previous.currentDelay;
    const int whole = static_cast<int>(sourceAge);
    const float part = sourceAge - whole;
    const auto at = [&] (int n)
    {
        return fresh.delay[static_cast<std::size_t>(
            wrapDelayIndex(fresh.writeIndex - 1 - n))];
    };
    return at(whole) + part * (at(whole + 1) - at(whole));
}

std::array<double, 2> AcustraEngine::repluckIncrementWork(
    const StringLoop& fresh, const StringLoop& previous,
    float* alignedIncrements) noexcept
{
    const int length = std::clamp(static_cast<int>(std::ceil(previous.currentDelay)) + 1,
                                  4, maximumDelaySamples - 2);
    const auto at = [&] (int age)
    {
        return previous.delay[static_cast<std::size_t>(
            wrapDelayIndex(previous.writeIndex - 1 - age))];
    };
    double old = at(length - 1);
    double increment = alignedRepluckIncrement(fresh, previous, length - 1);
    std::array<double, 2> result {};
    for (int age = 0; age < length; ++age)
    {
        const double current = at(age);
        const double next = alignedRepluckIncrement(fresh, previous, age);
        if (alignedIncrements != nullptr)
            alignedIncrements[age] = static_cast<float>(next);
        const double slope = next - increment;
        result[0] += slope * slope;
        result[1] += (current - old) * slope;
        old = current;
        increment = next;
    }
    return result;
}

void AcustraEngine::mergeRepluckLoop(StringLoop& fresh, StringLoop& previous,
                                     float gain, const float* alignedIncrements) noexcept
{
    const int length = std::clamp(static_cast<int>(std::ceil(previous.currentDelay)) + 1,
                                  4, maximumDelaySamples - 2);
    for (int age = 0; age < length; ++age)
        previous.delay[static_cast<std::size_t>(
            wrapDelayIndex(previous.writeIndex - 1 - age))]
            += gain * (alignedIncrements != nullptr ? alignedIncrements[age]
                : alignedRepluckIncrement(fresh, previous, age));
    previous.targetDelay = fresh.targetDelay;
    // Keep the already travelling field, its ring phase and every stored
    // filter state. The ordinary continuing configure below adopts current
    // control coefficients without resetting those memories.
    previous.derivativeCrossesContact = true;
    fresh = previous;
}

void AcustraEngine::RepluckArrivals::clear() noexcept
{
    for (auto& plane : wave) plane.fill(0.0f);
    readIndex = remaining = 0;
    overflow = false;
}

void AcustraEngine::RepluckArrivals::add(int offset, float normal,
                                       float parallel) noexcept
{
    if (offset >= capacity)
    {
        overflow = true;
        return;
    }
    if (normal == 0.0f && parallel == 0.0f)
        return;
    const auto slot = static_cast<std::size_t>((readIndex + offset) % capacity);
    wave[0][slot] += normal;
    wave[1][slot] += parallel;
    remaining = std::max(remaining, offset + 1);
}

std::array<float, 2> AcustraEngine::RepluckArrivals::process() noexcept
{
    if (remaining <= 0) return {};
    const auto slot = static_cast<std::size_t>(readIndex);
    const std::array<float, 2> result { wave[0][slot], wave[1][slot] };
    wave[0][slot] = wave[1][slot] = 0.0f;
    readIndex = (readIndex + 1) % capacity;
    --remaining;
    return result;
}

void AcustraEngine::retainRepluckArrivals(Voice& voice) noexcept
{
    // Freeze each already emitted packet's own contact delays. They cannot
    // be mixed by adding delay histories whose tap coefficients differ.
    // This queue holds their future arrivals, preserving a third rapid
    // attack without allocating another string or discarding the first.
    // Transport input history is <=8192 samples. Legal contact geometry
    // bounds first-order poles below .82 and second-order poles below .88;
    // another 8192 zero samples drains even float-max input below the double
    // retirement threshold. The <=1153-sample explicit finger pulse fits
    // within that bound. Overflow is an asserted invariant, never hidden.
    int offset = 0;
    while ((voice.contactTravelEnabled && voice.contactTravel.active)
        || voice.contactNoiseTravel.active || voice.legatoContactTravel.active
        || voice.legatoContactSamples > 0)
    {
        if (offset >= RepluckArrivals::capacity)
        {
            voice.repluckArrivals.overflow = true;
            assert(false && "repluck contact transport exceeded its proven bound");
            break;
        }
        float normal = 0.0f;
        float parallel = 0.0f;
        constexpr float split = 0.7071067811865475f;
        if (voice.contactTravelEnabled && voice.contactTravel.active)
        {
            const auto paths = voice.contactTravel.process(0.0f);
            const float local = split * (paths[0] - paths[1]);
            normal += 0.76f * local;
            parallel += voice.excitationParallelGain * local;
        }
        if (voice.contactNoiseTravel.active)
        {
            const auto paths = voice.contactNoiseTravel.process(0.0f);
            const float local = paths[0] - paths[1];
            normal += voice.contactNoiseNormal * local;
            parallel += voice.contactNoiseParallel * local;
        }
        if (voice.legatoContactSamples > 0 || voice.legatoContactTravel.active)
        {
            float source = 0.0f;
            if (voice.legatoContactSamples > 0)
            {
                source = voice.legatoContactAmplitude
                    * voice.legatoContactPulse[static_cast<std::size_t>(voice.legatoContactAge)];
                if (++voice.legatoContactAge > voice.legatoContactSamples)
                    voice.legatoContactSamples = 0;
            }
            const auto paths = voice.legatoContactTravel.process(source);
            normal += split * (paths[0] - paths[1]);
        }
        voice.repluckArrivals.add(offset++, normal, parallel);
    }
}

void AcustraEngine::captureTail(Voice& voice) noexcept
{
    // The retained wave overlaps the newly released pluck as a second virtual
    // string state. Its nominal 10 ms post-release T60 is provisional.
    // Laurson, Erkut, Valimaki and Kuuskankare (CMJ 25(3), 2001,
    // "Simulation of Playing Styles") instead drive the loop gain toward
    // zero over about 10 ms BEFORE replucking; that gain ramp does not measure
    // this T60. Erkut et al. (AES 108, 2000, preprint 5114, Sec. 3.2) report a
    // 20-60 ms finger-contact regime, also distinct from a decay constant.
    // Bridge backreaction can keep this branch active after its initial wave
    // has damped; the existing reaction-force threshold decides retirement.
    if (!(voice.level > 2.0e-7f)
        && !(voice.contactTravelEnabled && voice.contactTravel.active)
        && !voice.contactNoiseTravel.active
        && !voice.legatoContactTravel.active && voice.legatoContactSamples == 0
        && voice.repluckArrivals.remaining == 0
        && !(voice.loops[0].gestureContact.active
            && exact::abs(voice.loops[0].gestureContact.memory) > 1.0e-12f)
        && !(voice.loops[1].gestureContact.active
            && exact::abs(voice.loops[1].gestureContact.memory) > 1.0e-12f))
    {
        voice.tailActive = false;
        voice.tailRetiring = false;
        voice.tailContactTravel.active = false;
        voice.tailContactNoiseTravel.active = false;
        voice.tailLegatoContactTravel.active = false;
        voice.tailLegatoContactSamples = 0;
        voice.tailCharacteristicImpedance = 0.0f;
        return;
    }
    voice.tailLoop = voice.loops[0];
    voice.tailParallelLoop = voice.loops[1];
    voice.tailExcitationParallelGain = voice.excitationParallelGain;
    voice.tailRepluckArrivals = voice.repluckArrivals;
    voice.repluckArrivals.clear();
    if (voice.legatoContactTravel.active || voice.legatoContactSamples > 0)
    {
        voice.tailLegatoContactTravel = voice.legatoContactTravel;
        voice.tailLegatoContactAmplitude = voice.legatoContactAmplitude;
        voice.tailLegatoContactAge = voice.legatoContactAge;
        voice.tailLegatoContactSamples = voice.legatoContactSamples;
        voice.tailLegatoContactPulse = voice.legatoContactPulse;
    }
    else
    {
        voice.tailLegatoContactTravel.active = false;
        voice.tailLegatoContactSamples = 0;
    }
    voice.legatoContactTravel.active = false;
    voice.legatoContactSamples = 0;
    voice.legatoContactAmplitude = 0.0f;
    // Already emitted contact waves still travelling toward the bridge are
    // part of the retained string state. The old source stops here;
    // this copied transport receives only zeros while the new pluck starts.
    if (voice.contactTravelEnabled && voice.contactTravel.active)
        voice.tailContactTravel = voice.contactTravel;
    else
        voice.tailContactTravel.active = false;
    // So is the contact noise in flight: the old contact stops making it
    // (and its click, still in the air, with it), and what it already
    // launched travels on to the bridge and the nut in the tail's two planes.
    if (voice.contactNoiseTravel.active)
    {
        voice.tailContactNoiseTravel = voice.contactNoiseTravel;
        voice.tailContactNoiseNormal = voice.contactNoiseNormal;
        voice.tailContactNoiseParallel = voice.contactNoiseParallel;
    }
    else
        voice.tailContactNoiseTravel.active = false;
    voice.contactNoiseTravel.active = false;
    voice.contactNoiseSamples = 0;
    voice.tailCharacteristicImpedance = voice.characteristicImpedance
        * voice.appliedBendImpedanceScale;
    constexpr float tailT60Seconds = 0.010f;
    voice.tailDamping = handDamping(tailT60Seconds, loopFundamental(voice));
    // Keep the old pitch and intrinsic loss when the main string is retuned
    // or aged. Only the heel of the bridge hand is common to both branches:
    // copying its pressure-dependent coefficients once made CC2 stop
    // reaching the retained wave. Save its intended coefficients as well;
    // the copied loop retains any transition still in flight, while lifting
    // pressure restores that prescription rather than an intermediate gain.
    const int stringIndex = static_cast<int>(&voice - voices_.data());
    const auto& physical = physicalCalibration_.steel;
    const float age = parameters_.stringAge;
    const int stoppedMidi = voice.harmonic > 1 ? voice.openMidi : voice.midiNote;
    const float fretT60Factor = clamp(1.0f
        - physicalCalibration_.steelFretT60Slope * voice.speakingFret,
        0.10f, 2.0f);
    voice.tailHandFrequency = static_cast<float>(sampleRate_)
                            / voice.contactPeriodSamples;
    voice.tailHandUnbentFrequency = midiFrequency(stoppedMidi);
    voice.tailHandIntrinsicT60 = 5.4f * (1.0f - 0.12f * age)
                              * fretT60Factor * physical.fundamentalT60Scale;
    voice.tailHandIntrinsicHighLoss = clamp((0.035f + 0.42f * age
        + 0.018f * static_cast<float>(stringCount - 1 - stringIndex))
        * physical.frequencyLossScale, 0.0f, 0.95f);
    voice.tailHandPressure = voice.tailCapturedHandPressure = palmMute_;
    for (int plane = 0; plane < 2; ++plane)
    {
        const auto& loop = voice.loops[static_cast<std::size_t>(plane)];
        voice.tailCapturedLoopGain[static_cast<std::size_t>(plane)]
            = loop.loopGainTransitionSamples > 0 ? loop.targetLoopGain : loop.loopGain;
        voice.tailCapturedHighLoss[static_cast<std::size_t>(plane)] = loop.highLossMix;
    }
    voice.tailLevel = voice.level;
    voice.tailQuietSamples = 0;
    voice.tailActive = true;
    voice.tailRetiring = false;
}

void AcustraEngine::updateTailHandLoss(Voice& voice) noexcept
{
    if (!voice.tailActive || palmMute_ == voice.tailHandPressure)
        return;
    voice.tailHandPressure = palmMute_;
    if (palmMute_ == voice.tailCapturedHandPressure)
    {
        voice.tailLoop.setLoopGain(voice.tailCapturedLoopGain[0]);
        voice.tailParallelLoop.setLoopGain(voice.tailCapturedLoopGain[1]);
        voice.tailLoop.highLossMix = voice.tailCapturedHighLoss[0];
        voice.tailParallelLoop.highLossMix = voice.tailCapturedHighLoss[1];
        return;
    }
    // Same additive loss rates and high/fundamental time ratio as
    // configureVoice. The tail's independent 10 ms repluck damping is kept;
    // its driven bridge response still passes through these live shelves.
    // Redesigning the old delay, dispersion or intrinsic filters here would
    // make its old note follow the newly fretted one.
    const float handRate = palmMute_ > 0.0f
        ? palmMute_ / std::exp(std::log(4.0f)
            + palmMute_ * (std::log(0.080f) - std::log(4.0f)))
        : 0.0f;
    const float highLoss = voice.tailHandIntrinsicHighLoss;
    const float mutedHighLoss = handRate > 0.0f
        ? clamp(1.0f - (1.0f - highLoss) * std::pow(0.001f,
              (1.0f / 0.62f - 1.0f) * handRate
              / std::max(voice.tailHandUnbentFrequency, 1.0f)), 0.0f, 0.95f)
        : highLoss;
    float fundamentalT60 = voice.tailHandIntrinsicT60;
    if (handRate > 0.0f)
        fundamentalT60 = 1.0f / (1.0f / fundamentalT60 + handRate);
    const float frequency = voice.tailHandFrequency;
    const float omega = twoPi * frequency * inverseSampleRate_;
    const float lossOmega = static_cast<float>(referenceLossOmega(omega, sampleRate_));
    const auto& loop = voice.tailLoop;
    const float filterGain = magnitudeForOnePoleMix(
        loop.broadLossCoefficient, loop.broadLossMix, lossOmega)
        * magnitudeForOnePoleMix(loop.lowpassCoefficient, mutedHighLoss, lossOmega)
        * static_cast<float>(bendingLossMagnitude(loop.bendingLossGain,
            loop.bendingLossA1, loop.bendingLossA2, omega));
    const float loopGain = std::pow(0.001f,
        1.0f / std::max(fundamentalT60 * frequency, 1.0f))
        / std::max(filterGain, 0.50f);
    voice.tailLoop.setLoopGain(clamp(loopGain * 0.9995f, 0.70f, 0.999995f));
    voice.tailParallelLoop.setLoopGain(clamp(loopGain * 0.9988f, 0.70f, 0.999995f));
    voice.tailLoop.highLossMix = mutedHighLoss;
    voice.tailParallelLoop.highLossMix = clamp(mutedHighLoss * 1.08f, 0.0f, 1.0f);
}

void AcustraEngine::updateReleaseJoinWindow(Voice& voice) noexcept
{
    // Round up by less than one sample so a MIDI gap rounded from an exact
    // 1/32 note remains eligible. Keep an elapsed-sample limit rather than
    // adding an absolute deadline, including for exceptionally slow tempos.
    long double duration =
        static_cast<long double>(voice.releaseJoinRemainingBeats)
        * (60.0L * sampleRate_) / static_cast<long double>(tempoBpm_);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (duration >= static_cast<long double>(maximum))
    {
        voice.releaseJoinWindowSamples = maximum;
        return;
    }
    // Converting remaining beats back from double must not turn an exact
    // integer duration into an extra sample (e.g. 4425 + 2e-13). Only remove
    // numerical residue near a positive integer: a genuinely sub-sample
    // positive interval still rounds up to one, even at DBL_MAX tempo.
    const long double nearest = std::round(duration);
    const long double tolerance = std::min(0.125L,
        8.0L * std::numeric_limits<double>::epsilon() * std::max(1.0L, duration));
    if (nearest >= 1.0L && std::abs(duration - nearest) <= tolerance)
        duration = nearest;
    const long double samples = std::ceil(duration);
    voice.releaseJoinWindowSamples = static_cast<std::uint64_t>(samples);
}

void AcustraEngine::processPendingRelease(Voice& voice, int stringIndex) noexcept
{
    if (!voice.releaseJoinPending)
        return;
    if (!voice.played || voice.keyDown || voice.pedalHeld)
    {
        voice.releaseJoinPending = false;
        return;
    }
    // MIDI at the inclusive endpoint is handled before rendering it. Only
    // the first later sample begins damping and the optional release noise.
    if (sampleClock_ - voice.releaseJoinAnchorSample > voice.releaseJoinWindowSamples)
        beginRelease(voice, stringIndex);
}

void AcustraEngine::beginRelease(Voice& voice, int stringIndex) noexcept
{
    const bool deferredKeyUp = voice.releaseJoinPending;
    voice.releaseJoinPending = false;
    voice.releaseJoinRemainingBeats = 0.0;
    if (!deferredKeyUp)
        releaseFinger(stringIndex);
    // An explicitly started 2-3 ms finger pulse finishes smoothly under the
    // hand's loss; cutting a nonzero sin^2 pulse here would inject a sharp
    // edge whose slope energy can exceed its original contact budget.
    // Key-up itself never requests a new source.
    voice.pedalHeld = false;
    // A damping release ends the picking contact. Waves already emitted
    // remain in transit and receive the same hand loss on arrival.
    if (voice.contactTravelEnabled)
        voice.excitationEnvelope = 0.0f;
    voice.contactNoiseAmplitude = 0.0f;
    const float nominalReleaseSeconds = voice.fret == 0 ? 1.25f : 0.16f;
    // An explicit fast key-up represents a firm damping contact; a slow
    // one a gentler contact. This is a bounded performance map, not a fit
    // to recordings (the sustained-note corpora carry no release gesture).
    // MIDI's usual/default 64 and a missing velocity preserve the existing
    // hand time exactly. Only loss changes: no refret or new excitation.
    constexpr float nominalReleaseVelocity = 64.0f / 127.0f;
    const float releaseScale = voice.releaseVelocity >= 0.0f
        ? std::exp2(2.0f * (nominalReleaseVelocity
            - clamp(voice.releaseVelocity, 0.0f, 1.0f))) : 1.0f;
    const float releaseSeconds = nominalReleaseSeconds * releaseScale;
    voice.releaseSeconds = releaseSeconds;
    voice.releaseDamping = handDamping(releaseSeconds, loopFundamental(voice));
    voice.returnSamples = static_cast<int>(
        (releaseSeconds + 0.08f) * static_cast<float>(sampleRate_));
    beginGestureDamping(voice);
    if (targetParameters_.releaseNoise > 0.0f)
        startReleaseNoise(voice, stringIndex,
                          voice.fret > 0 && voice.harmonic <= 1);
    voice.releaseVelocity = -1.0f;
}

void AcustraEngine::beginGestureDamping(Voice& voice) noexcept
{
    if (!performanceRealism_.gestureDamping || voice.loops[0].gestureContact.active)
        return;
    // These are authored contact assumptions, not measured releases. The
    // trailing fretting pad touches 18 mm inside the old speaking length;
    // an open string is caught where its picking contact was. The existing
    // scalar hand T60 remains the fundamental release prescription. This
    // bounded extra loss makes partials respond to that contact's location.
    float position = voice.pluckPoint;
    const bool fretContact = voice.fret > 0 && voice.harmonic <= 1;
    if (fretContact)
    {
        // Conventional/manager slides change the physical length beyond
        // the decay table's 0..20-fret range. speakingFret is clamped for
        // that table; the pad must use the actual slide geometry instead.
        const float speakingLength = voice.speakingLengthMetres;
        position = 1.0f - 0.018f / std::max(speakingLength, 0.05f);
    }
    const float strength = clamp((fretContact ? 0.45f : 0.06f)
                                    * (1.0f - voice.releaseDamping),
                                  0.0f, 0.22f);
    // Six milliseconds is an authored soft-contact relaxation time, not
    // a release T60 or a measured guitarist gesture duration.
    const int relaxation = std::max(1, static_cast<int>(std::ceil(0.006 * sampleRate_)));
    for (auto& loop : voice.loops)
        loop.beginGestureContact(position, strength, relaxation);
}

// A plectrum's release. The rest displacement is Smith's opposed half-height
// waves (PASP App. C.3.2), each half of the folded line the smoothed
// triangle read at its own bridge fraction, so displacementAt returns the
// triangle itself and the velocity is zero. The release velocity is the
// same integrated step on both halves, which unfolds to a hump of velocity
// over the contact width - the string the tip was carrying - with no
// displacement of its own. In a lossless line every sample-to-sample
// difference carries T/dx times its square of energy whichever wave it
// belongs to, and the two waves' energies add without a cross term, so the
// hump's height is set from the two components' summed squared differences
// alone: no tension, length or unit enters the share. The two components'
// partials sit in quadrature (cosine and sine phases at release), so their
// powers add and the hump's sign is immaterial.
// A plectrum does not let the string go at an instant: the string slides
// round the rounded edge of the tip, and while it does the force the tip
// holds falls from F0 to nothing. The pluck point answers a change dF in
// that force at once with a velocity dF/(2Z): each half of the string
// presents its characteristic impedance Z = T/c = sqrt(T mu) until the first
// reflection returns (Fletcher and Rossing, The Physics of Musical
// Instruments, 2nd ed. 1998, ch. 2). Linearising the edge's hold as falling
// in proportion to how far round it the string has moved, F = F0 (1 - y/r)
// for an edge of radius r, the string's own motion carries it off,
// dy/dt = F0 y / (2 Z r), so the force unloads as e^(t/tau) with
//     tau = 2 Z r / F0 = r / u,   u = F0 / (2Z) = (c/2) y0 (1/a + 1/(L - a)),
// u being the speed an instantly released pluck point starts with: y0 the
// held displacement, a its distance from the bridge, L the speaking length.
// Every partial of the released string is then the instant release's times
// 1/(1 - j omega tau), the transform of that one-sided exponential ending at
// release: a first-order low-pass in absolute frequency, with no zero to
// invert a partial, whose corner rises with the force the tip held. A hard
// stroke is let go fast and bright, a soft one slowly and dark, which is the
// velocity-to-brightness law the picked archtop rows show and a pluck with
// a fixed contact width cannot make (Docs/decisions.md, 2026-09-27). The
// pick's own speed would add to u; it is not measured for single notes and
// adding the strum map's 0.51-2.46 m/s read worse on both splits, so r is
// fitted with it absent and absorbs it. Returns the one-pole's pole, or 0.
double AcustraEngine::plectrumSlipPole(const Voice& voice,
                                       float releasedAmplitude,
                                       float heldDistance,
                                       float soundingLength,
                                       float scaleLength,
                                       float edgeRadius) const noexcept
{
    if (!(edgeRadius > 0.0f))
        return 0.0;
    // c = 2 L0 f0 of the open string: fretting shortens the string, not the
    // wave speed.
    const float unbentWaveSpeed = 2.0f * scaleLength * midiFrequency(voice.openMidi);
    // A member's lateral bend raises wave speed at the same stopped length.
    // This is the same sqrt(T/mu) ratio already used by the junction, with
    // stretched mass included; a slide leaves the ratio exactly one.
    const float waveSpeed = voice.bendImpedanceScale == 1.0f
        ? unbentWaveSpeed : unbentWaveSpeed * voice.bendImpedanceScale;
    const float heldMetres = exact::abs(releasedAmplitude)
        * std::max(physicalCalibration_.steelDisplacementScaleMetres, 1.0e-4f);
    const float a = clamp(heldDistance, 1.0e-3f, 0.999f * soundingLength);
    const float releaseSpeed = 0.5f * waveSpeed * heldMetres
        * (1.0f / a + 1.0f / (soundingLength - a));
    if (!(releaseSpeed > 0.0f))
        return 0.0;
    const double tauSamples = static_cast<double>(edgeRadius / releaseSpeed)
        * static_cast<double>(sampleRate_);
    if (!(tauSamples > 1.0e-3) || !exact::isfinite(tauSamples))
        return 0.0;
    return std::exp(-1.0 / tauSamples);
}

// The written loop holds one period of the released waves, read oldest
// first, so a later output sits at a lower sample index. The anticausal
// 1/(1 - j omega tau) is then y[s] = (1-b) x[s] + b y[s-1] run upward over
// the period from its periodic steady state, exact per loop harmonic. The
// sample at the bridge is re-zeroed afterwards, as the pluck itself is: the
// loop's loss and dispersion states start empty, which only agrees with a
// line that starts at rest there (smoothing the corner across that point and
// leaving it raised put a broadband click into every note).
// slipPeriod runs it over any one-period line, at(0) the bridge sample: the
// loop here, and writePickRelease's 48 kHz reference line.
namespace
{
template <typename At>
void slipPeriod(const At& at, int length, double slipPole) noexcept
{
    const double b = slipPole;
    double state = 0.0;
    double weight = 1.0;
    for (int k = 0; k < length; ++k)
    {
        state += weight * static_cast<double>(at(length - 1 - k));
        weight *= b;
    }
    state *= (1.0 - b) / (1.0 - weight);
    double first = 0.0;
    for (int sample = 0; sample < length; ++sample)
    {
        state = (1.0 - b) * static_cast<double>(at(sample)) + b * state;
        if (sample == 0)
            first = state;
        at(sample) = static_cast<float>(state - first);
    }
}
} // namespace

void AcustraEngine::applyPlectrumSlip(StringLoop& loop, int length,
                                      double slipPole) noexcept
{
    if (!(slipPole > 0.0))
        return;
    slipPeriod([&loop] (int sample) -> float&
    {
        return loop.delay[static_cast<std::size_t>(
            maximumDelaySamples - sample - 1)];
    }, length, slipPole);
}

void AcustraEngine::writePickRelease(StringLoop& loop, int length, float height,
                                     float position, float aperture, int modes,
                                     float releaseShare, double slipPole,
                                     float referenceDelay) noexcept
{
    // As the shape's own bound (initialisePluck): p of the written line.
    // The reference grid below moves it (setApex) and puts it back.
    float p = clamp(position, 0.05f, 0.60f);
    double apex = static_cast<double>(p);
    const auto bridgeFraction = [] (double phase)
    {
        phase -= exact::floor(phase);
        return phase < 0.5 ? 2.0 * phase : 2.0 * (1.0 - phase);
    };
    const auto displacementWave = [&] (double phase)
    {
        const double wrapped = phase - exact::floor(phase);
        const double fraction = bridgeFraction(wrapped);
        const double triangle = fraction < apex ? fraction / apex
                                                : (1.0 - fraction) / (1.0 - apex);
        return (wrapped < 0.5 ? -0.5 : 0.5) * triangle;
    };
    // The same continuous Gaussian contact initialisePluck's shape uses,
    // sigma the aperture in loop phase, applied exactly to both components.
    // The rest wave is piecewise linear with its two slope changes at the
    // folded apex, p/2 and 1-p/2, so smoothing adds sigma*q(|z|/sigma) there
    // (q the tabulated corner function). The velocity wave is the unit box
    // between those two points, whose smoothing is the difference of two
    // Gaussian edges; taking the whole box rather than a sharp box plus a
    // correction leaves nothing to decide at a sample landing exactly on an
    // edge, which the first sample always does. Periodic images within the
    // kernel's reach are summed. The node projection for a natural harmonic
    // follows; all of it is linear, so both components are treated alike.
    const double sigma = std::max(static_cast<double>(aperture), 1.0e-9);
    double cornerA = 0.5 * apex;
    double cornerB = 1.0 - 0.5 * apex;
    double slopeChange = 1.0 / (apex * (1.0 - apex));
    const auto setApex = [&] (float share)
    {
        p = share;
        apex = static_cast<double>(p);
        cornerA = 0.5 * apex;
        cornerB = 1.0 - 0.5 * apex;
        slopeChange = 1.0 / (apex * (1.0 - apex));
    };
    const auto unitCorner = [] (double z)
    {
        if (z >= 10.0)
            return 0.0;
        const double scaled = z * 64.0;
        const int index = static_cast<int>(scaled);
        const double t = scaled - index;
        const auto& a = gaussianAperture::cornerTable[index];
        const auto& b = gaussianAperture::cornerTable[index + 1];
        const double da = a[1] / 64.0;
        const double db = b[1] / 64.0;
        const double difference = b[0] - a[0];
        return a[0] + t * (da + t * (3.0 * difference - 2.0 * da - db
            + t * (-2.0 * difference + da + db)));
    };
    const auto corner = [&] (double z)
    {
        return sigma * unitCorner(exact::abs(z) / sigma);
    };
    const auto step = [&] (double z)
    {
        return 0.5 * std::erfc(-z / (sigma * sqrt2Double));
    };
    const auto images = [] (double z, const auto& kernel)
    {
        const double wrapped = z - exact::floor(z + 0.5);
        double sum = 0.0;
        for (int image = -2; image <= 2; ++image)
            sum += kernel(wrapped + static_cast<double>(image));
        return sum;
    };
    const auto smoothedDisplacement = [&] (double phase)
    {
        return displacementWave(phase) + slopeChange
            * (images(phase - cornerA, corner) - images(phase - cornerB, corner));
    };
    const auto smoothedVelocity = [&] (double phase)
    {
        const double wrapped = phase - exact::floor(phase);
        double sum = 0.0;
        for (int image = -2; image <= 2; ++image)
            sum += step(wrapped - cornerA + static_cast<double>(image))
                 - step(wrapped - cornerB + static_cast<double>(image));
        return sum;
    };
    const auto released = [&] (const auto& smoothed, float phase)
    {
        if (modes <= 1)
            return static_cast<float>(smoothed(static_cast<double>(phase)));
        double sum = 0.0;
        for (int shift = 0; shift < modes; ++shift)
            sum += smoothed(static_cast<double>(phase)
                            + static_cast<double>(shift) / static_cast<double>(modes));
        return static_cast<float>(sum / static_cast<double>(modes));
    };
    // Phases are read in the fitted frame below, advanced by half the apex
    // phase; the energies are summed on that same sampled grid, because a
    // step smoothed over less than a sample lands on one difference or two
    // depending on where the grid falls, and the share must describe what
    // is written.
    const auto phaseOn = [&p] (int grid, int sample)
    {
        return static_cast<float>(sample - 1) / static_cast<float>(grid)
             - 0.5f * p;
    };
    const auto phaseOf = [&] (int sample)
    {
        return phaseOn(length, sample);
    };
    struct GridEnergies
    {
        double displacement { 0.0 };
        double velocity { 0.0 };
        double cross { 0.0 };
    };
    // The two waves' summed squared differences and cross term over one
    // period of `grid` samples, the waves kept for a later pass if asked.
    const auto energiesOn = [&] (int grid, bool keep)
    {
        GridEnergies energies;
        float previousDisplacement = released(smoothedDisplacement,
                                              phaseOn(grid, grid));
        float previousVelocity = released(smoothedVelocity, phaseOn(grid, grid));
        for (int sample = 1; sample <= grid; ++sample)
        {
            const float displacement = released(smoothedDisplacement,
                                                phaseOn(grid, sample));
            const float velocity = released(smoothedVelocity, phaseOn(grid, sample));
            if (keep)
            {
                pickReleaseDisplacement_[static_cast<std::size_t>(sample - 1)] = displacement;
                pickReleaseVelocity_[static_cast<std::size_t>(sample - 1)] = velocity;
            }
            const double displacementStep = displacement - previousDisplacement;
            const double velocityStep = velocity - previousVelocity;
            energies.displacement += displacementStep * displacementStep;
            energies.velocity += velocityStep * velocityStep;
            energies.cross += displacementStep * velocityStep;
            previousDisplacement = displacement;
            previousVelocity = velocity;
        }
        return energies;
    };
    // The slipped displacement's energy and its cross term with the
    // velocity, on a grid whose slipped line (per unit height) and velocity
    // are read at each sample.
    const auto slippedOn = [] (int grid, const auto& lineAt,
                               const auto& velocityAt)
    {
        GridEnergies energies;
        double previousLine = lineAt(grid);
        float previousVelocity = velocityAt(grid);
        for (int sample = 1; sample <= grid; ++sample)
        {
            const double line = lineAt(sample);
            const float velocity = velocityAt(sample);
            const double lineStep = line - previousLine;
            const double velocityStep = velocity - previousVelocity;
            energies.displacement += lineStep * lineStep;
            energies.cross += lineStep * velocityStep;
            previousLine = line;
            previousVelocity = velocity;
        }
        return energies;
    };
    // The fitted level law describes the displacement the tip leaves behind;
    // the velocity it also leaves is energy on top of that. Redistributing
    // one fitted energy between the two instead was tried and read worse on
    // both splits: the hump's energy sits in partials that decay fast, so the
    // sustained level then rose too little with velocity for the recordings.
    // In the continuum the two waves' energies add with no cross term, but on
    // the grid the step's smoothed spike sits on the apex kink, whose slope
    // jump it samples at different offsets on the two halves, so the cross
    // term is kept and the hump solved for exactly: h^2 V + 2 h X = share D.
    // Of its two roots the one that vanishes with the share is taken, in
    // the form that stays stable when X dominates; its sign follows X's,
    // which the string does not hear (the components are in quadrature).
    const auto solvedHump = [] (double added, double cross, double velocity)
    {
        const double magnitude = added
            / (exact::abs(cross) + exact::sqrt(cross * cross + added * velocity));
        return cross < 0.0 ? -magnitude : magnitude;
    };
    // The share is solved on the 48 kHz grid at every host rate. The waves
    // are continuous, so the hump's height over the rest's is a property of
    // the shape, not of the grid it is written on; but the summed squared
    // differences are the grid's. The rest's are its slope's, which fall as
    // 1/length; the velocity's steps are smoothed over the contact, a
    // fraction of a 48 kHz sample to a few, so on a finer grid they spread
    // over more differences and V falls more slowly than D, or not at all
    // where the step stays sharper than a sample. Solved on the host grid
    // the same share set a lower hump the higher the rate: its 0-6 kHz
    // partials over the rest's, on the written line at MIDI 40-84,
    // velocity 0.2-1 and Touch 0.1-1, sat 0.1-4.8 dB below 48 kHz's at
    // 96 kHz and 0.2-7.6 dB below at 192 kHz, and up to 0.95 dB above at
    // 44.1 kHz. The share was chosen by ear at 48 kHz, so the solve reads
    // the same waves at the samples of this note's 48 kHz period
    // (configureVoice's referencePickDelay), and slips them at the edge's
    // 48 kHz pole, the same release time in 48 kHz samples; the host grid
    // then writes that height, and those partials agree with 48 kHz's to
    // 0.1 dB at 44.1-192 kHz. The period has to be 48 kHz's own, not this
    // one rescaled: the sub-sample landing of the step against the apex
    // kink moves X, and a period one sample off moved the hump by up to
    // 1.7 dB. At 48 kHz the solve is the one on the written grid, as it was.
    const float rest = height;
    const bool referenceGrid = sampleRate_ != 48000.0;
    bool referenceSolved = false;
    double referenceHump = 0.0;
    if (referenceGrid)
    {
        // The kept arrays serve as the reference's scratch: the host pass
        // below rewrites them before the write reads them. A pluck not
        // tuned for a pick (the technique changed while it waited) takes
        // this period rescaled.
        const double periodAt48k = referenceDelay > 0.0f
            ? static_cast<double>(referenceDelay)
            : static_cast<double>(loop.targetDelay) * 48000.0 / sampleRate_;
        const int grid = std::clamp(
            static_cast<int>(std::round(periodAt48k)),
            8, std::min(maximumDelaySamples - 3,
                        static_cast<int>(pickReleaseDisplacement_.size())));
        const double referencePole = slipPole > 0.0
            ? std::pow(slipPole, sampleRate_ / 48000.0) : 0.0;
        // The kink is a share of the line it is written on, which is the
        // string's period over that line's rounded length (initialisePluck);
        // the same point on the string is its own share of the 48 kHz line.
        const float hostShare = p;
        setApex(clamp(static_cast<float>(static_cast<double>(hostShare) * length
            * (48000.0 / sampleRate_) / grid), 0.05f, 0.60f));
        const auto reference = energiesOn(grid, true);
        referenceSolved = true;
        if (reference.velocity > 0.0)
        {
            const double share = static_cast<double>(releaseShare);
            if (referencePole > 0.0)
            {
                // The rest line per unit height, from zero at the bridge,
                // slipped as applyPlectrumSlip slips the loop.
                const float first = pickReleaseDisplacement_[0];
                for (int sample = 0; sample < grid; ++sample)
                    pickReleaseDisplacement_[static_cast<std::size_t>(sample)] -= first;
                slipPeriod([this] (int sample) -> float&
                {
                    return pickReleaseDisplacement_[static_cast<std::size_t>(sample)];
                }, grid, referencePole);
                const auto slipped = slippedOn(grid,
                    [this] (int sample)
                    {
                        return static_cast<double>(pickReleaseDisplacement_[
                            static_cast<std::size_t>(sample - 1)]);
                    },
                    [this] (int sample)
                    {
                        return pickReleaseVelocity_[static_cast<std::size_t>(sample - 1)];
                    });
                referenceHump = solvedHump(share * slipped.displacement,
                                           slipped.cross, reference.velocity);
            }
            else
                referenceHump = solvedHump(share * reference.displacement,
                                           reference.cross, reference.velocity);
        }
    }

    if (referenceSolved)
        setApex(clamp(position, 0.05f, 0.60f));

    // Kept for the write pass below, which reads the same phases.
    const bool kept = length >= 1
        && length <= static_cast<int>(pickReleaseDisplacement_.size());
    const auto host = energiesOn(length, kept);
    float hump = 0.0f;
    if (referenceSolved)
        hump = rest * static_cast<float>(referenceHump);
    else if (host.velocity > 0.0)
        hump = rest * static_cast<float>(solvedHump(
            static_cast<double>(releaseShare) * host.displacement,
            host.cross, host.velocity));

    // The plucked shape every calibration was fitted with is this rest state
    // advanced by half the apex phase and negated: initialisePluck's
    // tri_p(phase) equals -rest(phase - p/2) + 1/2 (partial magnitudes agree
    // to 1e-3 dB and phases differ by exactly -pi*p*n). The initial modal
    // phases are not free here - a rest-frame pluck with the same magnitudes
    // moved the archtop harmonics term from 8.6 to 12.8, because the idle
    // strings, bridge and body at coinciding partials interfere with the
    // string according to the phase it starts with - so both components are
    // written in that fitted frame, which keeps their quadrature intact, and
    // the line starts at zero at the bridge as the legacy shape does.
    // The released waves at a sample of the loop, as the energy pass formed
    // them.
    const auto displacementAt = [&] (int sample)
    {
        return kept ? pickReleaseDisplacement_[static_cast<std::size_t>(sample - 1)]
                    : released(smoothedDisplacement, phaseOf(sample));
    };
    const auto velocityAt = [&] (int sample)
    {
        return kept ? pickReleaseVelocity_[static_cast<std::size_t>(sample - 1)]
                    : released(smoothedVelocity, phaseOf(sample));
    };
    const auto frame = [&] (int sample)
    {
        return -(rest * displacementAt(sample) + hump * velocityAt(sample));
    };
    if (!(slipPole > 0.0))
    {
        const float endpoint = frame(1);
        for (int sample = 1; sample <= length; ++sample)
            loop.delay[static_cast<std::size_t>(wrapDelayIndex(
                loop.writeIndex - sample))] = frame(sample) - endpoint;
        return;
    }
    const auto restFrame = [&] (int sample)
    {
        return -rest * displacementAt(sample);
    };
    const auto humpFrame = [&] (int sample)
    {
        return -hump * velocityAt(sample);
    };
    const float restEndpoint = restFrame(1);
    for (int sample = 1; sample <= length; ++sample)
        loop.delay[static_cast<std::size_t>(wrapDelayIndex(
            loop.writeIndex - sample))] = restFrame(sample) - restEndpoint;
    applyPlectrumSlip(loop, length, slipPole);
    // The displacement the tip leaves behind is now the slipped one, so the
    // share is of its energy and the cross term is read against it: the
    // same exact solve on what is written.
    // At another rate the reference solve above already read the slip.
    if (!referenceSolved && host.velocity > 0.0 && rest != 0.0f)
    {
        const auto lineAt = [&] (int sample)
        {
            return static_cast<double>(loop.delay[static_cast<std::size_t>(
                wrapDelayIndex(loop.writeIndex - sample))])
                / static_cast<double>(-rest);
        };
        const auto slipped = slippedOn(length, lineAt, velocityAt);
        hump = rest * static_cast<float>(solvedHump(
            static_cast<double>(releaseShare) * slipped.displacement,
            slipped.cross, host.velocity));
    }
    const float humpEndpoint = humpFrame(1);
    for (int sample = 1; sample <= length; ++sample)
        loop.delay[static_cast<std::size_t>(wrapDelayIndex(
            loop.writeIndex - sample))] += humpFrame(sample) - humpEndpoint;
}

void AcustraEngine::freezeMemberPitchBend(Voice& voice) noexcept
{
    if (!voice.mpeMember || voice.memberPitchBendFrozen)
        return;
    voice.frozenMemberPitchBendSemitones = pitchBendSemitones_[
        static_cast<std::size_t>(voice.midiChannel - 1)];
    voice.memberPitchBendFrozen = true;
}

bool AcustraEngine::isLowerZoneMaster(int midiChannel) const noexcept
{
    return lowerZoneMemberCount_ > 0 && midiChannel == 1;
}

bool AcustraEngine::isLowerZoneMember(int midiChannel) const noexcept
{
    return lowerZoneMemberCount_ > 0 && midiChannel >= 2
        && midiChannel <= lowerZoneMemberCount_ + 1;
}

bool AcustraEngine::channelControlsVoice(int midiChannel,
                                         const Voice& voice) const noexcept
{
    if (isLowerZoneMaster(midiChannel))
        return voice.midiChannel == 1 || voice.mpeMember;
    return voice.midiChannel == midiChannel;
}

bool AcustraEngine::sustainIsDown(const Voice& voice) const noexcept
{
    if (voice.midiChannel < 1 || voice.midiChannel > midiChannelCount)
        return false;
    const bool own = sustainPedals_[static_cast<std::size_t>(
        voice.midiChannel - 1)];
    return voice.mpeMember ? sustainPedals_[0] || own : own;
}

// The fretting hand. A guitarist fretting a note keeps the hand where it
// is if a finger reaches, takes an open string if one sounds the note, and
// otherwise reaches low on the neck (the listener's direction for Acustra:
// "imagine a player's hand on the frets that are currently playing or were
// playing previously - try to fit a fret that a finger would reach. then,
// prefer empty strings, then prefer lower frets"). Each string remembers
// the last fretted note it sounded (hand_); held notes, key or pedal, weigh
// 1, and released ones exp(-age / handMemoryTimeConstantSeconds) until
// handMemorySeconds, when they are forgotten.
AcustraEngine::HandWeights AcustraEngine::handWeights() const noexcept
{
    HandWeights weights {};
    const double memory = static_cast<double>(handMemorySeconds) * sampleRate_;
    const double timeConstant
        = static_cast<double>(handMemoryTimeConstantSeconds) * sampleRate_;
    for (int string = 0; string < stringCount; ++string)
    {
        const auto& finger = hand_[static_cast<std::size_t>(string)];
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        float weight = 0.0f;
        if (finger.valid)
        {
            if (voice.played && (voice.keyDown || voice.pedalHeld)
                && voice.harmonic == 1 && voice.fret == finger.fret)
                weight = 1.0f;
            else
            {
                const double age = static_cast<double>(
                    sampleClock_ - std::min(finger.heldAt, sampleClock_));
                if (age <= memory)
                    weight = static_cast<float>(std::exp(-age / timeConstant));
            }
        }
        weights[static_cast<std::size_t>(string)] = weight;
    }
    return weights;
}

bool AcustraEngine::handKnown(const HandWeights& weights) noexcept
{
    return std::any_of(weights.begin(), weights.end(),
                       [] (float weight) { return weight > 0.0f; });
}

// How far a set of frets is from one hand. shapeFrets names the fret each
// string would sound (-1: not part of the shape). The frets the hand must
// hold are the shape's fretted notes and every other string whose key is
// down (movableStrings are chord members about to be re-placed, so they do
// not count); what it remembers are the other strings' released or pedal-
// held fingers, at their weights. The hand is an index finger at fret p
// covering p..p+handPositionSpan; a held fret one outside it is a stretch
// (handStretchCost a fret), a remembered one outside it costs its weight
// per fret the hand would have to move. The cost is the best p's; frets
// the hand must hold that no one hand can span are impossibleShapeCost
// plus their spread. Zero is a shape the hand reaches where it is.
float AcustraEngine::shapeCost(const StringFrets& shapeFrets,
                               unsigned movableStrings,
                               const HandWeights& weights) const noexcept
{
    std::array<int, 2 * stringCount> held {};
    StringFrets fingerFrets {};
    fingerFrets.fill(-1);
    int heldCount = 0;
    std::array<int, stringCount> remembered {};
    std::array<float, stringCount> rememberedWeight {};
    int rememberedCount = 0;
    int low = fretCount + 1;
    int high = -1;
    const auto hold = [&] (int fret)
    {
        held[static_cast<std::size_t>(heldCount++)] = fret;
        low = std::min(low, fret);
        high = std::max(high, fret);
    };
    for (int string = 0; string < stringCount; ++string)
    {
        const auto index = static_cast<std::size_t>(string);
        const auto& voice = voices_[index];
        const bool movable = ((movableStrings >> string) & 1u) != 0u;
        const bool keyed = voice.played && voice.keyDown && !movable;
        // Open strings need no finger, but they prevent a lower barre from
        // spanning across them. Keep them alongside the stopped strings.
        if (shapeFrets[index] >= 0)
            fingerFrets[index] = shapeFrets[index];
        else if (keyed && voice.harmonic == 1)
            fingerFrets[index] = voice.fret;
        if (fingerFrets[index] >= 1)
            hold(fingerFrets[index]);
        if (movable || (voice.played && voice.keyDown))
            continue;
        if (weights[index] > 0.0f)
        {
            remembered[static_cast<std::size_t>(rememberedCount)]
                = hand_[index].fret;
            rememberedWeight[static_cast<std::size_t>(rememberedCount)]
                = weights[index];
            ++rememberedCount;
        }
    }
    if (heldCount == 0 && rememberedCount == 0)
        return 0.0f;
    if (heldCount > 0 && high - low > handStretchSpan)
        return impossibleShapeCost + static_cast<float>(high - low);
    // A narrow fret span alone does not establish that four fretting
    // fingers can hold it. Equal-fret stops can share a barre only when no
    // intervening played string must remain open or stop below that fret.
    // Higher stops can sit above the barre; unused strings may be muted.
    // This is a necessary conventional four-finger grip constraint, not a
    // full hand/contact solver (thumb-over and exceptional grips are outside
    // this model). An impossible grip stays a scored fallback, never a drop.
    int fingers = 0;
    for (int fret = low; fret <= high; ++fret)
    {
        bool barre = false;
        for (const int stop : fingerFrets)
        {
            if (stop >= 0 && stop < fret)
                barre = false;
            else if (stop == fret)
            {
                if (!barre)
                    ++fingers;
                barre = true;
            }
        }
    }
    if (fingers > 4)
        return impossibleShapeCost + static_cast<float>(fingers);
    int firstPosition = 1;
    int lastPosition = fretCount - handPositionSpan;
    if (heldCount > 0)
    {
        // Every held fret within one fret of the position.
        firstPosition = std::max(firstPosition, high - handPositionSpan - 1);
        lastPosition = std::min(lastPosition, low + 1);
    }
    const auto outside = [] (int fret, int position)
    {
        return fret < position ? position - fret
            : fret > position + handPositionSpan
                ? fret - position - handPositionSpan : 0;
    };
    float best = std::numeric_limits<float>::max();
    for (int position = firstPosition; position <= lastPosition; ++position)
    {
        float cost = 0.0f;
        for (int index = 0; index < heldCount; ++index)
            cost += handStretchCost * static_cast<float>(
                outside(held[static_cast<std::size_t>(index)], position));
        for (int index = 0; index < rememberedCount; ++index)
            cost += rememberedWeight[static_cast<std::size_t>(index)]
                * static_cast<float>(outside(
                    remembered[static_cast<std::size_t>(index)], position));
        best = std::min(best, cost);
    }
    return best == std::numeric_limits<float>::max()
        ? impossibleShapeCost : best;
}

int AcustraEngine::chooseString(int midiNote) const noexcept
{
    // A note repeated after its key came up is replucked on the string still
    // sounding it, as a guitarist does, rather than hopping to whichever free
    // string can also reach it and leaving the first one ringing. A local
    // release contact keeps that physical string occupied until its existing
    // hand-back deadline, even when the level estimate crosses the quiet floor.
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && !voice.keyDown && voice.harmonic == 1
            && voice.midiNote == midiNote
            && (voice.level > 2.0e-7f
                || voice.loops[0].gestureContact.active
                || voice.loops[1].gestureContact.active))
            return string;
    }
    const auto weights = handWeights();
    if (!handKnown(weights))
        return chooseStringWithoutHand(midiNote);

    // With a hand on the neck, in order: never a string whose key is still
    // down while another can sound the note; then the fret the hand reaches
    // from where it is (shapeCost zero; otherwise the fewest frets moved,
    // weighted by how recently the hand was there); then an open string;
    // then the lower fret; then a silent string over one still ringing, and
    // the one that has rung longest.
    constexpr float tie = 1.0e-4f;
    int best = -1;
    bool bestSteal = true;
    float bestCost = 0.0f;
    bool bestOpen = false;
    int bestFret = fretCount + 1;
    bool bestRinging = true;
    std::uint64_t bestOrder = 0;
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        const int fret = midiNote - voice.openMidi;
        if (fret < 0 || fret > fretCount)
            continue;
        StringFrets frets {};
        frets.fill(-1);
        frets[static_cast<std::size_t>(string)] = fret;
        const bool steal = voice.played && voice.keyDown;
        const float cost = shapeCost(frets, 0u, weights);
        const bool open = fret == 0;
        const bool ringing = voice.played;
        bool better = best < 0;
        if (!better && steal != bestSteal)
            better = !steal;
        else if (!better && exact::abs(cost - bestCost) > tie)
            better = cost < bestCost;
        else if (!better && open != bestOpen)
            better = open;
        else if (!better && fret != bestFret)
            better = fret < bestFret;
        else if (!better && ringing != bestRinging)
            better = !ringing;
        else if (!better)
            better = voice.startOrder < bestOrder;
        if (better)
        {
            best = string;
            bestSteal = steal;
            bestCost = cost;
            bestOpen = open;
            bestFret = fret;
            bestRinging = ringing;
            bestOrder = voice.startOrder;
        }
    }
    return best;
}

// The allocator with no hand on the neck (a fresh engine, or two seconds
// with nothing fretted): the free string with the lowest fret, else the
// string released longest ago, else the oldest. A lone note is placed
// exactly as it always was. A repeated note is replucked where it rings,
// as with a hand.
int AcustraEngine::chooseStringWithoutHand(int midiNote) const noexcept
{
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && !voice.keyDown && voice.harmonic == 1
            && voice.midiNote == midiNote
            && (voice.level > 2.0e-7f
                || voice.loops[0].gestureContact.active
                || voice.loops[1].gestureContact.active))
            return string;
    }
    int best = -1;
    int bestFret = fretCount + 1;
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        const int fret = midiNote - voice.openMidi;
        if (fret < 0 || fret > fretCount)
            continue;
        if (!voice.played && fret < bestFret)
        {
            best = string;
            bestFret = fret;
        }
    }
    if (best >= 0)
        return best;

    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        const int fret = midiNote - voice.openMidi;
        if (fret < 0 || fret > fretCount)
            continue;
        if (!voice.keyDown && voice.startOrder < oldest)
        {
            best = string;
            oldest = voice.startOrder;
        }
    }
    if (best >= 0)
        return best;

    oldest = std::numeric_limits<std::uint64_t>::max();
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        const int fret = midiNote - voice.openMidi;
        if (fret >= 0 && fret <= fretCount && voice.startOrder < oldest)
        {
            best = string;
            oldest = voice.startOrder;
        }
    }
    return best;
}

AcustraEngine::HarmonicChoice
AcustraEngine::chooseHarmonic(int midiNote) const noexcept
{
    // A natural harmonic sounds at exactly n times a string's open pitch, so
    // the requested note itself says whether the guitar can produce it. No
    // keyswitch is needed and none is offered. Harmonics 2 to 6 and 8 fall
    // within 25 cents of equal temperament; the seventh is 31 cents flat,
    // which is why players avoid it against tempered material, and the same
    // tolerance excludes it here. Nodes above the eighth are impractical to
    // touch and very quiet, so the search stops there.
    constexpr int highestHarmonic = 8;
    constexpr float toleranceCents = 25.0f;
    const float wanted = midiFrequency(midiNote);
    // Re-strike a released harmonic on the string still ringing it, just
    // as chooseString does for a fretted note. Moving to an unused string
    // would leave the first vibration sounding alongside the new attack.
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && !voice.keyDown && voice.harmonic > 1
            && voice.midiNote == midiNote
            && (voice.level > 2.0e-7f
                || voice.loops[0].gestureContact.active
                || voice.loops[1].gestureContact.active)
            // Tuning automation can move the ringing harmonic away from
            // its requested MIDI pitch. It must still reach this note.
            && exact::abs(1200.0f * std::log2(midiFrequency(voice.openMidi)
                * static_cast<float>(voice.harmonic) / wanted)) <= toleranceCents)
            return { string, voice.harmonic };
    }
    HarmonicChoice best {};
    int bestAvailability = 0;
    for (int string = stringCount - 1; string >= 0; --string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        const float open = midiFrequency(voice.openMidi);
        for (int harmonic = 2; harmonic <= highestHarmonic; ++harmonic)
        {
            const float sounding = open * static_cast<float>(harmonic);
            if (exact::abs(1200.0f * std::log2(sounding / wanted))
                > toleranceCents)
                continue;
            // Prefer an unused string, then one whose key is up, before
            // taking a held note. Within that choice the lowest node is
            // the loudest and the one a player reaches for first.
            const int availability = !voice.played ? 0 : !voice.keyDown ? 1 : 2;
            if (best.string < 0 || availability < bestAvailability
                || (availability == bestAvailability && harmonic < best.harmonic))
            {
                best = { string, harmonic };
                bestAvailability = availability;
            }
            break;
        }
    }
    return best;
}

bool AcustraEngine::canSound(int midiNote, int midiChannel) const noexcept
{
    if (!prepared_ || midiNote < 0 || midiNote > 127
        || midiChannel < 1 || midiChannel > midiChannelCount)
        return false;
    // The drops noteOn makes, which depend on the tuning alone: a
    // string-per-channel note its own string cannot fret, and a note no
    // string frets and no natural harmonic reaches.
    const auto frets = [&] (int string)
    {
        const int fret = midiNote
            - voices_[static_cast<std::size_t>(string)].openMidi;
        return fret >= 0 && fret <= fretCount;
    };
    if (stringPerChannelMode_ && midiChannel <= stringCount)
        return frets(midiChannel - 1);
    for (int string = 0; string < stringCount; ++string)
        if (frets(string))
            return true;
    return chooseHarmonic(midiNote).string >= 0;
}

void AcustraEngine::noteOn(int midiNote, float velocity, int midiChannel,
                           int pluckDelaySamples, bool strumMember) noexcept
{
    if (!prepared_ || midiNote < 0 || midiNote > 127
        || !exact::isfinite(velocity) || velocity <= 0.0f
        || midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    if (!canSound(midiNote, midiChannel))
        return;

    // The scheduled delay is bounded before anything scales or counts it:
    // ten seconds is far beyond any strum (at most about 0.12 s), and at the
    // highest rate it times the largest speed draw plus the countdown's one
    // stays far inside an int, so no caller's value can overflow.
    const int maximumPluckDelay = static_cast<int>(10.0 * sampleRate_);
    int delaySamples = std::clamp(pluckDelaySamples, 0, maximumPluckDelay);
    if (strumMember)
    {
        // One speed draw belongs to the entire stroke, including strings
        // already held from the preceding stroke (see beginStrum).
        delaySamples = std::max(0, static_cast<int>(
            std::round(static_cast<float>(delaySamples) * strumSpeedScale_)));
    }

    // A chord still forming is a run of one channel's onsets each within the
    // chord window of the one before it; another channel's notes are
    // another hand's and neither open nor extend it.
    const auto chordWindow = static_cast<std::uint64_t>(
        static_cast<double>(chordWindowSeconds) * sampleRate_);
    const auto channelIndex = static_cast<std::size_t>(midiChannel - 1);
    if (!noteOnSeen_[channelIndex]
        || sampleClock_ - lastNoteOnSample_[channelIndex] > chordWindow)
        chordStartSample_[channelIndex] = sampleClock_;
    lastNoteOnSample_[channelIndex] = sampleClock_;
    noteOnSeen_[channelIndex] = true;

    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        processPendingRelease(voice, string);
        if (voice.played && !voice.keyDown && voice.midiNote == midiNote
            && voice.midiChannel == midiChannel
            && (voice.releaseJoinPending || voice.releaseAfterPluck))
        {
            const bool controllerString = stringPerChannelMode_
                && midiChannel <= stringCount;
            const int fret = midiNote - voice.openMidi;
            const bool placementValid = (!controllerString || string == midiChannel - 1)
                && (voice.harmonic == 1 ? fret >= 0 && fret <= fretCount
                    : !controllerString && exact::abs(1200.0f * std::log2(
                        midiFrequency(voice.openMidi) * static_cast<float>(voice.harmonic)
                        / midiFrequency(midiNote))) <= 25.0f);
            if (!placementValid)
                continue;
            // The released MIDI owner is gone, but this is still its
            // physical string. Cancel that key-up before taking a new owner.
            startNote(string, voice.harmonic, midiNote, velocity, midiChannel,
                      delaySamples, strumMember);
            return;
        }
        if (voice.played && voice.midiNote == midiNote
            && voice.midiChannel == midiChannel && voice.keyDown)
        {
            ++voice.ownerCount;
            voice.startOrder = ++noteOrder_;
            voice.velocity = clamp(velocity, 0.001f, 1.0f);
            voice.strumming = strumMember;
            voice.pluckParallelSign = strumMember ? strumParallelSign_ : 1.0f;
            voice.pluckGesture = strumMember ? strumGesture_ : PickingGesture {};
            voice.repluckPending = true;
            // The key is down again before the pick came: its key-up is
            // void, and one pick serves both strokes, as it always has.
            voice.releaseAfterPluck = false;
            voice.pedalReleasedBeforePluck = false;
            if (delaySamples > 0)
                voice.pluckDelay = delaySamples + 1;
            else
                firePluck(voice, string);
            return;
        }
    }

    int string = -1;
    int harmonic = 1;
    // Guitar-controller mode: the channel already says which string, the way
    // a GK pickup or TriplePlay does in mono mode, so the fret-distance guess
    // below never runs. A note the channel's own string cannot fret is
    // dropped rather than handed to a different string, matching what such
    // a controller can physically pick.
    if (stringPerChannelMode_ && midiChannel >= 1 && midiChannel <= stringCount)
    {
        const int candidate = midiChannel - 1;
        const int fret = midiNote
            - voices_[static_cast<std::size_t>(candidate)].openMidi;
        if (fret < 0 || fret > fretCount)
            return;
        string = candidate;
    }
    else
    {
        string = -1;
        bool planned = false;
        if (plannedCount_ > 0 && plannedSample_ == sampleClock_
            && plannedChannel_ == midiChannel)
        {
            for (int index = 0; index < plannedCount_; ++index)
            {
                const auto slot = static_cast<std::size_t>(index);
                if (plannedNotes_[slot] != midiNote)
                    continue;
                string = plannedStrings_[slot];
                plannedNotes_[slot] = -1;
                planned = true;
                break;
            }
        }
        // MPE member notes keep the handless allocator exactly: a member
        // controller's notes are placed as they always were.
        if (string < 0)
            string = isLowerZoneMember(midiChannel)
                ? chooseStringWithoutHand(midiNote) : chooseString(midiNote);
        if (string >= 0 && !planned && !isLowerZoneMember(midiChannel))
            string = reshapeFormingChord(midiNote, midiChannel, string);
        if (string < 0)
        {
            // Above the fretted range the guitar still reaches, through the
            // natural harmonics of its open strings. Below it, it does not.
            const auto choice = chooseHarmonic(midiNote);
            if (choice.string < 0)
                return;
            string = choice.string;
            harmonic = choice.harmonic;
        }
    }
    startNote(string, harmonic, midiNote, velocity, midiChannel, delaySamples,
              strumMember);
}

bool AcustraEngine::transitionNote(int sourceMidiNote, int targetMidiNote,
                                     float fingerVelocity, int midiChannel) noexcept
{
    if (!prepared_ || sourceMidiNote == targetMidiNote
        || targetMidiNote < 0 || targetMidiNote > 127
        || !exact::isfinite(fingerVelocity) || fingerVelocity <= 0.0f
        || midiChannel < 1 || midiChannel > midiChannelCount)
        return false;
    const int string = heldString(sourceMidiNote, midiChannel);
    if (string < 0 || heldString(targetMidiNote, midiChannel) >= 0)
        return false;
    auto& voice = voices_[static_cast<std::size_t>(string)];
    const int targetFret = targetMidiNote - voice.openMidi;
    if (voice.ownerCount != 1 || voice.harmonic != 1 || !voice.attackFired
        || voice.pluckDelay != 0 || voice.repluckPending
        || targetFret < 0 || targetFret > fretCount
        || std::abs(targetMidiNote - sourceMidiNote) > 12
        || voice.legatoContactTravel.active || voice.legatoContactSamples > 0)
        return false;

    // No shape replacement and no tail copy: these are the same physical
    // waves at a new fret. A released source's later Note Off is harmless;
    // only the target owns this vibrating string after the transfer.
    const bool hammer = targetMidiNote > sourceMidiNote;
    const float contactPosition = clamp(std::exp2(-static_cast<float>(
        std::abs(targetMidiNote - sourceMidiNote)) / 12.0f), 0.5f, 0.97f);
    const float displacement = voice.loops[0].displacementAt(contactPosition);
    double slopeEnergy = 0.0;
    for (const auto& loop : voice.loops)
    {
        const int length = std::clamp(static_cast<int>(
            std::round(loop.currentDelay)), 8, maximumDelaySamples - 3);
        float previous = loop.delay[static_cast<std::size_t>(
            wrapDelayIndex(loop.writeIndex - length))];
        for (int age = length - 1; age >= 0; --age)
        {
            const float current = loop.delay[static_cast<std::size_t>(
                wrapDelayIndex(loop.writeIndex - age))];
            const double difference = static_cast<double>(current) - previous;
            slopeEnergy += difference * difference;
            previous = current;
        }
    }
    const float period = voice.loops[0].currentDelay;
    voice.legatoContactSamples = std::clamp(static_cast<int>(
        (hammer ? 0.002 : 0.003) * sampleRate_), 4,
        static_cast<int>(voice.legatoContactPulse.size()) - 1);
    voice.legatoContactAge = 0;
    // A little skin/fret friction accompanies the smooth contact, especially
    // on wound strings and longer finger movements. This is an authored
    // listening map, not a measured contact-force fit. Its independent draws
    // cannot change the picking/release streams or ordinary overlapping MIDI.
    // Windowed 0.9--5 kHz noise starts/ends at rest. Orthogonalise its sampled
    // slope against the smooth pulse, then give it a small part of the SAME
    // <=2% budget: adding fret noise must not add an unconstrained attack.
    auto& pulse = voice.legatoContactPulse;
    std::array<float, 1153> smoothPulse {};
    pulse[0] = 0.0f;
    const double high = 1.0 - std::exp(-2.0 * piDouble
        * std::min(5000.0, 0.4 * sampleRate_) / sampleRate_);
    const double low = 1.0 - std::exp(-2.0 * piDouble * 900.0 / sampleRate_);
    double highState = 0.0, lowState = 0.0;
    double smoothNorm = 0.0, cross = 0.0;
    double previousSmooth = 0.0, previousNoise = 0.0;
    for (int age = 1; age <= voice.legatoContactSamples; ++age)
    {
        const double phase = piDouble * age / voice.legatoContactSamples;
        smoothPulse[static_cast<std::size_t>(age)] = age == voice.legatoContactSamples ? 0.0f
            : static_cast<float>(std::sin(phase) * std::sin(phase));
        const double smooth = smoothPulse[static_cast<std::size_t>(age)];
        const double white = xorshiftNoise(voice.legatoFrictionState);
        highState += high * (white - highState);
        lowState += low * (white - lowState);
        pulse[static_cast<std::size_t>(age)] = static_cast<float>(
            smooth * (highState - lowState));
        const double noise = pulse[static_cast<std::size_t>(age)];
        const double ds = smooth - previousSmooth;
        smoothNorm += ds * ds;
        cross += ds * (noise - previousNoise);
        previousSmooth = smooth; previousNoise = noise;
    }
    const double projection = cross / smoothNorm;
    double noiseNorm = 0.0;
    previousNoise = 0.0;
    for (int age = 1; age <= voice.legatoContactSamples; ++age)
    {
        const double smooth = smoothPulse[static_cast<std::size_t>(age)];
        auto& noise = pulse[static_cast<std::size_t>(age)];
        noise = static_cast<float>(noise - projection * smooth);
        const double difference = noise - previousNoise;
        noiseNorm += difference * difference;
        previousNoise = noise;
    }
    const double distance = std::sqrt(std::abs(targetMidiNote - sourceMidiNote) / 12.0);
    const double frictionShare = (string < 4 ? 0.12 : 0.05) * (0.5 + 0.5 * distance);
    const double frictionGain = noiseNorm > 0.0 ? std::sqrt(
        frictionShare * smoothNorm / ((1.0 - frictionShare) * noiseNorm)) : 0.0;
    double norm = 0.0, previous = 0.0;
    for (int age = 1; age <= voice.legatoContactSamples; ++age)
    {
        const double smooth = smoothPulse[static_cast<std::size_t>(age)];
        auto& value = pulse[static_cast<std::size_t>(age)];
        value = static_cast<float>(smooth + frictionGain * value);
        const double difference = value - previous;
        norm += difference * difference;
        previous = value;
    }
    const float velocity = clamp(fingerVelocity, 0.0f, 1.0f);
    const float amplitude = slopeEnergy > 0.0 && norm > 0.0
        ? static_cast<float>(std::sqrt(0.02 * velocity * velocity
                                       * slopeEnergy / norm)) : 0.0f;
    voice.legatoContactAmplitude = (hammer ? -1.0f : 1.0f)
        * (displacement < 0.0f ? -amplitude : amplitude);
    voice.legatoContactTravel.reset(0.5f * period * contactPosition,
                                    period * (1.0f - 0.5f * contactPosition));
    voice.velocity = velocity;
    voice.startOrder = ++noteOrder_;
    voice.onsetSample = sampleClock_;
    voice.memberPitchBendFrozen = false;
    voice.releaseVelocity = -1.0f;
    voice.releaseSeconds = 0.0f;
    voice.returnSamples = 0;
    voice.attackPitchCents = 0.0f;
    voice.attackPitchDecay = 1.0f;
    // Stop only future picking input. Already emitted waves keep travelling.
    voice.excitationEnvelope = 0.0f;
    voice.contactNoiseAmplitude = 0.0f;
    voice.strumming = false;
    configureVoice(voice, string, targetMidiNote, false);
    rememberFinger(string);
    plannedCount_ = 0;
    return true;
}

void AcustraEngine::startNote(int string, int harmonic, int midiNote,
                              float velocity, int midiChannel,
                              int delaySamples, bool strumMember,
                              const PickingGesture* capturedGesture,
                              float capturedParallelSign) noexcept
{
    auto& voice = voices_[static_cast<std::size_t>(string)];
    voice.releaseJoinPending = false;
    voice.releaseJoinRemainingBeats = 0.0;
    // Taking a string that is still sounding, for any note, is a refret and a
    // repluck, not a cut: what it still holds carries on under the hand while
    // the new pluck is released from rest.
    const bool retainedWave = voice.level > 2.0e-7f
        || (voice.contactTravelEnabled && voice.contactTravel.active)
        || voice.contactNoiseTravel.active
        || voice.legatoContactTravel.active || voice.legatoContactSamples > 0
        || voice.repluckArrivals.remaining > 0
        || (voice.loops[0].gestureContact.active
            && exact::abs(voice.loops[0].gestureContact.memory) > 1.0e-12f)
        || (voice.loops[1].gestureContact.active
            && exact::abs(voice.loops[1].gestureContact.memory) > 1.0e-12f);
    voice.repluckContactPending = retainedWave && voice.played && voice.attackFired
        && harmonic == 1 && voice.harmonic == 1 && voice.midiNote == midiNote
        && voice.midiChannel == midiChannel;
    const bool continuing = voice.repluckContactPending;
    if (retainedWave && !continuing)
        captureTail(voice);
    // A new stroke lifts the release hand. A different note retains the
    // old contact with its copied tail; it must not damp the fresh string.
    for (auto& loop : voice.loops)
        loop.gestureContact = {};
    if (!continuing)
    {
        voice.legatoContactSamples = 0;
        voice.legatoContactAmplitude = 0.0f;
    }
    voice.harmonic = harmonic;
    voice.played = true;
    voice.keyDown = true;
    voice.pedalHeld = false;
    voice.ownerCount = 1;
    voice.midiChannel = midiChannel;
    voice.mpeMember = isLowerZoneMember(midiChannel);
    voice.memberPitchBendFrozen = false;
    voice.frozenMemberPitchBendSemitones = 0.0f;
    voice.startOrder = ++noteOrder_;
    const float v = clamp(velocity, 0.001f, 1.0f);
    voice.velocity = v;
    voice.attackPitchCents = 0.0f;
    voice.attackPitchDecay = 1.0f;
    configureVoice(voice, string, midiNote, !continuing);
    voice.onsetSample = sampleClock_;
    rememberFinger(string);
    voice.strumming = strumMember;
    // A forming chord may move an unfired string while another stroke has
    // already begun. Restore the original stroke before a zero-delay move
    // can fire; neither its direction nor its hand posture belongs to the
    // latest beginStrum call.
    voice.pluckParallelSign = capturedGesture != nullptr
        ? capturedParallelSign : strumMember ? strumParallelSign_ : 1.0f;
    voice.pluckGesture = capturedGesture != nullptr
        ? *capturedGesture : strumMember ? strumGesture_ : PickingGesture {};
    voice.repluckPending = continuing;
    voice.attackFired = continuing;
    voice.releaseAfterPluck = false;
    voice.pedalHeldAtKeyUp = false;
    voice.pedalReleasedBeforePluck = false;
    voice.releaseVelocity = -1.0f;
    if (delaySamples > 0)
    {
        // Fretted and waiting: a junction member with nothing on it until
        // the pick arrives. The countdown fires at the top of that sample,
        // exactly where a note-on issued then would have put the shape.
        if (!continuing)
            voice.excitationEnvelope = 0.0f;
        voice.pluckDelay = delaySamples + 1;
        return;
    }
    firePluck(voice, string);
}

// The hand's memory of a string: the fret its finger now holds there, or
// none for an open string or a natural harmonic's touch.
void AcustraEngine::rememberFinger(int stringIndex) noexcept
{
    const auto index = static_cast<std::size_t>(stringIndex);
    const auto& voice = voices_[index];
    auto& finger = hand_[index];
    if (voice.harmonic == 1 && voice.fret >= 1)
        finger = { voice.fret, sampleClock_, true };
    else
        finger.valid = false;
}

// The finger leaves the note: its memory starts to fade from now.
void AcustraEngine::releaseFinger(int stringIndex) noexcept
{
    const auto index = static_cast<std::size_t>(stringIndex);
    const auto& voice = voices_[index];
    auto& finger = hand_[index];
    if (finger.valid && voice.played && voice.harmonic == 1
        && voice.fret == finger.fret)
        finger.heldAt = sampleClock_;
}

bool AcustraEngine::betterShape(const ShapeScore& candidate,
                                const ShapeScore& incumbent) noexcept
{
    constexpr float tie = 1.0e-4f;
    if (candidate.steals != incumbent.steals)
        return candidate.steals < incumbent.steals;
    if (candidate.impossible != incumbent.impossible)
        return candidate.impossible < incumbent.impossible;
    if (candidate.moves != incumbent.moves)
        return candidate.moves < incumbent.moves;
    if (exact::abs(candidate.cost - incumbent.cost) > tie)
        return candidate.cost < incumbent.cost;
    if (candidate.misses != incumbent.misses)
        return candidate.misses < incumbent.misses;
    if (candidate.opens != incumbent.opens)
        return candidate.opens > incumbent.opens;
    if (candidate.fretSum != incumbent.fretSum)
        return candidate.fretSum < incumbent.fretSum;
    return candidate.ringing < incumbent.ringing;
}

// Every way of putting the chord's notes one to a string, depth first: at
// most 6! complete shapes, scored by betterShape; the first of equals wins,
// so the result depends only on the engine's state.
void AcustraEngine::searchShape(ShapeSearch& search, int index,
                                unsigned used) const noexcept
{
    if (index == search.count)
    {
        ShapeScore score {};
        for (int note = 0; note < search.count; ++note)
        {
            const auto& shapeNote = search.notes[static_cast<std::size_t>(note)];
            const int string = search.strings[static_cast<std::size_t>(note)];
            const auto& voice = voices_[static_cast<std::size_t>(string)];
            const int fret = search.frets[static_cast<std::size_t>(string)];
            if (voice.played && voice.keyDown && string != shapeNote.current
                && ((search.movable >> string) & 1u) == 0u)
                ++score.steals;
            if (shapeNote.current >= 0 && string != shapeNote.current)
                ++score.moves;
            if (shapeNote.ringing >= 0 && string != shapeNote.ringing)
                ++score.misses;
            score.opens += fret == 0 ? 1 : 0;
            score.fretSum += fret;
            score.ringing += voice.played && string != shapeNote.current ? 1 : 0;
        }
        score.cost = shapeCost(search.frets, search.movable, search.weights);
        score.impossible = score.cost >= impossibleShapeCost ? 1 : 0;
        if (!search.found || betterShape(score, search.best))
        {
            search.best = score;
            search.bestStrings = search.strings;
            search.found = true;
        }
        return;
    }
    const auto& shapeNote = search.notes[static_cast<std::size_t>(index)];
    for (int string = stringCount - 1; string >= 0; --string)
    {
        if (((used >> string) & 1u) != 0u)
            continue;
        if (shapeNote.fixed && string != shapeNote.current)
            continue;
        const int fret = shapeNote.midiNote
            - voices_[static_cast<std::size_t>(string)].openMidi;
        if (fret < 0 || fret > fretCount)
            continue;
        search.strings[static_cast<std::size_t>(index)] = string;
        search.frets[static_cast<std::size_t>(string)] = fret;
        searchShape(search, index + 1, used | (1u << string));
        search.frets[static_cast<std::size_t>(string)] = -1;
    }
}

// A chord that arrives on one sample is fretted as one shape. The notes are
// placed together, one to a string, by the same rules as a single note:
// no string whose key is still down while a free one serves; a shape the
// hand holds within one span, reached from where it is with the fewest
// frets moved; a note re-struck on the string still ringing it; the most
// open strings; the lowest frets; silent strings before ringing ones.
void AcustraEngine::planChord(const int* midiNotes, int count,
                              int midiChannel) noexcept
{
    plannedCount_ = 0;
    if (!prepared_ || midiNotes == nullptr || count < 2 || count > stringCount
        || midiChannel < 1 || midiChannel > midiChannelCount
        || isLowerZoneMember(midiChannel)
        || (stringPerChannelMode_ && midiChannel <= stringCount))
        return;
    ShapeSearch search {};
    search.frets.fill(-1);
    search.weights = handWeights();
    unsigned harmonicStrings = 0u;
    for (int index = 0; index < count; ++index)
    {
        const int midiNote = midiNotes[index];
        if (midiNote < 0 || midiNote > 127)
            continue;
        bool duplicate = false;
        for (int other = 0; other < search.count; ++other)
            duplicate |= search.notes[static_cast<std::size_t>(other)].midiNote
                == midiNote;
        bool frettable = false;
        for (const auto& voice : voices_)
            frettable |= midiNote - voice.openMidi >= 0
                && midiNote - voice.openMidi <= fretCount;
        // A repeat of a note the chord already has, or one only a natural
        // harmonic reaches, is left to the note-by-note allocator. The
        // harmonic's string is kept out of the shape where the shape allows,
        // or the note would find it taken and never sound.
        if (!frettable && !duplicate)
        {
            const auto harmonic = chooseHarmonic(midiNote);
            if (harmonic.string >= 0)
                harmonicStrings |= 1u << harmonic.string;
        }
        if (duplicate || !frettable)
            continue;
        ShapeNote note {};
        note.midiNote = midiNote;
        for (int string = 0; string < stringCount; ++string)
        {
            const auto& voice = voices_[static_cast<std::size_t>(string)];
            if (voice.played && voice.keyDown && voice.midiNote == midiNote
                && voice.midiChannel == midiChannel)
            {
                // noteOn re-plucks a held key where it is.
                note.current = string;
                note.fixed = true;
            }
            else if (voice.played && !voice.keyDown && voice.harmonic == 1
                     && voice.midiNote == midiNote
                     && (voice.level > 2.0e-7f
                         || voice.loops[0].gestureContact.active
                         || voice.loops[1].gestureContact.active))
                note.ringing = string;
        }
        search.notes[static_cast<std::size_t>(search.count++)] = note;
    }
    if (search.count < 2)
        return;
    searchShape(search, 0, harmonicStrings);
    if (!search.found && harmonicStrings != 0u)
        searchShape(search, 0, 0u);
    if (!search.found)
        return;
    for (int index = 0; index < search.count; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);
        plannedNotes_[slot] = search.notes[slot].midiNote;
        plannedStrings_[slot] = search.bestStrings[slot];
    }
    plannedCount_ = search.count;
    plannedChannel_ = midiChannel;
    plannedSample_ = sampleClock_;
}

int AcustraEngine::plannedString(int midiNote, int midiChannel) const noexcept
{
    if (midiNote < 0 || midiNote > 127
        || plannedSample_ != sampleClock_ || plannedChannel_ != midiChannel)
        return -1;
    for (int index = 0; index < plannedCount_; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);
        if (plannedNotes_[slot] == midiNote)
            return plannedStrings_[slot];
    }
    return -1;
}

// A chord that arrives one key at a time can be fretted into a corner: a
// rolled C4-E4-G4 puts C4 on the B string's first fret and E4 on the open
// E before G4 arrives, and then only the G string's twelfth fret is free.
// When the note just chosen would take a string whose key is down, or
// cannot be held in one hand with the notes already down, and it follows
// notes of its own channel still forming a chord (onsets within
// chordWindowSeconds of each other), the chord so far is refretted with
// it as one shape. Only an attack that has not fired can move: a note
// already sounding stays on its physical string instead of being plucked
// again when a later key completes the chord. A pending explicit re-pluck
// also stays with its preceding vibration. Without looking ahead, some
// rolls cannot fit the final shape with those strings committed; they keep
// the note-by-note allocation. Gather Chords can plan the whole shape before
// any attack. Returns the string for the new note.
int AcustraEngine::reshapeFormingChord(int midiNote, int midiChannel,
                                       int chosenString) noexcept
{
    const auto weights = handWeights();
    {
        const auto& chosen = voices_[static_cast<std::size_t>(chosenString)];
        StringFrets frets {};
        frets.fill(-1);
        frets[static_cast<std::size_t>(chosenString)]
            = midiNote - chosen.openMidi;
        const bool steal = chosen.played
            && (chosen.keyDown || chosen.releaseAfterPluck);
        if (!steal && shapeCost(frets, 0u, weights) < impossibleShapeCost)
            return chosenString;
    }
    ShapeSearch search {};
    search.frets.fill(-1);
    search.weights = weights;
    for (int string = 0; string < stringCount; ++string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        if (!voice.played || (!voice.keyDown && !voice.releaseAfterPluck)
            || voice.harmonic != 1
            || voice.mpeMember
            || voice.midiChannel != midiChannel
            || voice.onsetSample
                < chordStartSample_[static_cast<std::size_t>(midiChannel - 1)]
            || voice.midiNote == midiNote)
            continue;
        ShapeNote note {};
        note.midiNote = voice.midiNote;
        note.current = string;
        note.fixed = voice.attackFired;
        search.notes[static_cast<std::size_t>(search.count++)] = note;
        search.movable |= 1u << string;
    }
    if (search.count == 0 || search.count >= stringCount)
        return chosenString;
    ShapeNote incoming {};
    incoming.midiNote = midiNote;
    search.notes[static_cast<std::size_t>(search.count++)] = incoming;
    searchShape(search, 0, 0u);
    if (!search.found || search.best.impossible != 0 || search.best.steals != 0)
        return chosenString;
    const int incomingString = search.bestStrings[
        static_cast<std::size_t>(search.count - 1)];
    // The best shape may keep every note where it is and put only the new
    // one elsewhere: an upstroke's B2, chosen on the A string's second fret
    // under G3-C4-E4-B4 already held at frets 5-7, fits them on the low E's
    // seventh. Returning the string first chosen there left a five-fret
    // stretch no hand holds whenever a strum's notes arrived a few
    // milliseconds apart rather than on one sample.
    if (search.best.moves == 0)
        return incomingString;

    // Lift every moved note first, so a chain of moves finds its strings free.
    struct Moved
    {
        int from, to, midiNote, channel, ownerCount, delay;
        float velocity, releaseVelocity;
        bool strumming, releaseAfterPluck, pedalHeldAtKeyUp, pedalReleasedBeforePluck;
        std::uint64_t startOrder, onsetSample;
        float parallelSign;
        PickingGesture gesture;
    };
    std::array<Moved, stringCount> moved {};
    int movedCount = 0;
    unsigned vacated = 0u;
    for (int index = 0; index + 1 < search.count; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);
        const int from = search.notes[slot].current;
        const int to = search.bestStrings[slot];
        if (from == to)
            continue;
        auto& voice = voices_[static_cast<std::size_t>(from)];
        moved[static_cast<std::size_t>(movedCount++)] = {
            from, to, voice.midiNote, voice.midiChannel, voice.ownerCount,
            voice.pluckDelay > 0 ? voice.pluckDelay - 1 : 0,
            voice.velocity, voice.releaseVelocity, voice.strumming, voice.releaseAfterPluck,
            voice.pedalHeldAtKeyUp, voice.pedalReleasedBeforePluck,
            voice.startOrder, voice.onsetSample,
            voice.pluckParallelSign, voice.pluckGesture };
        voice.keyDown = false;
        voice.pedalHeld = false;
        voice.ownerCount = 0;
        voice.pluckDelay = 0;
        voice.repluckPending = false;
        voice.releaseAfterPluck = false;
        hand_[static_cast<std::size_t>(from)].valid = false;
        vacated |= 1u << from;
    }
    unsigned taken = 1u << incomingString;
    for (int index = 0; index < movedCount; ++index)
    {
        const auto& move = moved[static_cast<std::size_t>(index)];
        taken |= 1u << move.to;
        startNote(move.to, 1, move.midiNote, move.velocity, move.channel,
                  move.delay, move.strumming, &move.gesture, move.parallelSign);
        auto& voice = voices_[static_cast<std::size_t>(move.to)];
        voice.ownerCount = move.ownerCount;
        voice.startOrder = move.startOrder;
        voice.onsetSample = move.onsetSample;
        // A strum member already let go keeps its pending key-up on the
        // string it moved to (startNote cleared it). One whose pick was due
        // on this very sample has just been plucked by startNote, so its
        // key-up is due now too: left pending, the key stayed down with no
        // owner and the note rang on unreleased.
        if (move.releaseAfterPluck)
        {
            voice.releaseVelocity = move.releaseVelocity;
            if (voice.pluckDelay > 0)
            {
                voice.releaseAfterPluck = true;
                voice.pedalHeldAtKeyUp = move.pedalHeldAtKeyUp;
                voice.pedalReleasedBeforePluck = move.pedalReleasedBeforePluck;
                voice.keyDown = false;
                voice.pedalHeld = move.pedalHeldAtKeyUp;
            }
            else
            {
                completeKeyUp(voice, move.to, move.pedalHeldAtKeyUp);
                if (move.pedalReleasedBeforePluck && !voice.pedalHeld)
                    beginRelease(voice, move.to);
            }
        }
    }
    for (int string = 0; string < stringCount; ++string)
        if (((vacated >> string) & 1u) != 0u && ((taken >> string) & 1u) == 0u)
            muteVacatedString(voices_[static_cast<std::size_t>(string)], string);
    return incomingString;
}

// A finger that leaves a string for another damps what it leaves behind
// with the hand loss a released fretted note already has, open or not.
void AcustraEngine::muteVacatedString(Voice& voice, int stringIndex) noexcept
{
    voice.releaseJoinPending = false;
    voice.releaseJoinRemainingBeats = 0.0;
    static_cast<void>(stringIndex);
    if (voice.contactTravelEnabled)
        voice.excitationEnvelope = 0.0f;
    constexpr float releaseSeconds = 0.16f;
    voice.releaseSeconds = releaseSeconds;
    voice.releaseDamping = handDamping(releaseSeconds, loopFundamental(voice));
    voice.returnSamples = static_cast<int>(
        (releaseSeconds + 0.08f) * static_cast<float>(sampleRate_));
    beginGestureDamping(voice);
    if (targetParameters_.releaseNoise > 0.0f)
        startReleaseNoise(voice, stringIndex,
                          voice.fret > 0 && voice.harmonic <= 1);
}

int AcustraEngine::heldString(int midiNote, int midiChannel) const noexcept
{
    int found = -1;
    for (int string = 0; string < stringCount; ++string)
    {
        const auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && voice.keyDown && voice.midiNote == midiNote
            && voice.midiChannel == midiChannel
            && (found < 0 || voice.startOrder
                > voices_[static_cast<std::size_t>(found)].startOrder))
            found = string;
    }
    return found;
}

void AcustraEngine::firePluck(Voice& voice, int stringIndex) noexcept
{
    const bool scheduled = voice.pluckDelay > 0;
    voice.pluckDelay = 0;
    // initialisePluck clears a preceding release's velocity. A strum's
    // queued key-up belongs to this stroke instead, so keep it until the
    // actual pluck arms the ordinary release grace.
    const float queuedReleaseVelocity = voice.releaseVelocity;
    bool merge = false;
    if (voice.repluckPending)
    {
        voice.repluckPending = false;
        merge = voice.attackFired && voice.harmonic == 1
            && sampleClock_ > voice.lastPluckSample;
        voice.attackPitchCents = 0.0f;
        voice.attackPitchDecay = 1.0f;
        // The pick reaches this held string now. Keep its preceding wave
        // intact until then, and carry it under the hand while the new pluck
        // is released, exactly as for an immediate re-pluck.
        if (merge)
        {
            repluckOldLoops_ = voice.loops;
            retainRepluckArrivals(voice);
        }
        else if (voice.level > 2.0e-7f
            || (voice.contactTravelEnabled && voice.contactTravel.active)
            || voice.contactNoiseTravel.active
            || voice.legatoContactTravel.active || voice.legatoContactSamples > 0
            || voice.repluckArrivals.remaining > 0)
        {
            voice.repluckContactPending = voice.attackFired && voice.harmonic == 1;
            captureTail(voice);
        }
        else
            voice.repluckContactPending = false;
        // Repeated Pick releases need current reference-rate geometry too,
        // even when the preceding wave and its filter memories continue.
        configureVoice(voice, stringIndex, voice.midiNote, !merge, true);
    }
    else if (scheduled)
    {
        // A wheel or playing control can move after fretting but before the
        // pick arrives, even between two control updates. Release from the
        // string's current geometry, as an immediate stroke would do now.
        configureVoice(voice, stringIndex, voice.midiNote, true);
    }
    initialisePluck(voice, stringIndex, voice.velocity, merge);
    voice.attackFired = true;
    if (merge)
        bridgeDerivativesCrossConfigure_ = true;
    else
        bridgeDerivativesCrossRelease_ = true;
    configureVoice(voice, stringIndex, voice.midiNote, false);
    if (voice.releaseAfterPluck)
    {
        const bool immediate = voice.pedalReleasedBeforePluck;
        voice.releaseVelocity = queuedReleaseVelocity;
        completeKeyUp(voice, stringIndex, voice.pedalHeldAtKeyUp);
        if (immediate && !voice.pedalHeld)
            beginRelease(voice, stringIndex);
    }
}

int AcustraEngine::strumDelaySamples(int stringRank,
                                     float velocity) const noexcept
{
    // The pick crosses the strings at one speed, so the k-th string it
    // reaches sounds k spacings later. The spacing is the set-up dimension
    // at the saddle: 2 1/8" across the six on a steel-string. The pick's speed is the one number MIDI does not carry;
    // GuitarSet's comping tracks (Tools/MeasureStrums.py on the hex-pickup
    // channels and JAMS note onsets, Zenodo 3371780 CC BY 4.0), clustered
    // into 641 same-stroke (>=3 strings within a window derived from each
    // track's own annotated tempo -- a sixteenth note at its fastest, so
    // the window cannot merge two distinct strokes) onset clusters, put the
    // pooled 10-90% traversal speed, at the shipping 10.8 mm steel spacing,
    // at 0.51 to 2.46 m/s (five 10.8 mm gaps across six strings: 106 ms to
    // 22 ms); the speed showed no resolvable dependence on stroke level
    // (plain Pearson r of speed against mean stroke level, about -0.03 --
    // that mean level is averaged across strings on the hex pickup's
    // uncalibrated per-coil sensitivity, which biases the correlation
    // toward zero, so this null is weaker evidence than a same-channel
    // measurement would give), so the map from MIDI velocity to speed
    // across that measured range stays a player's map, not a fitted
    // dependence.
    constexpr float spacing = 0.0540f / 5.0f;
    const float speed = 0.51f + 1.95f * clamp(velocity, 0.0f, 1.0f);
    return static_cast<int>(static_cast<float>(std::max(stringRank, 0))
        * spacing / speed * static_cast<float>(sampleRate_) + 0.5f);
}

void AcustraEngine::noteOff(int midiNote, int midiChannel) noexcept
{
    releaseKey(midiNote, midiChannel, false, false);
}

void AcustraEngine::noteOff(int midiNote, int midiChannel,
                            bool sustained) noexcept
{
    releaseKey(midiNote, midiChannel, true, sustained);
}

void AcustraEngine::noteOffWithVelocity(int midiNote, int midiChannel,
                                        float releaseVelocity) noexcept
{
    releaseKey(midiNote, midiChannel, false, false, releaseVelocity);
}

void AcustraEngine::noteOffWithVelocity(int midiNote, int midiChannel,
                                        bool sustained,
                                        float releaseVelocity) noexcept
{
    releaseKey(midiNote, midiChannel, true, sustained, releaseVelocity);
}

void AcustraEngine::noteOffWithVelocity(int midiNote, int midiChannel,
                                        bool sustained, float releaseVelocity,
                                        bool pedalReleased) noexcept
{
    releaseKey(midiNote, midiChannel, true, sustained,
               pedalReleased ? -1.0f : releaseVelocity, pedalReleased);
}

bool AcustraEngine::sustainHolds(int midiChannel) const noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return false;
    const bool own = sustainPedals_[static_cast<std::size_t>(midiChannel - 1)];
    return isLowerZoneMember(midiChannel) ? sustainPedals_[0] || own : own;
}

void AcustraEngine::releaseKey(int midiNote, int midiChannel,
                               bool sustainGiven, bool sustained,
                               float releaseVelocity, bool immediateRelease) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    int candidateIndex = -1;
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && voice.keyDown && voice.midiNote == midiNote
            && voice.midiChannel == midiChannel
            && (candidateIndex < 0 || voice.startOrder
                > voices_[static_cast<std::size_t>(candidateIndex)].startOrder))
            candidateIndex = string;
    }
    if (candidateIndex < 0)
        return;
    auto& candidate = voices_[static_cast<std::size_t>(candidateIndex)];
    if (--candidate.ownerCount > 0)
        return;
    candidate.ownerCount = 0;
    freezeMemberPitchBend(candidate);
    candidate.releaseVelocity = exact::isfinite(releaseVelocity)
        ? std::min(releaseVelocity, 1.0f) : -1.0f;
    if (candidate.pluckDelay > 0 && candidate.strumming)
    {
        // A strum's key-up that comes before the pick reaches this string
        // does not stop the pick: the stroke was played as one gesture, and
        // the same note played alone and let go at once still sounds. The
        // string stays fretted until the pick arrives and is let go then,
        // under the pedal as it was at this key-up (setSustainPedal keeps
        // that current). An explicit, non-strum delay is still cancelled.
        candidate.releaseAfterPluck = true;
        candidate.pedalReleasedBeforePluck = immediateRelease;
        candidate.pedalHeldAtKeyUp = sustainGiven ? sustained
                                                  : sustainIsDown(candidate);
        if (candidate.pedalHeldAtKeyUp)
            candidate.releaseVelocity = -1.0f;
        candidate.keyDown = false;
        candidate.pedalHeld = candidate.pedalHeldAtKeyUp;
        return;
    }
    candidate.pluckDelay = 0;
    candidate.repluckPending = false;
    completeKeyUp(candidate, candidateIndex,
                  sustainGiven ? sustained : sustainIsDown(candidate), immediateRelease);
}

void AcustraEngine::completeKeyUp(Voice& voice, int stringIndex,
                                  bool pedalHeld, bool immediateRelease) noexcept
{
    voice.releaseAfterPluck = false;
    voice.pedalHeldAtKeyUp = false;
    voice.pedalReleasedBeforePluck = false;
    voice.keyDown = false;
    voice.pedalHeld = pedalHeld;
    voice.releaseJoinPending = false;
    // Under the pedal the hand damps the string at pedal-up, long after the
    // key came up, so how fast the key came up does not reach it.
    if (voice.pedalHeld)
        voice.releaseVelocity = -1.0f;
    if (!voice.pedalHeld && immediateRelease)
        beginRelease(voice, stringIndex);
    else if (!voice.pedalHeld)
    {
        releaseFinger(stringIndex);
        voice.releaseJoinPending = true;
        voice.releaseJoinRemainingBeats = 0.125;
        voice.releaseJoinAnchorSample = sampleClock_;
        updateReleaseJoinWindow(voice);
        voice.releaseDamping = 1.0f;
        voice.releaseSeconds = 0.0f;
        voice.returnSamples = 0;
    }
}

void AcustraEngine::setSustainPedal(bool down, int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    auto& pedal = sustainPedals_[static_cast<std::size_t>(midiChannel - 1)];
    if (pedal == down)
        return;
    pedal = down;
    if (down)
        return;
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.releaseAfterPluck && channelControlsVoice(midiChannel, voice)
            && !sustainIsDown(voice))
        {
            voice.pedalReleasedBeforePluck = voice.pedalReleasedBeforePluck
                || voice.pedalHeldAtKeyUp;
            voice.pedalHeldAtKeyUp = false;
            voice.pedalHeld = false;
            continue;
        }
        if (!voice.pedalHeld || voice.keyDown
            || !channelControlsVoice(midiChannel, voice)
            || sustainIsDown(voice))
            continue;
        beginRelease(voice, string);
    }
}

void AcustraEngine::setPitchBend(float semitones, int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    pitchBendSemitones_[static_cast<std::size_t>(midiChannel - 1)]
        = clamp(exact::isfinite(semitones) ? semitones : 0.0f, -96.0f, 96.0f);
}

void AcustraEngine::setVibrato(float amount) noexcept
{
    vibrato_ = clamp(exact::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
}

void AcustraEngine::setMpeTimbre(float value, int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    mpeTimbre_[static_cast<std::size_t>(midiChannel - 1)]
        = exact::isfinite(value) && value >= 0.0f
            ? clamp(value, 0.0f, 1.0f) : -1.0f;
}

void AcustraEngine::setMpePressure(float value, int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    mpePressure_[static_cast<std::size_t>(midiChannel - 1)]
        = exact::isfinite(value) && value >= 0.0f
            ? clamp(value, 0.0f, 1.0f) : -1.0f;
}

void AcustraEngine::setStringPerChannelMode(bool enabled) noexcept
{
    stringPerChannelMode_ = enabled;
}

void AcustraEngine::setLowerZoneMemberCount(int memberCount) noexcept
{
    const int next = std::clamp(memberCount, 0, midiChannelCount - 1);
    if (next == lowerZoneMemberCount_)
        return;

    // MIDI MPE defines layout changes as controller-reset/note-stop
    // boundaries for the union of the old and new zone. Acustra deliberately
    // supports the lower zone only; channels outside that union stay intact.
    const int lastAffectedChannel = std::max(next, lowerZoneMemberCount_) + 1;
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && voice.midiChannel <= lastAffectedChannel)
            returnToOpenString(voice, string, true);
    }
    for (int midiChannel = 1; midiChannel <= lastAffectedChannel; ++midiChannel)
    {
        pitchBendSemitones_[static_cast<std::size_t>(midiChannel - 1)] = 0.0f;
        mpeTimbre_[static_cast<std::size_t>(midiChannel - 1)] = -1.0f;
        mpePressure_[static_cast<std::size_t>(midiChannel - 1)] = -1.0f;
        sustainPedals_[static_cast<std::size_t>(midiChannel - 1)] = false;
    }
    lowerZoneMemberCount_ = next;
    if (getActiveVoiceCount() == 0)
    {
        for (int string = 0; string < stringCount; ++string)
            returnToOpenString(voices_[static_cast<std::size_t>(string)], string, true);
        resetSoundState();
    }
}

void AcustraEngine::allNotesOff(int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        if (!voice.played || (!voice.keyDown && !voice.releaseJoinPending
                             && !voice.releaseAfterPluck)
            || !channelControlsVoice(midiChannel, voice))
            continue;
        const bool alreadyReleased = voice.releaseJoinPending;
        voice.ownerCount = 0;
        voice.pluckDelay = 0;
        voice.repluckPending = false;
        voice.releaseAfterPluck = false;
        voice.pedalHeldAtKeyUp = false;
        voice.pedalReleasedBeforePluck = false;
        freezeMemberPitchBend(voice);
        voice.keyDown = false;
        // A pedal pressed after this ordinary key-up never recatches it,
        // including when an explicit All Notes Off flushes its grace.
        voice.pedalHeld = !alreadyReleased && sustainIsDown(voice);
        voice.releaseJoinPending = false;
        voice.releaseJoinRemainingBeats = 0.0;
        if (!voice.pedalHeld)
            beginRelease(voice, string);
    }
}

void AcustraEngine::allSoundOff(int midiChannel) noexcept
{
    if (midiChannel < 1 || midiChannel > midiChannelCount)
        return;
    for (int string = 0; string < stringCount; ++string)
    {
        auto& voice = voices_[static_cast<std::size_t>(string)];
        if (voice.played && channelControlsVoice(midiChannel, voice))
            returnToOpenString(voice, string, true);
    }
    if (getActiveVoiceCount() == 0)
    {
        for (int string = 0; string < stringCount; ++string)
            returnToOpenString(voices_[static_cast<std::size_t>(string)], string, true);
        resetSoundState();
        pickingGesture_ = {};
        strumGesture_ = {};
        pickingGestureSeen_ = false;
    }
}

void AcustraEngine::setPalmMutePressure(float pressure) noexcept
{
    targetPalmMute_ = exact::isfinite(pressure)
        ? clamp(pressure, 0.0f, 1.0f) : 0.0f;
}

void AcustraEngine::setBridgeCouplingEnabled(bool enabled) noexcept
{
    if (bridgeCouplingEnabled_ == enabled)
        return;
    bridgeCouplingEnabled_ = enabled;
    ++voiceConfigurationGeneration_;
    bridgeLoad_.reset();
    for (auto& derivative : bridgePowerDerivatives_)
        derivative.reset();
    bridgeVelocityDerivative_.reset();
    bridgeRotationDerivative_.reset();
    bridgeForceDerivative_.reset();
    bridgeForceMomentDerivative_.reset();
    bridgeBodyForceDerivative_.reset();
    bridgeBodyMomentDerivative_.reset();
    bridgeTailForceDerivative_.reset();
    bridgeTailMomentDerivative_.reset();
    piezoForceDerivative_.reset();
    lastBridgeVelocity_ = 0.0f;
    lastBridgeReactionForce_ = 0.0f;
    lastPiezoWave_ = lastPiezoForce_ = 0.0f;
    lastBridgeBodyForce_ = 0.0f;
    lastBridgeTailForce_ = 0.0f;
    lastLongitudinalForce_ = 0.0f;
    lastBridgePower_ = 0.0f;
    lastBridgeBodyPower_ = 0.0f;
    lastBridgeTailPower_ = 0.0f;
    bridgeDerivativesNeedPriming_ = true;
    bridgeDerivativesCrossRelease_ = false;
    bridgeDerivativesCrossConfigure_ = false;
    bridgeLoadFade_ = 1.0f;
    if (bridgeUpdatePending_)
        applyPendingBridge(false);
    if (!prepared_)
        return;
    for (int string = 0; string < stringCount; ++string)
        configureVoice(voices_[static_cast<std::size_t>(string)], string,
                       voices_[static_cast<std::size_t>(string)].midiNote,
                       false);
}

void AcustraEngine::setPortObserversEnabled(bool enabled) noexcept
{
    if (portObserversEnabled_ == enabled)
        return;
    portObserversEnabled_ = enabled;
    // A history that stopped is stale; restart the observers from rest, as
    // reset() does, rather than differencing across the gap.
    for (auto& derivative : bridgePowerDerivatives_)
        derivative.reset();
    bridgeForceMomentDerivative_.reset();
    bridgeTailForceDerivative_.reset();
    bridgeTailMomentDerivative_.reset();
    lastBridgeTailForce_ = 0.0f;
    lastBridgePower_ = 0.0f;
    lastBridgeBodyPower_ = 0.0f;
    lastBridgeTailPower_ = 0.0f;
}

void AcustraEngine::setSympatheticStringsEnabled(bool enabled) noexcept
{
    sympatheticStringsEnabled_ = enabled;
}

void AcustraEngine::ContactTravel::reset(float directDelay,
                                         float nutDelay) noexcept
{
    history.fill(0.0f);
    writeIndex = 0;
    historyLength = historyRemaining = 0;
    active = false;
    const std::array<float, 2> delays { directDelay, nutDelay };
    for (std::size_t i = 0; i < taps.size(); ++i)
    {
        auto& tap = taps[i];
        tap = {};
        const double delay = std::clamp(static_cast<double>(delays[i]),
            0.0, static_cast<double>(maximumDelaySamples - 3));
        // The existing second-order Thiran convention is stable for its
        // residual delay >=1.1. Very short causal paths need first order.
        if (delay >= 1.1)
        {
            tap.whole = delayAnchor(delay);
            tap.order = 2;
            thiranCoefficients(delay - tap.whole, tap.a1, tap.a2);
        }
        else if (delay > 1.0e-8)
        {
            tap.order = 1;
            tap.a1 = (1.0 - delay) / (1.0 + delay);
        }
        historyLength = std::max(historyLength, tap.whole + tap.order + 1);
    }
}

std::array<float, 2> AcustraEngine::ContactTravel::process(float source) noexcept
{
    if (source != 0.0f)
    {
        active = true;
        historyRemaining = historyLength;
    }
    if (!active)
        return {};
    history[static_cast<std::size_t>(writeIndex)] = source;
    std::array<float, 2> result {};
    for (std::size_t i = 0; i < taps.size(); ++i)
    {
        auto& tap = taps[i];
        const auto at = [&] (int lag)
        {
            return static_cast<double>(history[static_cast<std::size_t>(
                wrapDelayIndex(writeIndex - lag))]);
        };
        double value = at(tap.whole);
        if (tap.order == 2)
            value = tap.a2 * value + tap.a1 * at(tap.whole + 1)
                + at(tap.whole + 2) - tap.a1 * tap.y1 - tap.a2 * tap.y2;
        else if (tap.order == 1)
            value = tap.a1 * value + at(1) - tap.a1 * tap.y1;
        tap.y2 = tap.y1;
        tap.y1 = value;
        result[i] = static_cast<float>(value);
    }
    writeIndex = (writeIndex + 1) % maximumDelaySamples;
    if (historyRemaining > 0)
        --historyRemaining;
    if (historyRemaining == 0)
    {
        // No delayed input remains. In this fixed Thiran domain the absolute
        // feedback coefficient sum is <1, so states below this bound can
        // never produce another nonzero float sample. Retire without an
        // audible threshold or changing the held-note waveform.
        constexpr double silent = 0.25 * std::numeric_limits<float>::denorm_min();
        bool silentState = true;
        for (const auto& tap : taps)
            silentState = silentState && exact::abs(tap.y1) <= silent
                                      && exact::abs(tap.y2) <= silent;
        if (silentState)
            active = false;
    }
    return result;
}

namespace
{
// The variance of the first difference of three one-pole low-passes - two
// with coefficient a, then one with b - driven by unit white noise: states
// (y1, y2, d) after each update, x' = A x + B w, force = C x + D w.
double contactNoiseForceVariance(double a, double b) noexcept
{
    const double r = 1.0 - a;
    const double q = 1.0 - b;
    const double A[3][3] { { r, 0.0, 0.0 }, { a * r, r, 0.0 },
                           { b * a * r, b * r, q } };
    const double B[3] { a, a * a, b * a * a };
    const double C[3] { b * a * r, b * r, q - 1.0 };
    const double D = b * a * a;
    // (I - A (x) A) vec(P) = vec(B B'), nine unknowns by elimination.
    double M[9][10] {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            const int row = 3 * i + j;
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    M[row][3 * k + l] = (row == 3 * k + l ? 1.0 : 0.0)
                        - A[i][k] * A[j][l];
            M[row][9] = B[i] * B[j];
        }
    for (int column = 0; column < 9; ++column)
    {
        int pivot = column;
        for (int row = column + 1; row < 9; ++row)
            if (std::abs(M[row][column]) > std::abs(M[pivot][column]))
                pivot = row;
        for (int k = 0; k < 10; ++k)
            std::swap(M[column][k], M[pivot][k]);
        const double diagonal = M[column][column];
        if (!(std::abs(diagonal) > 0.0))
            return 1.0;
        for (int row = 0; row < 9; ++row)
        {
            if (row == column)
                continue;
            const double factor = M[row][column] / diagonal;
            for (int k = column; k < 10; ++k)
                M[row][k] -= factor * M[column][k];
        }
    }
    double variance = D * D;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            variance += C[i] * C[j] * M[3 * i + j][9] / M[3 * i + j][3 * i + j];
    return variance > 0.0 ? variance : 1.0;
}
} // namespace

// The noise the contact makes as a finger, nail or plectrum leaves the string
// (PhysicalCalibration's contactNoise and contactClick fields say what was
// measured and fitted). One force per pluck, from the release, along the
// stroke: renderContactNoise returns the displacement it launches into the
// string each way at the contact point, which the per-sample loop carries to
// the bridge directly and by the nut exactly as the string's own first
// arrivals travel, and leaves the click - the force's rate of change - for
// finishVoice's direct path, which the string and body never see.
void AcustraEngine::initialiseContactNoise(Voice& voice, float v,
                                          float position,
                                          float contactDistance,
                                          float releasedAmplitude,
                                          float contactWidthRatio) noexcept
{
    const auto technique = parameters_.picking;
    const bool pick = technique == PickingTechnique::Pick;
    // A natural harmonic's touching finger damps every mode its node does not
    // share, the contact's own noise with the rest (see the burst in
    // initialisePluck), so a harmonic launches none.
    const bool touched = voice.harmonic > 1;
    const float level = touched ? 0.0f
        : pick ? physicalCalibration_.contactNoisePick
               : physicalCalibration_.contactNoiseFinger;
    const float click = touched ? 0.0f
        : pick ? physicalCalibration_.contactClickPick
               : physicalCalibration_.contactClickFinger;
    voice.contactNoiseSamples = 0;
    voice.contactNoiseAmplitude = 0.0f;
    voice.contactNoiseDecay = 0.0f;
    voice.contactNoiseString = level;
    voice.contactNoiseClick = click;
    voice.contactNoiseLaunched = voice.contactNoiseForce = 0.0f;
    voice.contactNoiseAir = 0.0f;
    if (!(level > 0.0f || click > 0.0f)
        || !(voice.contactPeriodSamples > 0.0f))
        return;
    const float rate = static_cast<float>(sampleRate_);
    // The corner above which the contact smooths its own noise moves with
    // the sliding speed, the stroke's velocity, and a thumb's is lower by
    // its broader pad.
    float corner = (pick ? physicalCalibration_.pickContactNoiseCornerHz
                         : physicalCalibration_.contactNoiseCornerHz)
        * std::max(v, 0.05f);
    if (technique == PickingTechnique::Thumb)
        corner *= contactWidthRatio;
    corner = clamp(corner, 20.0f, 0.45f * rate);
    // The force is white between the string's fundamental and that corner:
    // slower changes than a period are the release itself, which the
    // pluck's shape already is, and faster ones than the corner are smoothed
    // by the contact. It is a first-order high-pass at f0 and a critically
    // damped second-order low-pass at the corner on white noise, so the
    // displacement it launches, its integral, is three one-pole low-passes:
    // one at f0 and two at the corner. The gain makes the force - the
    // launched displacement's first difference - unit RMS for the uniform
    // draw on [-1, 1] (variance 1/3): the force's variance for unit white
    // input is C P C' + D^2 with the state covariance P solving the discrete
    // Lyapunov equation P = A P A' + B B' of the three stages.
    const float fundamental = rate / voice.contactPeriodSamples;
    const double a = 1.0 - std::exp(-2.0 * static_cast<double>(pi)
        * static_cast<double>(corner) / static_cast<double>(rate));
    const double low = 1.0 - std::exp(-2.0 * static_cast<double>(pi)
        * static_cast<double>(std::min(fundamental, 0.5f * corner))
        / static_cast<double>(rate));
    const double sum = contactNoiseForceVariance(a, low);
    voice.contactNoiseCoefficient = static_cast<float>(a);
    voice.contactNoiseLowCoefficient = static_cast<float>(low);
    voice.contactNoiseGain = static_cast<float>(1.0 / std::sqrt(sum / 3.0));
    voice.contactNoiseStage1 = 0.0f;
    voice.contactNoiseStage2 = 0.0f;
    voice.contactNoiseStage3 = 0.0f;
    // A held string's force F0 = T y (1/a + 1/(L - a)) leaves as velocity
    // waves F0 / (2Z) each way; on the loop's triangle that is the apex's
    // slope step, y (1/p + 1/(1 - p)) / D per sample. The noise force is a
    // fraction of it, launched each way as the same per-sample step.
    const float p = clamp(position, 0.02f, 0.98f);
    const float held = releasedAmplitude * (1.0f / p + 1.0f / (1.0f - p))
        / voice.contactPeriodSamples;
    const float take = contactNoiseTakeSpreadDb
        * xorshiftNoise(voice.contactNoiseState);
    voice.contactNoiseAmplitude
        = std::pow(v, physicalCalibration_.contactNoiseVelocityExponent)
        * held * std::pow(10.0f, take / 20.0f);
    // The click's force-rate derivative, per 48 kHz reference sample. A
    // source radiates as a dipole, its pressure following dF/dt, only while
    // it is small against the wavelength; above c / (2 pi r) its pressure
    // follows F. The radiator is the contact region - a plectrum's tip, a
    // nail's edge - taken as r = 3 mm, which puts that corner at 18 kHz.
    const float perReference = rate / 48000.0f;
    voice.contactNoiseAirScale = perReference * perReference;
    constexpr float speedOfSound = 343.0f;
    constexpr float radiatorRadius = 0.003f;
    const float radiationCorner = std::min(
        speedOfSound / (2.0f * pi * radiatorRadius), 0.45f * rate);
    voice.contactNoiseAirCoefficient = 1.0f - std::exp(
        -2.0f * pi * radiationCorner / rate);
    voice.contactNoiseAirLowpass = 0.0f;
    // And it reaches the microphones through the air: from the contact, a
    // distance a from the bridge along the string, to the Stereo pair's
    // treble-bridge microphone 10 cm over the bridge (the body's measured
    // responses already carry their own path from the bridge).
    constexpr float microphoneHeight = 0.10f;
    const float airPath = exact::sqrt(microphoneHeight * microphoneHeight
        + contactDistance * contactDistance);
    voice.contactNoiseAirDelay = std::clamp(
        static_cast<int>(std::lround(airPath / speedOfSound * rate)), 1,
        static_cast<int>(voice.contactNoiseAirLine.size()) - 1);
    voice.contactNoiseAirLine.fill(0.0f);
    voice.contactNoiseAirWrite = 0;
    const float decaySeconds = physicalCalibration_.contactNoiseDecaySeconds;
    voice.contactNoiseDecay = std::exp(-1.0f / (decaySeconds * rate));
    // Twelve time constants, -104 dB, the stages' own ring-down, and the
    // click's flight to the microphone.
    voice.contactNoiseSamples = static_cast<int>(std::ceil(
        12.0f * decaySeconds * rate + 24.0f / static_cast<float>(a)
        + 12.0f / static_cast<float>(low))) + voice.contactNoiseAirDelay;
    // The force is along the stroke: its normal and parallel parts are the
    // pluck's own.
    voice.contactNoiseNormal = exact::sqrt(voice.polarisationMix);
    voice.contactNoiseParallel = voice.pluckParallelSign
        * exact::sqrt(1.0f - voice.polarisationMix);
    voice.contactNoiseTravel.reset(0.5f * p * voice.contactPeriodSamples,
        (1.0f - 0.5f * p) * voice.contactPeriodSamples);
}

float AcustraEngine::renderContactNoise(Voice& voice) noexcept
{
    if (voice.contactNoiseSamples <= 0)
        return 0.0f;
    if (--voice.contactNoiseSamples == 0)
    {
        voice.contactNoiseStage1 = voice.contactNoiseStage2
            = voice.contactNoiseStage3 = 0.0f;
        voice.contactNoiseLaunched = voice.contactNoiseForce = 0.0f;
        voice.contactNoiseAir = 0.0f;
        return 0.0f;
    }
    const float white = xorshiftNoise(voice.contactNoiseState)
        * voice.contactNoiseAmplitude;
    voice.contactNoiseAmplitude *= voice.contactNoiseDecay;
    const float a = voice.contactNoiseCoefficient;
    voice.contactNoiseStage1 += a * (white - voice.contactNoiseStage1);
    voice.contactNoiseStage2 += a * (voice.contactNoiseStage1
                                     - voice.contactNoiseStage2);
    voice.contactNoiseStage3 += voice.contactNoiseLowCoefficient
        * (voice.contactNoiseStage2 - voice.contactNoiseStage3);
    // The launched displacement at unit level; its first difference is the
    // force, and the force's own difference what a small source radiates.
    const float launched = voice.contactNoiseStage3 * voice.contactNoiseGain;
    const float force = launched - voice.contactNoiseLaunched;
    voice.contactNoiseAirLowpass += voice.contactNoiseAirCoefficient
        * ((force - voice.contactNoiseForce) * voice.contactNoiseAirScale
           - voice.contactNoiseAirLowpass);
    auto& line = voice.contactNoiseAirLine;
    const int size = static_cast<int>(line.size());
    line[static_cast<std::size_t>(voice.contactNoiseAirWrite)]
        = voice.contactNoiseAirLowpass;
    int read = voice.contactNoiseAirWrite - voice.contactNoiseAirDelay;
    if (read < 0)
        read += size;
    voice.contactNoiseAir = line[static_cast<std::size_t>(read)];
    voice.contactNoiseAirWrite = voice.contactNoiseAirWrite + 1 < size
        ? voice.contactNoiseAirWrite + 1 : 0;
    voice.contactNoiseLaunched = launched;
    voice.contactNoiseForce = force;
    return launched * voice.contactNoiseString;
}

ACUSTRA_NOINLINE void AcustraEngine::addContactNoise(
    Voice& voice, float& verticalIncident, float& horizontalIncident) noexcept
{
    const float noise = renderContactNoise(voice);
    if (voice.contactNoiseTravel.active || noise != 0.0f)
    {
        const auto paths = voice.contactNoiseTravel.process(noise);
        const float local = paths[0] - paths[1];
        const float vertical = voice.contactNoiseNormal * local;
        const float horizontal = voice.contactNoiseParallel * local;
        verticalIncident += voice.loops[0].appliedReleaseGain == 1.0f
            ? vertical
            : vertical * voice.loops[0].appliedReleaseGain;
        horizontalIncident += voice.loops[1].appliedReleaseGain == 1.0f
            ? horizontal
            : horizontal * voice.loops[1].appliedReleaseGain;
    }
}

ACUSTRA_NOINLINE void AcustraEngine::addTailContactNoise(
    Voice& voice, float& tailIncident, float& tailParallelIncident) noexcept
{
    const auto paths = voice.tailContactNoiseTravel.process(0.0f);
    const float local = paths[0] - paths[1];
    tailIncident += voice.tailContactNoiseNormal * local
        * voice.tailLoop.appliedReleaseGain;
    tailParallelIncident += voice.tailContactNoiseParallel * local
        * voice.tailParallelLoop.appliedReleaseGain;
}

// The key-up's own sound (EngineParameters::releaseNoise). Lifting a key is
// the hand damping the string, and a hand does not damp a vibrating string
// silently: skin lands on a string still moving under it, and the friction
// of that landing is a small random force at the contact (Akay, "Acoustics
// of friction", JASA 111 (2002)), in proportion to how fast the string moves
// there, so to how much of the note is left. It is launched into the string
// from the damping point both ways, exactly as the contact noise is
// (renderContactNoise), so it reaches the bridge and body as the string's
// own arrivals do and is damped with them by the hand's loss; nothing of it
// goes through the air. Where the hand lands is the one geometric choice:
// a fretted note is damped by the fretting finger as it relaxes, its pad
// lying against the fret side of the speaking length, so the touch sits a
// finger's width from the fret and, that close to the loop's end, excites
// the upper modes far more than the fundamental (a thin "tk", not a thud);
// an open string or a natural harmonic is stopped by the picking hand, a
// fingertip returning to where it plucked, or under Pick the palm's edge
// just in front of the saddle, broader and softer. On the four wound
// strings the lifting pad also brushes the winding: a faint band of noise
// a few milliseconds later, whose level follows the note as it was played
// rather than what is left of it (the brush happens whether or not the
// string still rings). Release velocity sets how firmly the hand lands.
// Every level, corner and time draws its own take, from the voice's own
// generator, so no two key-ups are the same and every other draw stays as
// it was. The two levels are set so that at 0.5 a key-up sounds 30-45 dB
// under its note's attack (measured on rendered notes, chords and lines at
// 44.1-96 kHz), neither fitted nor chosen by ear: no recording in the
// corpus isolates a key-up.
void AcustraEngine::startReleaseNoise(Voice& voice, int stringIndex,
                                      bool fretSide) noexcept
{
    const float amount = targetParameters_.releaseNoise;
    if (!(amount > 0.0f) || !voice.played)
        return;
    const float rate = static_cast<float>(sampleRate_);
    const float fundamental = loopFundamental(voice);
    const float period = rate / fundamental;
    if (!(period > 1.0f))
        return;
    auto& state = voice.releaseNoiseState;
    const auto draw = [&state] { return xorshiftNoise(state); };
    const float rv = voice.releaseVelocity;
    // MIDI's nominal release velocity, 64, lands as firmly as a key-up that
    // sends none; a slower lift lands softer and a faster one firmer, the
    // force going about as the lift's speed to the 3/4.
    constexpr float nominalRelease = 64.0f / 127.0f;
    const float firmness = rv >= 0.0f && rv != nominalRelease
        ? clamp(std::pow(std::max(rv, 0.02f) / nominalRelease, 0.75f),
                0.35f, 1.7f)
        : 1.0f;
    const bool pick = parameters_.picking == PickingTechnique::Pick;
    const bool thumb = parameters_.picking == PickingTechnique::Thumb;
    constexpr float scaleLength = 0.648f;
    float position = 0.0f;
    float corner = 0.0f;
    float fallSeconds = 0.0f;
    float touchLevel = 0.0f;
    float brushLevel = 0.0f;
    if (fretSide)
    {
        const float speaking = scaleLength
            * std::exp2(-static_cast<float>(voice.fret) / 12.0f);
        const float fromFret = 0.018f + 0.006f * draw();
        position = 1.0f - fromFret / speaking;
        corner = 1400.0f;
        fallSeconds = 0.010f;
        touchLevel = 1.0f;
        brushLevel = 1.0f;
    }
    else if (pick)
    {
        position = (0.028f + 0.006f * draw()) / scaleLength;
        corner = 600.0f;
        fallSeconds = 0.018f;
        touchLevel = 0.6f;
        brushLevel = 0.3f;
    }
    else
    {
        position = voice.pluckPoint > 0.0f ? voice.pluckPoint : 0.2f;
        corner = thumb ? 700.0f : 1000.0f;
        fallSeconds = 0.014f;
        touchLevel = 0.25f;
        brushLevel = 0.4f;
    }
    position = clamp(position, 0.02f, 0.98f);
    corner = clamp(corner * (1.0f + 0.15f * draw()) * std::sqrt(firmness),
                   100.0f, 0.2f * rate);
    fallSeconds *= 1.0f + 0.2f * draw();
    touchLevel *= std::pow(10.0f, 1.5f * draw() / 20.0f);
    brushLevel *= std::pow(10.0f, 2.0f * draw() / 20.0f);
    // A force F at the contact leaves as displacement steps F / (2 Z) each
    // way per 48 kHz reference sample; the bridge's own force is the level
    // the note is measured by (finishVoice), so a share of it is a level
    // relative to the note.
    const float impedance = voice.characteristicImpedance
        * voice.appliedBendImpedanceScale;
    if (!(impedance > 0.0f))
        return;
    const float unit = 48000.0f / rate / (2.0f * impedance);
    const float scale = amount / 0.5f * firmness * unit;
    constexpr float touchShare = 0.07f;
    constexpr float brushShare = 0.0005f;
    const float touchRms = touchShare * touchLevel * scale * voice.level;
    const bool wound = stringIndex <= 3;
    const float brushRms = wound
        ? brushShare * brushLevel * scale * voice.peakLevel : 0.0f;
    if (!(touchRms > 0.0f || brushRms > 0.0f))
        return;
    // Each part's envelope is the difference of a rise and a fall, zero at
    // the key-up and normalised to peak at one: the landing takes 1.5 ms,
    // the brush follows the pad's lift at 5 ms.
    const auto envelope = [rate] (float riseSeconds, float fallTime,
                                  float& riseDecay, float& fallDecay)
    {
        riseDecay = std::exp(-1.0f / (riseSeconds * rate));
        fallDecay = std::exp(-1.0f / (fallTime * rate));
        const float peakTime = std::log(fallTime / riseSeconds)
            * riseSeconds * fallTime / (fallTime - riseSeconds);
        return 1.0f / (std::exp(-peakTime / fallTime)
                       - std::exp(-peakTime / riseSeconds));
    };
    const float touchPeak = envelope(0.0015f, fallSeconds,
        voice.releaseNoiseRiseDecay, voice.releaseNoiseFallDecay);
    const float brushFall = 0.016f * (1.0f + 0.25f * draw());
    const float brushPeak = envelope(0.005f, brushFall,
        voice.releaseNoiseBrushRiseDecay, voice.releaseNoiseBrushFallDecay);
    // The touch is white under two one-pole stages at the skin's corner,
    // scaled to unit RMS for the uniform draw (variance 1/3): the pair's
    // impulse response a^2 (n + 1) r^n has energy a^4 (1 + r^2)/(1 - r^2)^3.
    const double a = 1.0 - std::exp(-2.0 * static_cast<double>(pi)
        * static_cast<double>(corner) / static_cast<double>(rate));
    const double r = 1.0 - a;
    const double pairEnergy = a * a * a * a * (1.0 + r * r)
        / ((1.0 - r * r) * (1.0 - r * r) * (1.0 - r * r));
    const float touchGain = static_cast<float>(
        1.0 / std::sqrt(pairEnergy / 3.0));
    voice.releaseNoiseCoefficient = static_cast<float>(a);
    // The brush is a band of the winding's pulse rate, higher on the finer
    // windings: a state-variable band-pass at unit peak gain, scaled to unit
    // RMS by its noise bandwidth (pi / 2) f / Q.
    constexpr std::array<float, 4> brushCentres { 2000.0f, 2400.0f,
                                                  2900.0f, 3500.0f };
    const float centre = std::min(wound
        ? brushCentres[static_cast<std::size_t>(stringIndex)]
            * (1.0f + 0.15f * draw()) : 2500.0f, 0.3f * rate);
    constexpr float q = 2.5f;
    const float g = std::tan(pi * centre / rate);
    const float k = 1.0f / q;
    voice.releaseNoiseBandA1 = 1.0f / (1.0f + g * (g + k));
    voice.releaseNoiseBandA2 = g * voice.releaseNoiseBandA1;
    voice.releaseNoiseBandA3 = g * voice.releaseNoiseBandA2;
    voice.releaseNoiseBandK = k;
    const float bandGain = 1.0f / std::sqrt(
        (0.5f * pi * centre / q) / (0.5f * rate) / 3.0f);
    voice.releaseNoiseTouch = touchRms * touchGain * touchPeak;
    voice.releaseNoiseBrush = brushRms * bandGain * brushPeak;
    voice.releaseNoiseFall = voice.releaseNoiseRise = 1.0f;
    voice.releaseNoiseBrushFall = voice.releaseNoiseBrushRise = 1.0f;
    // What a period's worth of force does is the string's own motion; the
    // launched displacement leaks at the fundamental (or half the corner),
    // which keeps it bounded as the contact noise's third stage does.
    const double low = 1.0 - std::exp(-2.0 * static_cast<double>(pi)
        * static_cast<double>(std::min(fundamental, 0.5f * corner))
        / static_cast<double>(rate));
    voice.releaseNoiseLeak = static_cast<float>(1.0 - low);
    // Twelve fall times (-104 dB) of the longer part and the stages'
    // ring-down. A touch still running from an earlier key-up is replaced:
    // its stages carry on from where they are, so nothing steps.
    const float longest = std::max(fallSeconds, brushFall);
    voice.releaseNoiseSamples = static_cast<int>(std::ceil(
        12.0f * longest * rate + 24.0f / static_cast<float>(a)
        + 12.0f / static_cast<float>(low)));
    // A finger presses down on the string, so mostly normal to the top,
    // with the pad's slip across it the parallel share.
    voice.releaseNoiseNormal = 0.8660254f;
    voice.releaseNoiseParallel = 0.5f;
    if (!voice.releaseNoiseTravel.active)
        voice.releaseNoiseTravel.reset(0.5f * position * period,
                                       (1.0f - 0.5f * position) * period);
}

float AcustraEngine::renderReleaseNoise(Voice& voice) noexcept
{
    if (voice.releaseNoiseSamples <= 0)
        return 0.0f;
    if (--voice.releaseNoiseSamples == 0)
    {
        voice.releaseNoiseTouch = voice.releaseNoiseBrush = 0.0f;
        voice.releaseNoiseStage1 = voice.releaseNoiseStage2 = 0.0f;
        voice.releaseNoiseBand1 = voice.releaseNoiseBand2 = 0.0f;
        voice.releaseNoiseLaunched = 0.0f;
        return 0.0f;
    }
    const float touch = voice.releaseNoiseTouch
        * (voice.releaseNoiseFall - voice.releaseNoiseRise);
    voice.releaseNoiseFall *= voice.releaseNoiseFallDecay;
    voice.releaseNoiseRise *= voice.releaseNoiseRiseDecay;
    const float white = xorshiftNoise(voice.releaseNoiseState) * touch;
    const float a = voice.releaseNoiseCoefficient;
    voice.releaseNoiseStage1 += a * (white - voice.releaseNoiseStage1);
    voice.releaseNoiseStage2 += a * (voice.releaseNoiseStage1
                                     - voice.releaseNoiseStage2);
    float force = voice.releaseNoiseStage2;
    if (voice.releaseNoiseBrush != 0.0f)
    {
        const float brush = voice.releaseNoiseBrush
            * (voice.releaseNoiseBrushFall - voice.releaseNoiseBrushRise);
        voice.releaseNoiseBrushFall *= voice.releaseNoiseBrushFallDecay;
        voice.releaseNoiseBrushRise *= voice.releaseNoiseBrushRiseDecay;
        const float input = xorshiftNoise(voice.releaseNoiseState) * brush;
        const float v3 = input - voice.releaseNoiseBand2;
        const float v1 = voice.releaseNoiseBandA1 * voice.releaseNoiseBand1
                       + voice.releaseNoiseBandA2 * v3;
        const float v2 = voice.releaseNoiseBand2
            + voice.releaseNoiseBandA2 * voice.releaseNoiseBand1
            + voice.releaseNoiseBandA3 * v3;
        voice.releaseNoiseBand1 = 2.0f * v1 - voice.releaseNoiseBand1;
        voice.releaseNoiseBand2 = 2.0f * v2 - voice.releaseNoiseBand2;
        force += voice.releaseNoiseBandK * v1;
    }
    voice.releaseNoiseLaunched = voice.releaseNoiseLeak
        * voice.releaseNoiseLaunched + force;
    return voice.releaseNoiseLaunched;
}

ACUSTRA_NOINLINE void AcustraEngine::addReleaseNoise(
    Voice& voice, float& verticalIncident, float& horizontalIncident) noexcept
{
    const float noise = renderReleaseNoise(voice);
    if (voice.releaseNoiseTravel.active || noise != 0.0f)
    {
        const auto paths = voice.releaseNoiseTravel.process(noise);
        const float local = paths[0] - paths[1];
        verticalIncident += voice.releaseNoiseNormal * local
            * voice.loops[0].appliedReleaseGain;
        horizontalIncident += voice.releaseNoiseParallel * local
            * voice.loops[1].appliedReleaseGain;
    }
}

float AcustraEngine::renderExcitation(Voice& voice) noexcept
{
    float excitation = 0.0f;
    if (voice.excitationEnvelope > 1.0e-8f)
    {
        const float rateRatio = static_cast<float>(sampleRate_) / 48000.0f;
        const float noise = xorshiftNoise(voice.excitationNoiseState)
            * exact::sqrt(rateRatio);
        if (voice.excitationWhite)
            excitation = noise * voice.excitationEnvelope;
        else
        {
            const float excitationCoefficient = voice.excitationReleaseCoefficient;
            voice.excitationLowpass += excitationCoefficient
                * (noise - voice.excitationLowpass);
            if (voice.excitationSoft)
            {
                voice.excitationLowpass2 += excitationCoefficient
                    * (voice.excitationLowpass - voice.excitationLowpass2);
                excitation = voice.excitationLowpass2 * voice.excitationEnvelope;
            }
            else
                excitation = (voice.excitationLowpass
                    + 0.16f * voice.excitationColour
                        * (noise - voice.excitationLowpass))
                    * voice.excitationEnvelope;
        }
        voice.excitationEnvelope *= voice.excitationDecay;
    }
    return excitation;
}

void AcustraEngine::finishVoice(Voice& voice, int stringIndex,
                                float verticalIncident,
                                float horizontalIncident, float excitation,
                                float tailIncident,
                                float tailParallelIncident,
                                float bridgeDisplacement,
                                float bridgeVelocity,
                                float horizontalBridgeDisplacement,
                                float& directLeft,
                                float& directRight,
                                float& longitudinalForce) noexcept
{
    // A rigid bridge and nut each invert a displacement wave, so the collapsed
    // full-round-trip loop writes +incident.  A moving bridge has reflected
    // wave b=x-a; folding in the nut inversion therefore writes a-x.
    // The local-contact paths have already entered incident waves before
    // the junction solve. Writing this source again would duplicate energy.
    const float boundaryExcitation = voice.contactTravelEnabled ? 0.0f : excitation;
    voice.loops[0].write(verticalIncident - bridgeDisplacement
                         + 0.76f * boundaryExcitation);
    // The crown's sideways motion is (h/a) times the rocking; on a bridge
    // whose rocking was not measured it never moves, and the parallel loop
    // reflects rigidly as it always did.
    voice.loops[1].write((horizontalBridgeDisplacement != 0.0f
                              ? horizontalIncident - horizontalBridgeDisplacement
                              : horizontalIncident)
                         + voice.excitationParallelGain * boundaryExcitation);
    if (voice.loops[0].gestureContact.active)
    {
        if (voice.returnSamples > 0
            && voice.returnSamples <= voice.loops[0].gestureContact.withdrawalSamples + 1)
            for (auto& loop : voice.loops)
                loop.gestureContact.withdraw(voice.returnSamples);
        voice.loops[0].applyGestureContact();
        voice.loops[1].applyGestureContact();
    }

    const float sampleRateRatio = static_cast<float>(sampleRate_) / 48000.0f;
    const float verticalVelocity = voice.loops[0].bridgeVelocity(
        verticalIncident, sampleRateRatio);
    const float horizontalVelocity = voice.loops[1].bridgeVelocity(
        horizontalIncident, sampleRateRatio);
    // The squared slope is the same quantity for both materials; only the
    // pitch surrogate above it is steel-only.
    const float referenceRate48 = 48000.0f / static_cast<float>(sampleRate_);
    const float slopeV = voice.loops[0].currentDelay
                       * referenceRate48 * verticalVelocity;
    const float slopeH = voice.loops[1].currentDelay
                       * referenceRate48 * horizontalVelocity;
    const float slopeEnergy = slopeV * slopeV + slopeH * slopeH;
    if (physicalCalibration_.longitudinalGain > 0.0f && voice.played)
    {
        // Stretching the string adds tension, and that tension is a
        // longitudinal wave with the string's own axial resonances. The drive
        // is a square, so it carries the products of transverse partials: what
        // comes out are the sum and difference phantom partials rather than an
        // added tone.
        for (int mode = 0; mode < Voice::longitudinalModeCount; ++mode)
        {
            const float modeDrive = voice.longitudinalB0[mode]
                * voice.longitudinalDrive * slopeEnergy;
            const float output = modeDrive
                + voice.longitudinalA1[mode] * voice.longitudinalY1[mode]
                + voice.longitudinalA2[mode] * voice.longitudinalY2[mode];
            voice.longitudinalY2[mode] = voice.longitudinalY1[mode];
            voice.longitudinalY1[mode]
                = exact::isfinite(output) ? output : 0.0f;
            longitudinalForce += physicalCalibration_.longitudinalGain
                * voice.longitudinalY1[mode];
        }
    }
    if (voice.played)
    {
        const float referenceRate = 48000.0f / static_cast<float>(sampleRate_);
        const float verticalSlope = voice.loops[0].currentDelay
                                  * referenceRate * verticalVelocity;
        const float horizontalSlope = voice.loops[1].currentDelay
                                    * referenceRate * horizontalVelocity;
        const float rawEnergy = verticalSlope * verticalSlope
                              + horizontalSlope * horizontalSlope;
        if (exact::bits(voice.loops[0].currentDelay) != voice.observedSlopeDelay)
        {
            voice.observedSlopeAlpha = 1.0f - std::exp(
                -1.0f / std::max(voice.loops[0].currentDelay, 1.0f));
            voice.observedSlopeDelay = exact::bits(voice.loops[0].currentDelay);
        }
        const float alpha = voice.observedSlopeAlpha;
        const float observed = voice.observedSlopeEnergy
            + alpha * (rawEnergy - voice.observedSlopeEnergy);
        voice.observedSlopeEnergy = exact::isfinite(observed)
            ? std::max(observed, 0.0f) : 0.0f;
        voice.attackSlopeEnergy = exact::isfinite(voice.attackSlopeEnergy)
            ? std::min(std::max(voice.attackSlopeEnergy, 0.0f),
                       voice.observedSlopeEnergy)
            : 0.0f;
    }
    const float impedance = voice.characteristicImpedance
                          * voice.appliedBendImpedanceScale;
    if (voice.tailActive)
    {
        // This independently retained branch receives a full bridge return
        // and contributes its own captured impedance to the junction; its
        // parallel plane reads the crown's sideways motion as the voice's
        // does.
        voice.tailLoop.write(tailIncident - bridgeDisplacement);
        voice.tailParallelLoop.write(horizontalBridgeDisplacement != 0.0f
            ? tailParallelIncident - horizontalBridgeDisplacement
            : tailParallelIncident);
        if (voice.tailLoop.gestureContact.active)
        {
            voice.tailLoop.applyGestureContact();
            voice.tailParallelLoop.applyGestureContact();
        }
        const float tailVelocity = voice.tailLoop.bridgeVelocity(
            tailIncident, sampleRateRatio);
        const float tailForce = voice.tailCharacteristicImpedance
            * (2.0f * tailVelocity - bridgeVelocity);
        voice.tailLevel += levelSmoothing_
            * (exact::abs(tailForce) - voice.tailLevel);
        if (voice.tailLevel < 2.0e-7f)
            ++voice.tailQuietSamples;
        else
            voice.tailQuietSamples = 0;
        if (voice.tailQuietSamples > static_cast<int>(0.08 * sampleRate_)
            && !voice.tailContactTravel.active
            && !voice.tailContactNoiseTravel.active
            && !voice.tailLegatoContactTravel.active && voice.tailLegatoContactSamples == 0
            && voice.tailRepluckArrivals.remaining == 0)
            voice.tailRetiring = true;
        // A retiring tail's port leaves the junction on the delay's own
        // 6 ms time constant, as a bend's impedance moves (the junction sums
        // the ports every sample). Quiet as its own wave is, the tail is a
        // lossy port on the bridge: removed in one sample, the impedance
        // sum stepped and so did the bridge motion every string reads, a
        // tick under anything else sounding. At 1e-4 of the string's port,
        // about 55 ms on, it is dropped.
        if (voice.tailRetiring)
            voice.tailCharacteristicImpedance
                -= delaySmoothing_ * voice.tailCharacteristicImpedance;
        if (voice.tailRetiring && !voice.tailContactTravel.active
            && !voice.tailContactNoiseTravel.active
            && !voice.tailLegatoContactTravel.active && voice.tailLegatoContactSamples == 0
            && voice.tailRepluckArrivals.remaining == 0
            && !(voice.tailCharacteristicImpedance
                 > 1.0e-4f * voice.characteristicImpedance))
        {
            voice.tailActive = false;
            voice.tailRetiring = false;
            voice.tailContactTravel.active = false;
            voice.tailContactNoiseTravel.active = false;
            voice.tailLegatoContactTravel.active = false;
            voice.tailLegatoContactSamples = 0;
            voice.tailCharacteristicImpedance = 0.0f;
            voice.tailLevel = 0.0f;
            voice.tailQuietSamples = 0;
            voice.tailLoop.reset();
            voice.tailParallelLoop.reset();
            voice.tailRepluckArrivals.clear();
        }
    }
    const float localReactionForce = impedance
        * (2.0f * verticalVelocity - bridgeVelocity);
    const float directForce = localReactionForce * voice.polarisationMix
        + 0.44f * impedance * horizontalVelocity
            * (1.0f - voice.polarisationMix);

    const float pan = (static_cast<float>(stringIndex) - 2.5f) / 2.5f;
    // The measured force-to-pressure bank is the acoustic source. Retain only
    // a very quiet bridge-local component; the previous amplified contact
    // residual exposed the periodic string waveform as a harpsichord cue.
    // The tool's own click reaches the microphones through the air, not
    // through the string or the body: a small source at the contact whose
    // pressure follows its force's rate of change (renderContactNoise).
    // Without a click the sum is the product alone (adding zero changes at
    // most the sign of a zero, which the stereo sums below absorb).
    float direct = physicalCalibration_.directGain * directForce;
    if (voice.contactNoiseSamples > 0)
    {
        const float click = voice.contactNoiseClick * 2.0f * impedance
                          * voice.contactNoiseAir;
        direct = physicalCalibration_.directGain * directForce + click;
    }
    directLeft += direct * (1.0f - 0.18f * pan);
    directRight += direct * (1.0f + 0.18f * pan);

    const float magnitude = exact::abs(localReactionForce);
    voice.level += levelSmoothing_ * (magnitude - voice.level);
    voice.peakLevel = std::max(voice.peakLevel, voice.level);
    // A released string is handed back once its release damping has had its
    // T60 and the hand's 80 ms; waiting for it to fall silent would wait for
    // ever, because the bridge keeps driving it while anything else sounds.
    // What it still carries goes on as an idle string's wave.
    if (voice.played && !voice.keyDown && !voice.pedalHeld
        && voice.returnSamples > 0 && --voice.returnSamples == 0)
        returnToOpenString(voice, stringIndex, false);

}

void AcustraEngine::BodyBank::CaptureFilter::configure(double sampleRate,
                                                     bool bellido) noexcept
{
    if (enabled != bellido)
        reset();
    enabled = bellido;
    if (!enabled)
    {
        for (auto& section : coefficients)
            section = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        return;
    }
    // A recording-supported broad capture contour compensates the nearfield
    // observation's 250-630 Hz excess and 0.8-1.6 kHz deficit. This authored
    // microphone voicing does not simulate an exact distance or notch an
    // individual mode: every mechanical/radiation pole and residue is intact.
    // Shape and Wood retain its history; model fades copy it with the bank.
    constexpr std::array<double, sections> frequency { 500.0, 1400.0 };
    constexpr std::array<double, sections> gainDb { -6.0, 6.0 };
    constexpr double q = 1.2;
    constexpr double doublePi = 3.141592653589793238462643383279502884;
    for (int index = 0; index < sections; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);
        const double amplitude = std::pow(10.0, gainDb[slot] / 40.0);
        const double omega = 2.0 * doublePi * frequency[slot] / sampleRate;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * q);
        const double a0 = 1.0 + alpha / amplitude;
        coefficients[slot] = {
            static_cast<float>((1.0 + alpha * amplitude) / a0),
            static_cast<float>(-2.0 * cosine / a0),
            static_cast<float>((1.0 - alpha * amplitude) / a0),
            static_cast<float>(-2.0 * cosine / a0),
            static_cast<float>((1.0 - alpha / amplitude) / a0)
        };
    }
}

void AcustraEngine::BodyBank::CaptureFilter::render(BodyOutput& output) noexcept
{
    // Original bypasses the arithmetic entirely, preserving its sample bits.
    if (!enabled)
        return;
    const auto filter = [] (float input, const std::array<float, 5>& coefficient,
                           std::array<float, 2>& state)
    {
        const float result = coefficient[0] * input + state[0];
        const float first = coefficient[1] * input - coefficient[3] * result
                          + state[1];
        const float second = coefficient[2] * input - coefficient[4] * result;
        state[0] = exact::abs(first) < 1.0e-30f ? 0.0f : first;
        state[1] = exact::abs(second) < 1.0e-30f ? 0.0f : second;
        return result;
    };
    for (int index = 0; index < sections; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);
        output.left = filter(output.left, coefficients[slot], stateLeft[slot]);
        output.right = filter(output.right, coefficients[slot], stateRight[slot]);
    }
}

void AcustraEngine::BodyBank::load(const std::array<BodyMode, bodyModeCount>& modes,
                                   int modeCount, int orderedCount,
                                   bool resetStates) noexcept
{
    if (resetStates)
        captureFilter.reset();
    count = std::clamp(modeCount, 0, bodyModeCount);
    ordered = std::clamp(orderedCount, 0, count);
    for (int index = 0; index < capacity; ++index)
    {
        const auto slot = static_cast<std::size_t>(index);
        const BodyMode mode = index < count ? modes[slot] : BodyMode {};
        poleReal[slot] = mode.poleReal;
        poleImaginary[slot] = mode.poleImaginary;
        leftReal[slot] = mode.leftReal;
        leftImaginary[slot] = mode.leftImaginary;
        rightReal[slot] = mode.rightReal;
        rightImaginary[slot] = mode.rightImaginary;
        leftMomentReal[slot] = mode.leftMomentReal;
        leftMomentImaginary[slot] = mode.leftMomentImaginary;
        rightMomentReal[slot] = mode.rightMomentReal;
        rightMomentImaginary[slot] = mode.rightMomentImaginary;
        if (resetStates || index >= count)
            real[slot] = imaginary[slot] = momentReal[slot] = momentImaginary[slot] = 0.0f;
    }
}

// Each mode's two-pole step and the flush of tiny states, four modes at a time.
// Each lane is the scalar expression's own sequence of IEEE operations (the
// same products, sums and order), so each mode's states and contributions
// are the scalar ones bit for bit; the two sums then take the ordered modes'
// contributions one mode at a time in index order, as the scalar loop did.
// The modes after them (the steel blend's parallel parts) gather in four
// vector accumulators, added across lanes and to the sums once per sample
// (about 1.03 against 1.33 ns per mode-sample). Only modes below count are
// summed; lanes past it are zero padding.
AcustraEngine::BodyOutput AcustraEngine::BodyBank::render(float force, float moment) noexcept
{
    BodyOutput output;
#if defined(__clang__) || defined(__GNUC__)
    typedef float Vector __attribute__((vector_size(16)));
    typedef std::int32_t Mask __attribute__((vector_size(16)));
    const auto load = [] (const Lanes& lanes, int index)
    {
        Vector value;
        __builtin_memcpy(&value, lanes.data() + index, sizeof(value));
        return value;
    };
    const auto store = [] (Lanes& lanes, int index, Vector value)
    {
        __builtin_memcpy(lanes.data() + index, &value, sizeof(value));
    };
    // |x| < 1e-30 is exactly -1e-30 < x < 1e-30, NaN included (false).
    const auto flush = [] (Vector value)
    {
        const Mask tiny = (value < 1.0e-30f) & (value > -1.0e-30f);
        return reinterpret_cast<Vector>(reinterpret_cast<Mask>(value) & ~tiny);
    };
    const Vector forceLanes = { force, force, force, force };
    const Vector momentLanes = { moment, moment, moment, moment };
    // One group of four lanes advanced; its contributions to the two sums.
    const auto advance = [&] (int index, Vector& leftPart, Vector& rightPart)
    {
        const Vector pr = load(poleReal, index);
        const Vector pim = load(poleImaginary, index);
        const Vector re = load(real, index);
        const Vector im = load(imaginary, index);
        const Vector mr = load(momentReal, index);
        const Vector mi = load(momentImaginary, index);
        const Vector nextReal = forceLanes + pr * re - pim * im;
        const Vector nextImaginary = pim * re + pr * im;
        const Vector nextMomentReal = momentLanes + pr * mr
                                    - pim * mi;
        const Vector nextMomentImaginary = pim * mr
                                         + pr * mi;
        leftPart = load(leftReal, index) * nextReal
            - load(leftImaginary, index) * nextImaginary
            + load(leftMomentReal, index) * nextMomentReal
            - load(leftMomentImaginary, index) * nextMomentImaginary;
        rightPart = load(rightReal, index) * nextReal
            - load(rightImaginary, index) * nextImaginary
            + load(rightMomentReal, index) * nextMomentReal
            - load(rightMomentImaginary, index) * nextMomentImaginary;
        store(real, index, flush(nextReal));
        store(imaginary, index, flush(nextImaginary));
        store(momentReal, index, flush(nextMomentReal));
        store(momentImaginary, index, flush(nextMomentImaginary));
    };
    // One ordered group, the first valid of its lanes summed in order. A
    // constant valid lets the full groups keep their products in vector
    // registers.
    const auto group = [&] (int index, int valid)
    {
        Vector leftPart, rightPart;
        advance(index, leftPart, rightPart);
        for (int lane = 0; lane < valid; ++lane)
        {
            output.left += 2.0f * leftPart[lane];
            output.right += 2.0f * rightPart[lane];
        }
    };
    int index = 0;
    for (; index + lanes <= ordered; index += lanes)
        group(index, lanes);
    if (index < ordered)
    {
        group(index, ordered - index);
        index += lanes;
    }
    if (index < count)
    {
        Vector leftSum = { 0.0f, 0.0f, 0.0f, 0.0f };
        Vector rightSum = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (; index < count; index += lanes)
        {
            Vector leftPart, rightPart;
            advance(index, leftPart, rightPart);
            if (index + lanes > count)
            {
                const Mask lane = { 0, 1, 2, 3 };
                const Mask summed = lane < (count - index);
                leftPart = reinterpret_cast<Vector>(reinterpret_cast<Mask>(leftPart) & summed);
                rightPart = reinterpret_cast<Vector>(reinterpret_cast<Mask>(rightPart) & summed);
            }
            leftSum += leftPart;
            rightSum += rightPart;
        }
        output.left += 2.0f * ((leftSum[0] + leftSum[1]) + (leftSum[2] + leftSum[3]));
        output.right += 2.0f * ((rightSum[0] + rightSum[1]) + (rightSum[2] + rightSum[3]));
    }
#else
    for (int index = 0; index < count; ++index)
    {
        const auto i = static_cast<std::size_t>(index);
        const float nextReal = force + poleReal[i] * real[i] - poleImaginary[i] * imaginary[i];
        const float nextImaginary = poleImaginary[i] * real[i] + poleReal[i] * imaginary[i];
        const float nextMomentReal = moment + poleReal[i] * momentReal[i]
                                   - poleImaginary[i] * momentImaginary[i];
        const float nextMomentImaginary = poleImaginary[i] * momentReal[i]
                                        + poleReal[i] * momentImaginary[i];
        output.left += 2.0f * (leftReal[i] * nextReal - leftImaginary[i] * nextImaginary
            + leftMomentReal[i] * nextMomentReal - leftMomentImaginary[i] * nextMomentImaginary);
        output.right += 2.0f * (rightReal[i] * nextReal - rightImaginary[i] * nextImaginary
            + rightMomentReal[i] * nextMomentReal - rightMomentImaginary[i] * nextMomentImaginary);
        real[i] = exact::abs(nextReal) < 1.0e-30f ? 0.0f : nextReal;
        imaginary[i] = exact::abs(nextImaginary) < 1.0e-30f ? 0.0f : nextImaginary;
        momentReal[i] = exact::abs(nextMomentReal) < 1.0e-30f ? 0.0f : nextMomentReal;
        momentImaginary[i] = exact::abs(nextMomentImaginary) < 1.0e-30f
            ? 0.0f : nextMomentImaginary;
    }
#endif
    captureFilter.render(output);
    // The mono microphone's sum is the right channel's: configureBody gives
    // them the same residues.
    output.upper = output.right;
    return output;
}

AcustraEngine::BodyOutput AcustraEngine::renderBody(float bridgeInput,
                                                   float bodyMoment) noexcept
{
    BodyOutput result = bodyBank_.render(bridgeInput, bodyMoment);
    if (bodyModelFade_ < 1.0f)
    {
        const BodyOutput previous = fadingBodyBank_.render(bridgeInput, bodyMoment);
        const float mix = bodyModelFade_;
        result.left = previous.left + mix * (result.left - previous.left);
        result.right = previous.right + mix * (result.right - previous.right);
        result.upper = previous.upper + mix * (result.upper - previous.upper);
        bodyModelFade_ = std::min(1.0f, bodyModelFade_ + bodyModelFadeStep_);
    }
    if (bodyUpdatePending_ && bodyModelFade_ >= 1.0f)
        configureBody();
    return result;
}

// The saddle filter: the analog F_p / F_r (PiezoDesign) with its poles
// mapped exactly, z = e^{sT}, and its zeros (four coefficients, five from
// 88.2 kHz) fitted by weighted least squares to the analog response times
// the poles' denominator over 256 log-spaced points from 20 Hz to
// min(20 kHz, 0.45 fs), weighted by |H|^-1/2, with the DC gain held at
// exactly 1. Bilinear designs fold the whole band above the 6 kHz peak
// toward Nyquist, and a two-zero matched design (Vicanek) cannot place the
// analog zeros: 55-60 degrees off at 10 kHz. Below 16 kHz a pole at 6 kHz
// would alias, so there the filter is four taps with no poles.
AcustraEngine::PiezoSaddleFilter
AcustraEngine::designPiezoSaddle(double sampleRate, double impedance) noexcept
{
    using D = PiezoDesign;
    const double omega0 = 2.0 * piDouble * D::elementHz;
    const double stiffness = D::saddleMass * omega0 * omega0;
    const double loss = D::elementLoss * stiffness / omega0;
    const double g = D::bridgeConductance;
    // H(s) = (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0)
    const double n2 = loss * g * D::saddleMass;
    const double n1 = loss * (1.0 + g * impedance) + stiffness * g * D::saddleMass;
    const double n0 = stiffness * (1.0 + g * impedance);
    const double d2 = n2 + D::saddleMass;
    const double d1 = n1 + impedance;
    const double d0 = n0;
    const double period = 1.0 / sampleRate;
    double a1 = 0.0, a2 = 0.0;
    if (sampleRate >= 16000.0)
    {
        const double radius = std::exp(-0.5 * d1 / d2 * period);
        const double damped = 0.5 * std::sqrt(std::max(0.0, 4.0 * d2 * d0 - d1 * d1)) / d2;
        a1 = -2.0 * radius * std::cos(damped * period);
        a2 = radius * radius;
    }
    const int taps = sampleRate >= 88200.0 ? 5 : 4;
    const int freeTaps = taps - 1;
    const double dc = 1.0 + a1 + a2;
    const double bottom = 20.0;
    const double top = std::min(20000.0, 0.45 * sampleRate);
    const double logSpan = std::log(top / bottom);
    std::array<double, 16> normal {};
    std::array<double, 4> right {};
    constexpr int points = 256;
    for (int point = 0; point < points; ++point)
    {
        const double frequency = bottom * std::exp(logSpan * point / (points - 1));
        const double omega = 2.0 * piDouble * frequency;
        const double numRe = n0 - n2 * omega * omega, numIm = n1 * omega;
        const double denRe = d0 - d2 * omega * omega, denIm = d1 * omega;
        const double denNorm = denRe * denRe + denIm * denIm;
        const double hRe = (numRe * denRe + numIm * denIm) / denNorm;
        const double hIm = (numIm * denRe - numRe * denIm) / denNorm;
        std::array<double, 5> zRe {}, zIm {};
        for (int k = 0; k < taps; ++k)
        {
            zRe[static_cast<std::size_t>(k)] = std::cos(k * omega * period);
            zIm[static_cast<std::size_t>(k)] = -std::sin(k * omega * period);
        }
        const double aRe = 1.0 + a1 * zRe[1] + a2 * zRe[2];
        const double aIm = a1 * zIm[1] + a2 * zIm[2];
        const double targetRe0 = hRe * aRe - hIm * aIm;
        const double targetIm0 = hRe * aIm + hIm * aRe;
        const double hMagnitude = std::sqrt(hRe * hRe + hIm * hIm);
        const double weight = 1.0 / (hMagnitude * (aRe * aRe + aIm * aIm));
        const auto last = static_cast<std::size_t>(freeTaps);
        // b_last = dc - sum of the others: the freeTaps taps multiply
        // z^-k - z^-last, and dc z^-last moves to the target.
        const double targetRe = targetRe0 - dc * zRe[last];
        const double targetIm = targetIm0 - dc * zIm[last];
        for (int j = 0; j < freeTaps; ++j)
        {
            const auto uj = static_cast<std::size_t>(j);
            const double pjRe = zRe[uj] - zRe[last], pjIm = zIm[uj] - zIm[last];
            right[uj] += weight * (pjRe * targetRe + pjIm * targetIm);
            for (int l = 0; l < freeTaps; ++l)
            {
                const auto ul = static_cast<std::size_t>(l);
                const double plRe = zRe[ul] - zRe[last], plIm = zIm[ul] - zIm[last];
                normal[uj * 4 + ul] += weight * (pjRe * plRe + pjIm * plIm);
            }
        }
    }
    // Cholesky, in place: normal = L L^T.
    for (int j = 0; j < freeTaps; ++j)
    {
        const auto uj = static_cast<std::size_t>(j);
        double diagonal = normal[uj * 4 + uj];
        for (int k = 0; k < j; ++k)
            diagonal -= normal[uj * 4 + static_cast<std::size_t>(k)]
                * normal[uj * 4 + static_cast<std::size_t>(k)];
        diagonal = std::sqrt(std::max(diagonal, 1.0e-300));
        normal[uj * 4 + uj] = diagonal;
        for (int i = j + 1; i < freeTaps; ++i)
        {
            const auto ui = static_cast<std::size_t>(i);
            double value = normal[ui * 4 + uj];
            for (int k = 0; k < j; ++k)
                value -= normal[ui * 4 + static_cast<std::size_t>(k)]
                    * normal[uj * 4 + static_cast<std::size_t>(k)];
            normal[ui * 4 + uj] = value / diagonal;
        }
    }
    std::array<double, 4> solution {};
    for (int i = 0; i < freeTaps; ++i)
    {
        const auto ui = static_cast<std::size_t>(i);
        double value = right[ui];
        for (int k = 0; k < i; ++k)
            value -= normal[ui * 4 + static_cast<std::size_t>(k)]
                * solution[static_cast<std::size_t>(k)];
        solution[ui] = value / normal[ui * 4 + ui];
    }
    for (int i = freeTaps - 1; i >= 0; --i)
    {
        const auto ui = static_cast<std::size_t>(i);
        double value = solution[ui];
        for (int k = i + 1; k < freeTaps; ++k)
            value -= normal[static_cast<std::size_t>(k) * 4 + ui]
                * solution[static_cast<std::size_t>(k)];
        solution[ui] = value / normal[ui * 4 + ui];
    }
    PiezoSaddleFilter filter;
    double sum = 0.0;
    for (int k = 0; k < freeTaps; ++k)
    {
        filter.b[static_cast<std::size_t>(k)]
            = static_cast<float>(solution[static_cast<std::size_t>(k)]);
        sum += solution[static_cast<std::size_t>(k)];
    }
    filter.b[static_cast<std::size_t>(freeTaps)] = static_cast<float>(dc - sum);
    filter.a1 = static_cast<float>(a1);
    filter.a2 = static_cast<float>(a2);
    return filter;
}

void AcustraEngine::resetPiezo() noexcept
{
    piezoSaddleInput_.fill(0.0f);
    piezoSaddleOutput_.fill(0.0f);
    piezoFrontW_ = piezoFrontC2_ = 0.0;
    piezoLastOpen_ = piezoLastClamp_ = 0.0;
    piezoC3_ = piezoC4_ = piezoC5_ = 0.0;
    piezoLastBuffer_ = piezoLastStage_ = 0.0;
    piezoDrive_.fill(0.0);
    piezoStage_.fill(0.0);
    piezoOutputVolts_ = 0.0;
    lastPiezoOpen_ = lastPiezoVoltage_ = lastPiezoInput_ = lastPiezoDrive_ = 0.0f;
}


// The under-saddle piezo, from the rigid-saddle force its strings press on
// it to the voltage a DI takes from its preamp, as the circuit PiezoDesign
// lists (Docs/decisions.md 2026-09-29). Each stage drives the next without
// being loaded by it - op-amp inputs draw no current, and U1B's input stays
// in its range whenever its output is not already at a rail - so they are
// solved in turn, each as deviations from the 4.5 V operating point, and a
// silent input maps to exact zero. The chain runs every sample whatever
// Capture selects, so a switch or a newly cabled output lands on warm state.
// Its output is seven samples behind its input (the BLAMP's look-ahead) at
// every rate. The result is in force units: dividing by the element's
// sensitivity and the preamp's mid-band gain leaves the trim alone there.
// PiezoDesign item 2 at run time: the engine's force unit in newtons is the
// calibration's displacement unit (the strings' own) per 48 kHz sample, so
// the piezo moves with the strings when a calibration is set. The level
// match divides the unit out again with the element's sensitivity and the
// preamp's mid-band gain, so it moves the voltages, not the linear level.
void AcustraEngine::configurePiezoUnit() noexcept
{
    using D = PiezoDesign;
    piezoNewtonsPerUnit_ = static_cast<double>(std::max(
        physicalCalibration_.steelDisplacementScaleMetres, 1.0e-4f)) * 48000.0;
    // Below 5 pN - a picovolt at the element, some 130 dB under the thermal
    // noise of its own capacitance - the force is last-bit rounding in the
    // saddle sums, which after a release keep it near 1e-15 forever.
    piezoForceFloor_ = static_cast<float>(5.0e-12 / piezoNewtonsPerUnit_);
    piezoSaddleFloor_ = static_cast<float>(5.0e-8 / piezoNewtonsPerUnit_);
    if (piezoMidbandGain_ > 0.0)
        piezoOutputScale_ = 1.0 / (D::voltsPerNewton * piezoNewtonsPerUnit_
                                   * piezoMidbandGain_);
}

float AcustraEngine::renderPiezo(float force) noexcept
{
    using D = PiezoDesign;
    const float forceFloor = piezoForceFloor_;
    // A non-finite force would stay in every state; captureMix_ times NaN
    // would then reach Main even with the piezo unheard.
    if (!exact::isfinite(force))
    {
        resetPiezo();
        return 0.0f;
    }
    const float input = exact::abs(force) < forceFloor ? 0.0f : force;
    // 1. The saddle on its element (designPiezoSaddle), direct form I.
    const auto& saddle = piezoSaddle_;
    auto& x = piezoSaddleInput_;
    auto& y = piezoSaddleOutput_;
    x[4] = x[3];
    x[3] = x[2];
    x[2] = x[1];
    x[1] = x[0];
    x[0] = input;
    const float pressed = saddle.b[0] * x[0] + saddle.b[1] * x[1] + saddle.b[2] * x[2]
        + saddle.b[3] * x[3] + saddle.b[4] * x[4] - saddle.a1 * y[0] - saddle.a2 * y[1];
    y[1] = y[0];
    y[0] = pressed;
    // 2. The element's open-circuit voltage.
    const double open = D::voltsPerNewton * piezoNewtonsPerUnit_ * static_cast<double>(pressed);
    // 3. The input section (trapezoidal; U1A's output stopping at its input
    // range enters a sample late, through C2's bootstrap, 0.13 s).
    const double drive = open + piezoLastOpen_;
    const double clampSum = 2.0 * piezoLastClamp_;
    const double w = piezoFrontA00_ * piezoFrontW_ + piezoFrontA01_ * piezoFrontC2_
        + piezoFrontB0_ * drive + piezoFrontE0_ * clampSum;
    piezoFrontC2_ = piezoFrontA10_ * piezoFrontW_ + piezoFrontA11_ * piezoFrontC2_
        + piezoFrontB1_ * drive + piezoFrontE1_ * clampSum;
    piezoFrontW_ = w;
    double bufferInput = w + piezoInputShare_ * open;
    const double freeInput = bufferInput;
    // Past U1A's input range its output stops at the limit and D1/D2 see
    // the rest: they conduct as Shockley diodes, taking from the IN node,
    // over one sample, the charge Ceq z that brings the diode's voltage
    // x - z to what that current needs (backward Euler), tabulated in
    // prepare() (piezoDiodeDump_). C1 keeps the charge it gave up: the bias
    // shift a hard overload leaves, recovering over the input network's
    // 1 Hz modes. For a sine none of this runs below 1.9 dB over U1B's
    // positive swing or 0.35 dB over its negative one (U1B's gain of 1.62);
    // no playing reaches it (the player's hardest Pick strums keep 1.2 dB).
    const double excess = (bufferInput > 0.0 ? bufferInput : -bufferInput) - D::commonModeLimit;
    if (excess > 0.0)
    {
        double dump;
        constexpr double top = piezoDiodeStep * (piezoDiodePoints - 1);
        if (excess < top)
        {
            // Cubic Hermite between the table's points, its slopes exact.
            const double position = excess / piezoDiodeStep;
            const auto point = static_cast<std::size_t>(position);
            const double t = position - static_cast<double>(point);
            const double t2 = t * t, t3 = t2 * t;
            dump = (2.0 * t3 - 3.0 * t2 + 1.0) * piezoDiodeDump_[point]
                + (t3 - 2.0 * t2 + t) * piezoDiodeStep * piezoDiodeSlope_[point]
                + (3.0 * t2 - 2.0 * t3) * piezoDiodeDump_[point + 1]
                + (t3 - t2) * piezoDiodeStep * piezoDiodeSlope_[point + 1];
        }
        else
        {
            // Past 8 V the diode voltage is a slowly growing logarithm:
            // z = x - N Vt ln(1 + Ceq fs z / IS), by two fixed-point steps.
            dump = excess;
            for (int step = 0; step < 3; ++step)
                dump = excess - D::diodeThermalVoltage * std::log1p(piezoDiodeScale_ * dump);
        }
        const double sign = bufferInput > 0.0 ? 1.0 : -1.0;
        bufferInput -= sign * dump;
        piezoFrontW_ -= sign * dump;
    }
    const double buffer = bufferInput > D::commonModeLimit ? D::commonModeLimit
        : (bufferInput < -D::commonModeLimit ? -D::commonModeLimit : bufferInput);
    piezoLastClamp_ = buffer - bufferInput;
    piezoLastOpen_ = open;
    // 4. C3 into U1B's bias network.
    piezoC3_ = piezoC3Pole_ * piezoC3_ + piezoC3Gain_ * (buffer + piezoLastBuffer_);
    piezoLastBuffer_ = buffer;
    const double coupled = buffer - piezoC3_;
    // 5. U1B: the drive its feedback would give, clipped at its output
    // swing, with the clip's corners band-limited by a 12-tap BLAMP
    // (PiezoBlampTable.h). C4's state lags the drive by the eight samples
    // the pipeline holds, in a 0.48 Hz loop: 5e-4 rad.
    constexpr double feedback = D::r7 / D::r8;
    const double stageDrive = (1.0 + feedback) * coupled - feedback * piezoC4_;
    // The corners are placed on the drive U1A's input would give if neither
    // its input range nor the diodes stopped it: they only act while U1B
    // is already past its swing, and their own corners, a sample or less
    // from U1B's at 3 kHz, would bend the cubic the corner is found on.
    const double freeDrive = (1.0 + feedback) * (freeInput - piezoC3_)
        - feedback * piezoC4_;
    auto& u = piezoDrive_;
    auto& stage = piezoStage_;
    u[0] = u[1];
    u[1] = u[2];
    u[2] = u[3];
    u[3] = freeDrive;
    // stage[m] is sample n - 7 + m: seven held back, this one, and four
    // ahead that only hold corrections so far.
    for (std::size_t m = 0; m + 1 < stage.size(); ++m)
        stage[m] = stage[m + 1];
    stage[stage.size() - 1] = 0.0;
    stage[7] += stageDrive > D::railHigh ? D::railHigh
        : (stageDrive < D::railLow ? D::railLow : stageDrive);
    for (const double rail : { D::railHigh, D::railLow })
    {
        const double before = u[1] - rail, after = u[2] - rail;
        if (!(before * after < 0.0))
            continue;
        // The corner lies between samples n - 2 and n - 1, on the cubic
        // through the four drives about u[1], relative to the rail.
        const double p0 = u[0] - rail, p3 = u[3] - rail;
        const double c1 = -p0 / 3.0 - before / 2.0 + after - p3 / 6.0;
        const double c2 = 0.5 * p0 - before + 0.5 * after;
        const double c3 = -p0 / 6.0 + before / 2.0 - after / 2.0 + p3 / 6.0;
        double d = before / (before - after);
        for (int step = 0; step < 2; ++step)
        {
            const double value = before + d * (c1 + d * (c2 + d * c3));
            const double slope = c1 + d * (2.0 * c2 + 3.0 * d * c3);
            if (slope != 0.0)
                d -= value / slope;
            d = d < 0.0 ? 0.0 : (d > 0.999999 ? 0.999999 : d);
        }
        const double slope = c1 + d * (2.0 * c2 + 3.0 * d * c3);
        const bool entering = rail > 0.0 ? after > 0.0 : after < 0.0;
        const double change = entering ? -slope : slope;
        // The residual at samples n - 7 .. n + 4, between two of the table's
        // 129 fractional positions.
        const double position = d * piezoBlampPhases;
        const auto row = static_cast<std::size_t>(position);
        const double share = position - static_cast<double>(row);
        const auto& lower = piezoBlampTable[row];
        const auto& upper = piezoBlampTable[row + 1];
        for (std::size_t m = 0; m < stage.size(); ++m)
            stage[m] += change * (lower[m] + share * (upper[m] - lower[m]));
    }
    // 6. The output seven samples back: C4's and C5's states follow it, and
    // the DI takes it through R9 and C5 off the pot.
    const double gainStage = stage[0];
    piezoC4_ = piezoC4Pole_ * piezoC4_ + piezoC4Gain_ * (gainStage + piezoLastStage_);
    piezoC5_ = piezoC5Pole_ * piezoC5_ + piezoC5Gain_ * (gainStage + piezoLastStage_);
    piezoLastStage_ = gainStage;
    constexpr double load = D::volume * D::diInput / (D::volume + D::diInput);
    piezoOutputVolts_ = load / (D::r9 + load) * (gainStage - piezoC5_);
    lastPiezoOpen_ = static_cast<float>(open);
    lastPiezoVoltage_ = static_cast<float>(piezoJackElement_ * open
                                           + piezoJackInput_ * bufferInput);
    lastPiezoInput_ = static_cast<float>(bufferInput);
    lastPiezoDrive_ = static_cast<float>(stageDrive);
    // Exact silence: once the input has stopped and every state is below
    // 10 nV (50 dB under the preamp's own 1.2 uV of noise) and the saddle's
    // histories below 50 nN, they are all zeroed together; zeroing one
    // while another still moves would itself be an input.
    if (input == 0.0f)
    {
        const float saddleFloor = piezoSaddleFloor_;
        constexpr double floor = 1.0e-8;
        bool quiet = exact::abs(y[0]) < saddleFloor && exact::abs(y[1]) < saddleFloor;
        for (float value : x)
            quiet = quiet && exact::abs(value) < saddleFloor;
        for (double value : { piezoFrontW_, piezoFrontC2_, piezoLastOpen_, piezoLastClamp_,
                              piezoC3_, piezoC4_, piezoC5_, piezoLastBuffer_,
                              piezoLastStage_, u[0], u[1], u[2], u[3] })
            quiet = quiet && exact::abs(value) < floor;
        for (double value : stage)
            quiet = quiet && exact::abs(value) < floor;
        if (quiet)
            resetPiezo();
    }
    // 7. The level: the match to the microphones, on the output reference
    // for the construction and Picking
    // (ConstructionLoudnessData.h). It is smoothed as one value, like the
    // microphones' reference, so a change glides along one curve and never
    // overshoots where the two factors move apart.
    piezoTrim_ += parameterSmoothing_
        * (PiezoDesign::trim * piezoReferenceFor(parameters_) - piezoTrim_);
    const auto result = static_cast<float>(
        static_cast<double>(piezoTrim_) * piezoOutputScale_ * piezoOutputVolts_);
    if (!exact::isfinite(result))
    {
        resetPiezo();
        return 0.0f;
    }
    return result;
}

void AcustraEngine::process(float* left, float* right, int numSamples) noexcept
{
    process(left, right, OutputBuses {}, numSamples);
}

void AcustraEngine::process(float* left, float* right, const OutputBuses& buses,
                            int numSamples) noexcept
{
    if (!prepared_ || left == nullptr || right == nullptr || numSamples <= 0)
        return;
    float* const piezo = buses.piezo;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (++controlCounter_ >= controlPeriod)
        {
            // External note-ons arrive before this sample's control update.
            // A held re-pluck due now must reset its attack in that same
            // order, including when its release lands on this boundary.
            for (int string = 0; string < stringCount; ++string)
            {
                auto& voice = voices_[static_cast<std::size_t>(string)];
                if (voice.repluckPending && voice.pluckDelay == 1)
                    firePluck(voice, string);
            }
            controlCounter_ = 0;
            updateControlState();
        }

        bodyAmount_ += parameterSmoothing_
            * (targetParameters_.bodyAmount - bodyAmount_);
        width_ += parameterSmoothing_
            * (targetParameters_.stereoWidth - width_);
        outputGain_ += parameterSmoothing_
            * (targetParameters_.outputGain - outputGain_);
        {
            // Settles onto its target exactly, so a mix returned to zero
            // leaves Main bit for bit as it was without one.
            // A glide that stops moving snaps too: at 192 kHz a step near 1
            // is below half an ulp before the 1e-4 test is reached.
            const float target = clamp(targetParameters_.piezoMix, 0.0f, 1.0f);
            const float next = piezoMix_ + parameterSmoothing_ * (target - piezoMix_);
            piezoMix_ = next == piezoMix_ || exact::abs(target - next) < 1.0e-4f
                ? target : next;
        }

        std::array<float, stringCount> verticalIncident {};
        std::array<float, stringCount> horizontalIncident {};
        std::array<float, stringCount> excitation {};
        std::array<float, stringCount> tailIncident {};
        std::array<float, stringCount> tailParallelIncident {};
        BridgeDrive drive {};
        // The same heave sums with each string's piezo sensitivity: the
        // incident force and the two impedance moments a weighted saddle
        // force needs (renderPiezo). Unit weights reproduce the junction's
        // own sums bit for bit.
        float piezoIncident = 0.0f;
        float piezoImpedance0 = 0.0f;
        float piezoImpedance1 = 0.0f;
        const float saddleHeight = saddleHeightRatio();
        for (int string = 0; string < stringCount; ++string)
        {
            auto& voice = voices_[static_cast<std::size_t>(string)];
            if (voice.pluckDelay > 0)
            {
                if (voice.pluckDelay == 1)
                    firePluck(voice, string);
                else
                    --voice.pluckDelay;
            }
            processPendingRelease(voice, string);
            float releaseGain = (voice.keyDown || voice.pedalHeld
                                 || voice.releaseJoinPending
                                 || voice.releaseAfterPluck || !voice.played)
                ? 1.0f : voice.releaseDamping;
            // A bend is a tension change, so the port this string presents
            // moves with it. The junction sums impedances every sample and a
            // whole-tone bend moves this one by 12%; followed at the delay's
            // own rate rather than stepped once a control period, the sum
            // never steps. A scale of exactly 1 leaves both arithmetic and
            // output bit-identical to the unbent engine.
            voice.appliedBendImpedanceScale += delaySmoothing_
                * (voice.bendImpedanceScale
                   - voice.appliedBendImpedanceScale);
            if (voice.bridgeTailStiffnessSamples > 0)
            {
                if (--voice.bridgeTailStiffnessSamples == 0)
                    voice.appliedBridgeTailStiffness = voice.bridgeTailStiffness;
                else
                    voice.appliedBridgeTailStiffness = voice.bridgeTailStiffnessStep > 0.0f
                        ? std::min(voice.bridgeTailStiffness,
                            voice.appliedBridgeTailStiffness + voice.bridgeTailStiffnessStep)
                        : std::max(voice.bridgeTailStiffness,
                            voice.appliedBridgeTailStiffness + voice.bridgeTailStiffnessStep);
            }
            excitation[static_cast<std::size_t>(string)]
                = renderExcitation(voice);
            verticalIncident[static_cast<std::size_t>(string)]
                = voice.loops[0].advance(delaySmoothing_, releaseGain);
            horizontalIncident[static_cast<std::size_t>(string)]
                = voice.loops[1].advance(delaySmoothing_, releaseGain);
            if (voice.contactTravelEnabled
                && (voice.contactTravel.active
                    || excitation[static_cast<std::size_t>(string)] != 0.0f))
            {
                const auto paths = voice.contactTravel.process(
                    excitation[static_cast<std::size_t>(string)]);
                // A fixed nut reverses displacement. Split one emitted
                // displacement-wave burst equally between both directions:
                // each gets 1/sqrt(2), preserving the sum of directional
                // wave-velocity energies of the old one-way source. Their
                // coherent sum at the bridge need not preserve mic RMS.
                constexpr float equalEnergySplit = 0.7071067811865475f;
                const float localContact = equalEnergySplit * (paths[0] - paths[1]);
                // Keep the held-note expression unchanged, including its
                // floating-point contraction; hand loss affects arrivals
                // only once the corresponding loop has begun damping.
                const float verticalContact = voice.loops[0].appliedReleaseGain == 1.0f
                    ? localContact : localContact * voice.loops[0].appliedReleaseGain;
                const float horizontalContact = voice.loops[1].appliedReleaseGain == 1.0f
                    ? localContact : localContact * voice.loops[1].appliedReleaseGain;
                verticalIncident[static_cast<std::size_t>(string)]
                    += 0.76f * verticalContact;
                horizontalIncident[static_cast<std::size_t>(string)]
                    += voice.excitationParallelGain * horizontalContact;
            }
            if (voice.legatoContactSamples > 0 || voice.legatoContactTravel.active)
            {
                float contact = 0.0f;
                if (voice.legatoContactSamples > 0)
                {
                    contact = voice.legatoContactAmplitude
                        * voice.legatoContactPulse[static_cast<std::size_t>(voice.legatoContactAge)];
                    if (++voice.legatoContactAge > voice.legatoContactSamples)
                        voice.legatoContactSamples = 0;
                }
                const auto paths = voice.legatoContactTravel.process(contact);
                constexpr float equalEnergySplit = 0.7071067811865475f;
                const float local = equalEnergySplit * (paths[0] - paths[1]);
                verticalIncident[static_cast<std::size_t>(string)]
                    += local * voice.loops[0].appliedReleaseGain;
            }
            // The contact's noise force, launched both ways from the contact
            // point along the stroke; the nut inverts what reaches it. Out of
            // line and behind the two fields it would read first: at zero
            // levels neither is ever set, and inlined it cost the voice loop
            // about a tenth of the engine's time.
            if (voice.contactNoiseSamples > 0 || voice.contactNoiseTravel.active)
                addContactNoise(voice,
                    verticalIncident[static_cast<std::size_t>(string)],
                    horizontalIncident[static_cast<std::size_t>(string)]);
            if (voice.releaseNoiseSamples > 0 || voice.releaseNoiseTravel.active)
                addReleaseNoise(voice,
                    verticalIncident[static_cast<std::size_t>(string)],
                    horizontalIncident[static_cast<std::size_t>(string)]);
            if (voice.repluckArrivals.remaining > 0)
            {
                const auto arrivals = voice.repluckArrivals.process();
                verticalIncident[static_cast<std::size_t>(string)]
                    += arrivals[0] * voice.loops[0].appliedReleaseGain;
                horizontalIncident[static_cast<std::size_t>(string)]
                    += arrivals[1] * voice.loops[1].appliedReleaseGain;
            }
            if (voice.tailActive)
            {
                tailIncident[static_cast<std::size_t>(string)]
                    = voice.tailLoop.advance(delaySmoothing_, voice.tailDamping);
                tailParallelIncident[static_cast<std::size_t>(string)]
                    = voice.tailParallelLoop.advance(delaySmoothing_,
                                                     voice.tailDamping);
                if (voice.tailRepluckArrivals.remaining > 0)
                {
                    const auto arrivals = voice.tailRepluckArrivals.process();
                    tailIncident[static_cast<std::size_t>(string)]
                        += arrivals[0] * voice.tailLoop.appliedReleaseGain;
                    tailParallelIncident[static_cast<std::size_t>(string)]
                        += arrivals[1] * voice.tailParallelLoop.appliedReleaseGain;
                }
                if (voice.tailLegatoContactSamples > 0 || voice.tailLegatoContactTravel.active)
                {
                    float contact = 0.0f;
                    if (voice.tailLegatoContactSamples > 0)
                    {
                        contact = voice.tailLegatoContactAmplitude
                            * voice.tailLegatoContactPulse[static_cast<std::size_t>(voice.tailLegatoContactAge)];
                        if (++voice.tailLegatoContactAge > voice.tailLegatoContactSamples)
                            voice.tailLegatoContactSamples = 0;
                    }
                    const auto paths = voice.tailLegatoContactTravel.process(contact);
                    constexpr float equalEnergySplit = 0.7071067811865475f;
                    tailIncident[static_cast<std::size_t>(string)]
                        += equalEnergySplit * (paths[0] - paths[1])
                            * voice.tailLoop.appliedReleaseGain;
                }
                if (voice.tailContactTravel.active)
                {
                    const auto paths = voice.tailContactTravel.process(0.0f);
                    constexpr float equalEnergySplit = 0.7071067811865475f;
                    const float localContact = equalEnergySplit * (paths[0] - paths[1]);
                    tailIncident[static_cast<std::size_t>(string)]
                        += 0.76f * localContact * voice.tailLoop.appliedReleaseGain;
                    tailParallelIncident[static_cast<std::size_t>(string)]
                        += voice.tailExcitationParallelGain * localContact
                            * voice.tailParallelLoop.appliedReleaseGain;
                }
                if (voice.tailContactNoiseTravel.active)
                    addTailContactNoise(voice,
                        tailIncident[static_cast<std::size_t>(string)],
                        tailParallelIncident[static_cast<std::size_t>(string)]);
            }
            // Every string is anchored behind the saddle whether or not it
            // is being played, so the anchor the junction sees is a constant
            // of the instrument. Summing only the played ones made it stiffen
            // with each voice held, which more than doubled a note's sustain
            // inside a chord. Each stub stands at its own string's point on
            // the saddle, so the six enter as the three moments of a
            // stiffness matrix rather than as one sum.
            const float arm = saddleLeverArm(string);
            drive.stiffness0 += voice.appliedBridgeTailStiffness;
            drive.stiffness1 += arm * voice.appliedBridgeTailStiffness;
            drive.stiffness2 += arm * arm * voice.appliedBridgeTailStiffness;
            if (saddleHeight != 0.0f)
                drive.stiffness2 += saddleHeight * saddleHeight
                                  * voice.appliedBridgeTailStiffness;
            // Every string on the bridge is a member of the junction, played
            // or not: an idle string on a moving bridge carries a wave, and
            // at its resonance it presents thousands of times its
            // characteristic impedance, which is what pins a real bridge
            // there and bounds the sympathetic energy. Driving idle strings
            // from the bridge while leaving them out of the sum let their
            // reaction grow without limit. The retained tail evolves in a
            // separate loop and receives its own full bridge return, so it
            // also supplies a port. Counting its incident wave without its
            // impedance breaks the wave-norm balance by Z_tail*x_string^2.
            // The six physical anchor stubs above are unchanged.
            if (voice.played || sympatheticStringsEnabled_)
            {
                const float port = voice.characteristicImpedance
                                 * voice.appliedBendImpedanceScale;
                const float tailPort = voice.tailActive
                    ? voice.tailCharacteristicImpedance : 0.0f;
                const float branchImpedance = port + tailPort;
                // Keep the existing summed-incident arithmetic when both
                // branches have the same impedance, as ordinary unbent
                // re-plucks do. A changed member bend needs separate weights.
                float incident = 2.0f * port
                    * (verticalIncident[static_cast<std::size_t>(string)]
                       + tailIncident[static_cast<std::size_t>(string)]);
                if (voice.tailActive && tailPort != port)
                    incident = 2.0f * port
                        * verticalIncident[static_cast<std::size_t>(string)]
                        + 2.0f * tailPort
                        * tailIncident[static_cast<std::size_t>(string)];
                drive.impedance0 += branchImpedance;
                drive.impedance1 += arm * branchImpedance;
                drive.impedance2 += arm * arm * branchImpedance;
                drive.incidentHeave += incident;
                drive.incidentRock += arm * incident;
                // The static force the hand held this string aside with,
                // let go at its pluck (initialisePluck), enters as an
                // external force on the saddle at this string's point:
                // 2Z h / (p D) times (n + 1) d^n, d the 10 ms pole, a step
                // high-passed at 16 Hz (0.97 of a true step at the 90 Hz air
                // mode) whose net impulse is zero, so it leaves the saddle
                // where it was. Its sample of release is the one the
                // junction's derivatives re-reference (firePluck), so it
                // starts without a click. The force is the one released at
                // that sample, at the port the string had then: a bend after
                // it moves the string, not the force its release already
                // took off the saddle. (n + 1) d^n is two one-pole stages at
                // d in cascade, so a re-pluck's step is one more impulse into
                // them and an earlier step still runs out to its zero net
                // impulse. The piezo's own sum below does not take it - its
                // preamp's headroom was set without it (PiezoDesign,
                // 2026-09-30) - and reads only the saddle's motion it causes.
                if (voice.releaseStepAge >= 0)
                {
                    float force = releaseStepPole_ * voice.releaseStepForce;
                    if (voice.releaseStepRise != 0.0f)
                    {
                        force += 2.0f * port * voice.releaseStepRise;
                        voice.releaseStepRise = 0.0f;
                    }
                    voice.releaseStepForce = force;
                    voice.releaseStepLevel
                        = releaseStepPole_ * voice.releaseStepLevel + force;
                    drive.incidentHeave += voice.releaseStepLevel;
                    drive.incidentRock += arm * voice.releaseStepLevel;
                    if (++voice.releaseStepAge >= releaseStepSamples_)
                    {
                        voice.releaseStepAge = -1;
                        voice.releaseStepForce = voice.releaseStepLevel = 0.0f;
                    }
                }
                const float piezoWeight
                    = piezoStringWeights_[static_cast<std::size_t>(string)];
                const float piezoBranch = piezoWeight * branchImpedance;
                piezoImpedance0 += piezoBranch;
                piezoImpedance1 += arm * piezoBranch;
                piezoIncident += piezoWeight * incident;
                // The parallel polarisation's port on the rocking coordinate
                // (see saddleHeightRatio): its incident force times h/a is a
                // moment, and it presents (h/a)^2 of its impedance there. The
                // retained tail's parallel plane is a second such port, at
                // the impedance the tail captured.
                if (saddleHeight != 0.0f)
                {
                    drive.impedance2 += saddleHeight * saddleHeight * port;
                    drive.incidentRock -= saddleHeight * 2.0f * port
                        * horizontalIncident[static_cast<std::size_t>(string)];
                    if (voice.tailActive)
                    {
                        drive.impedance2 += saddleHeight * saddleHeight
                                          * tailPort;
                        drive.incidentRock -= saddleHeight * 2.0f * tailPort
                            * tailParallelIncident[
                                static_cast<std::size_t>(string)];
                    }
                }
            }
        }

        // A played string sets the port. When the last one goes quiet the
        // strings are still on the bridge, and the measured body is still
        // ringing - it outlasts them - so the junction keeps the port it had
        // rather than switching it out. Dropping it instead stepped the
        // displacement every string reads: on a chord left to decay in silence
        // that arrived as a transient of 0.96 against a 0.008 background, and
        // it stored the modes' energy until the next note released it as a
        // click.
        if (drive.impedance0 > 1.0e-6f)
        {
            lastImpedanceSum_ = drive.impedance0;
            lastImpedanceMoment_ = drive.impedance1;
            lastImpedanceInertia_ = drive.impedance2;
            lastPiezoImpedanceSum_ = piezoImpedance0;
            lastPiezoImpedanceMoment_ = piezoImpedance1;
        }
        else if (lastImpedanceSum_ > 1.0e-6f)
        {
            drive.impedance0 = lastImpedanceSum_;
            drive.impedance1 = lastImpedanceMoment_;
            drive.impedance2 = lastImpedanceInertia_;
            piezoImpedance0 = lastPiezoImpedanceSum_;
            piezoImpedance1 = lastPiezoImpedanceMoment_;
        }
        const bool portIsLoaded = drive.impedance0 > 1.0e-6f;
        float bridgeDisplacement = 0.0f;
        float bridgeRotation = 0.0f;
        float reactionWave = portIsLoaded ? drive.incidentHeave : 0.0f;
        float reactionMoment = portIsLoaded ? drive.incidentRock : 0.0f;
        float bodyForceWave = reactionWave;
        float bodyMomentWave = reactionMoment;
        float tailForceWave = 0.0f;
        float tailMomentWave = 0.0f;
        if (bridgeCouplingEnabled_ && portIsLoaded)
        {
            if (bridgeLoadFade_ < 1.0f)
            {
                bridgeLoad_.process(drive, inverseSampleRate_,
                                    fadingBridgeLoad_, bridgeLoadFade_);
                bridgeLoadFade_ = std::min(1.0f,
                    bridgeLoadFade_ + bridgeLoadFadeStep_);
                if (bridgeLoadFade_ >= 1.0f && bridgeUpdatePending_)
                    applyPendingBridge(true);
            }
            else
                bridgeLoad_.process(drive, inverseSampleRate_);
            bridgeDisplacement = bridgeLoad_.displacement;
            bridgeRotation = bridgeLoad_.rotation;
            reactionWave = bridgeLoad_.mainIntegratedForce;
            reactionMoment = bridgeLoad_.mainIntegratedMoment;
            bodyForceWave = bridgeLoad_.bodyIntegratedForce;
            bodyMomentWave = bridgeLoad_.bodyIntegratedMoment;
            tailForceWave = bridgeLoad_.tailIntegratedForce;
            tailMomentWave = bridgeLoad_.tailIntegratedMoment;
        }
        // Each string's saddle force is its incident force less its port
        // moving with the saddle, F_i = inc_i - Z_i (x + u_i r); the piezo
        // reads their weighted sum, written as mainIntegratedForce is.
        float piezoWave = portIsLoaded ? piezoIncident : 0.0f;
        if (bridgeCouplingEnabled_ && portIsLoaded)
            piezoWave = piezoIncident - piezoImpedance0 * bridgeDisplacement
                - piezoImpedance1 * bridgeRotation;
        lastPiezoWave_ = piezoWave;
        const float sampleRateRatio = static_cast<float>(sampleRate_) / 48000.0f;
        if (bridgeDerivativesNeedPriming_
            && (exact::abs(reactionWave) + exact::abs(bridgeDisplacement)
                > 1.0e-12f))
        {
            bridgeVelocityDerivative_.reset(bridgeDisplacement);
            bridgeRotationDerivative_.reset(bridgeRotation);
            bridgeForceDerivative_.reset(reactionWave);
            piezoForceDerivative_.reset(piezoWave);
            bridgeForceMomentDerivative_.reset(reactionMoment);
            bridgeBodyForceDerivative_.reset(bodyForceWave);
            bridgeBodyMomentDerivative_.reset(bodyMomentWave);
            bridgeTailForceDerivative_.reset(tailForceWave);
            bridgeTailMomentDerivative_.reset(tailMomentWave);
            for (int string = 0; string < stringCount; ++string)
            {
                auto& voice = voices_[static_cast<std::size_t>(string)];
                voice.loops[0].bridgeDerivative.reset(
                    verticalIncident[static_cast<std::size_t>(string)]);
                voice.loops[1].bridgeDerivative.reset(
                    horizontalIncident[static_cast<std::size_t>(string)]);
            }
            bridgeDerivativesNeedPriming_ = false;
        }
        // A note that starts while the instrument is sounding puts a whole
        // released shape into the junction's wave variables in one sample.
        // Differencing that reads as a bridge velocity the size of the entire
        // displacement: an impulse about ten times the note it belongs to,
        // on every note-on after the first.
        const bool crossingRelease = bridgeDerivativesCrossRelease_;
        const bool crossingConfigure = bridgeDerivativesCrossConfigure_;
        bridgeDerivativesCrossRelease_ = false;
        bridgeDerivativesCrossConfigure_ = false;
        const auto motion = [&] (FixedDerivative& derivative, float wave)
        {
            return crossingRelease
                ? derivative.processAcrossRelease(wave, sampleRateRatio)
                : crossingConfigure
                ? derivative.processAcrossStep(wave, sampleRateRatio)
                : derivative.process(wave, sampleRateRatio);
        };
        lastBridgeVelocity_ = motion(
            bridgeVelocityDerivative_, bridgeDisplacement);
        // The saddle's rocking rate. A string reads its own end's motion as
        // the heave plus its lever arm times this; the derivative is linear,
        // so two of them serve all six.
        const float bridgeRotationRate = motion(
            bridgeRotationDerivative_, bridgeRotation);
        lastBridgeReactionForce_ = motion(bridgeForceDerivative_, reactionWave);
        lastPiezoForce_ = motion(piezoForceDerivative_, piezoWave);
        lastBridgeBodyForce_ = motion(bridgeBodyForceDerivative_, bodyForceWave);
        const float bodyMomentRate
            = motion(bridgeBodyMomentDerivative_, bodyMomentWave);
        // Everything from here to the voices is observation only - the tail
        // force, the total and tail moments and the port-power ledger feed
        // getters, never the output - so it runs only while observed.
        if (portObserversEnabled_)
        {
            lastBridgeTailForce_ = motion(bridgeTailForceDerivative_, tailForceWave);
            // Power crosses the saddle in both coordinates, so each branch's is
            // the heave product plus the rocking one; reading only the first
            // would let the tail spring look like it stored negative energy.
            motion(bridgeForceMomentDerivative_, reactionMoment);
            motion(bridgeTailMomentDerivative_, tailMomentWave);
            // Account for the complete zero-state trajectory of the passive load.
            // Audio priming suppresses the displacement shape's initial boundary
            // step; using that primed derivative in the work ledger drops its
            // positive input work but still counts the following elastic return.
            // The independent histories keep that initial work, without changing
            // the derivatives that drive radiation or the string pitch observer.
            const std::array<float, 8> portWaves {
                bridgeDisplacement, bridgeRotation, reactionWave, reactionMoment,
                bodyForceWave, bodyMomentWave, tailForceWave, tailMomentWave
            };
            std::array<float, 8> portRates {};
            for (std::size_t index = 0; index < portWaves.size(); ++index)
                portRates[index] = bridgePowerDerivatives_[index].process(
                    portWaves[index], sampleRateRatio);
            lastBridgePower_ = portRates[0] * portRates[2]
                             + portRates[1] * portRates[3];
            lastBridgeBodyPower_ = portRates[0] * portRates[4]
                                 + portRates[1] * portRates[5];
            lastBridgeTailPower_ = portRates[0] * portRates[6]
                                 + portRates[1] * portRates[7];
        }

        float directLeft = 0.0f;
        float directRight = 0.0f;
        float longitudinalForce = 0.0f;
        for (int string = 0; string < stringCount; ++string)
        {
            const float arm = saddleLeverArm(string);
            finishVoice(voices_[static_cast<std::size_t>(string)], string,
                verticalIncident[static_cast<std::size_t>(string)],
                horizontalIncident[static_cast<std::size_t>(string)],
                excitation[static_cast<std::size_t>(string)],
                tailIncident[static_cast<std::size_t>(string)],
                tailParallelIncident[static_cast<std::size_t>(string)],
                bridgeDisplacement + arm * bridgeRotation,
                lastBridgeVelocity_ + arm * bridgeRotationRate,
                -saddleHeight * bridgeRotation,
                directLeft, directRight, longitudinalForce);
        }

        // The load entering the body compliance drives measured radiation.
        // Idle-string reactions already enter that load through the shared
        // junction. Axial radiation is still an additional one-way force
        // surrogate. The microphone bank itself does not feed back into the
        // junction.
        lastLongitudinalForce_ = longitudinalForce;
        // Same rigid-saddle basis as the paired measurement: F=Fb+Ft and
        // normalized moment T=M/a=Ft-Fb. Both inputs retain their measured
        // complex microphone phase. No extra stereo delay or gain is added.
        // The + 0.0f is where a separate sympathetic force, always zero, was
        // summed: it turns a -0.0 bridge force into +0.0 before the axial
        // force is added, so the body hears the same bits as it did.
        const BodyOutput body = renderBody(lastBridgeBodyForce_
            + 0.0f + lastLongitudinalForce_, bodyMomentRate);

        // Strings themselves radiate poorly. Keep the small bridge-local path
        // separate from the measurement-derived, author-transformed soundboard
        // response, and apply
        // Width to both paths so zero is genuinely mono.
        const float directMono = 0.5f * (directLeft + directRight);
        const float spreadDirectLeft = directMono
            + width_ * (directLeft - directMono);
        const float spreadDirectRight = directMono
            + width_ * (directRight - directMono);
        const float bodyScale = 0.68f + 0.72f * bodyAmount_;
        const float directScale = 0.10f + 0.10f * (1.0f - bodyAmount_);
        const float monoBody = 0.5f * (body.left + body.right);
        const float spreadLeft = monoBody + width_ * (body.left - monoBody);
        const float spreadRight = monoBody + width_ * (body.right - monoBody);
        // A material, construction or Picking change reaches the reference
        // over the same smoothing as the output control, so it never steps a
        // ringing instrument; the mono microphone's own reference glides
        // beside it.
        const float outputReference = outputReferenceFor(parameters_);
        outputReference_ += parameterSmoothing_
            * (outputReference - outputReference_);
        monoReference_ += parameterSmoothing_
            * (monoReferenceFor(parameters_) - monoReference_);
        const float reference = radiationReferenceGain * outputReference_;
        float outputLeft = reference * outputGain_
            * (bodyScale * spreadLeft + directScale * spreadDirectLeft);
        float outputRight = reference * outputGain_
            * (bodyScale * spreadRight + directScale * spreadDirectRight);

        // Capture is an observation: every route shares the unchanged
        // vibrating instrument, so switching sensors never resets a note.
        // Keep the default stereo path bit-for-bit, including its width law.
        // Advance the whole piezo chain even while unheard, so selecting it
        // crossfades to the voltage of the already-ringing instrument. The
        // axial force presses on the saddle through the strings' break angle
        // (zero while longitudinalGain ships at 0).
        const float loadedPiezo = renderPiezo(
            lastPiezoForce_ + PiezoDesign::axialShare * lastLongitudinalForce_);
        const float idlePeak = std::max({ exact::abs(body.left),
            exact::abs(body.right), exact::abs(body.upper),
            exact::abs(directLeft), exact::abs(directRight),
            exact::abs(loadedPiezo) });
        if (piezo != nullptr)
        {
            // The separate Piezo output observes before Capture chooses, at
            // the level the mono route below gives it and through its own
            // copy of the safety limiter; nothing reads it back, so Main is
            // untouched by wanting it. Written as the mono route's product:
            // with only the piezo selected that route adds exact zeros to
            // this, so the two agree.
            const float piezoOut = safetyLimit(
                radiationReferenceGain * outputGain_ * loadedPiezo);
            piezo[sample] = exact::isfinite(piezoOut) ? piezoOut : 0.0f;
        }
        // The microphones wait out the piezo's pipeline (renderPiezo), so a
        // Capture crossfade and Piezo Mix sum the two sensors as the
        // instrument moved them, at every sample rate. Main is seven samples
        // later than the strings for every Capture.
        float monoMic = radiationReferenceGain * monoReference_ * outputGain_
            * (bodyScale * body.upper + directScale * directMono);
        // The room around the microphones (RoomAmbience): fed the pair's mid
        // signal through the send, its field spread by Width as the pair is,
        // and heard by the mono microphone at that one's own reference. It
        // never reaches the piezo. Zero sends nothing and, once the room has
        // rung out, costs nothing and changes no bit.
        float roomPeak = 0.0f;
        {
            const float target = clamp(targetParameters_.room, 0.0f, 1.0f);
            const float next = roomAmount_ + parameterSmoothing_ * (target - roomAmount_);
            roomAmount_ = next == roomAmount_ || exact::abs(target - next) < 1.0e-4f
                ? target : next;
            if (roomAmount_ != 0.0f || room_.active)
            {
                if (roomAmount_ != roomSendFor_)
                {
                    // The send: 10 dB lower at each halving of Room.
                    roomSendFor_ = roomAmount_;
                    roomSend_ = roomAmount_ > 0.0f
                        ? 0.79432823f * std::pow(roomAmount_, 1.66f) : 0.0f;
                }
                float wetLeft = 0.0f;
                float wetRight = 0.0f;
                room_.process(roomSend_ * 0.5f * (outputLeft + outputRight),
                              wetLeft, wetRight);
                const float wetMid = 0.5f * (wetLeft + wetRight);
                const float wetSide = 0.5f * width_ * (wetLeft - wetRight);
                outputLeft += wetMid + wetSide;
                outputRight += wetMid - wetSide;
                monoMic += 1.41421356f * wetMid * monoReference_
                    / std::max(outputReference_, 1.0e-6f);
                roomPeak = std::max(exact::abs(wetLeft), exact::abs(wetRight));
            }
        }
        {
            const auto index = static_cast<std::size_t>(micDelayIndex_);
            const float heldLeft = micDelayLeft_[index];
            const float heldRight = micDelayRight_[index];
            const float heldMono = micDelayMono_[index];
            micDelayLeft_[index] = outputLeft;
            micDelayRight_[index] = outputRight;
            micDelayMono_[index] = monoMic;
            micDelayIndex_ = micDelayIndex_ + 1 == piezoPipelineSamples
                ? 0 : micDelayIndex_ + 1;
            outputLeft = heldLeft;
            outputRight = heldRight;
            if (parameters_.capture != CaptureType::StereoMic
                || captureMix_[0] != 1.0f)
            {
                for (std::size_t slot = 0; slot < captureMix_.size(); ++slot)
                {
                    const float target = slot == static_cast<std::size_t>(
                        parameters_.capture) ? 1.0f : 0.0f;
                    float& mix = captureMix_[slot];
                    const float next = mix + parameterSmoothing_ * (target - mix);
                    mix = next == mix || exact::abs(target - next) < 1.0e-4f
                        ? target : next;
                }
                // One physical microphone avoids the spaced pair's phase
                // cancellation; its copies and the piezo ignore width.
                const float mono = captureMix_[7] * heldMono
                    + radiationReferenceGain * outputGain_
                    * (captureMix_[6] * loadedPiezo);
                outputLeft = captureMix_[0] * outputLeft + mono;
                outputRight = captureMix_[0] * outputRight + mono;
            }
        }
        // Piezo Mix: the piezo, at the level Capture Piezo gives it, under
        // whichever microphones Capture selects, on both sides. It fades out
        // as Capture moves onto the piezo itself, which is then all of Main.
        if (piezoMix_ != 0.0f)
        {
            const float blended = piezoMix_ * (1.0f - captureMix_[6])
                * radiationReferenceGain * outputGain_ * loadedPiezo;
            outputLeft += blended;
            outputRight += blended;
        }

        // Preserve ordinary notes exactly; only the final 1 dB of headroom is
        // compressed for pathological automation and dense repicks.
        outputLeft = safetyLimit(outputLeft);
        outputRight = safetyLimit(outputRight);
        left[sample] = exact::isfinite(outputLeft) ? outputLeft : 0.0f;
        right[sample] = exact::isfinite(outputRight) ? outputRight : 0.0f;
        processIdleFlush(std::max(idlePeak, roomPeak));
        // Scheduled releases run inside this loop. Their repeat history must
        // use the same absolute sample as a note-on issued between blocks.
        ++sampleClock_;
    }
}

// The room (EngineParameters::room): a small studio, 5.2 x 4.1 x 2.7 m, the
// guitar 0.95 m over the floor with the microphones 0.32 m in front of it.
// The early reflections are its image sources within two bounces, the ten
// strongest, at their delays after the direct sound and their spherical
// spreading times the surfaces' pressure reflection (wood floor 0.88, treated
// ceiling 0.62, walls 0.75-0.80), panned by the arrival's side across the
// pair (softened to 0.7: neither microphone is a point). The late field is
// an eight-line feedback delay network (Jot and Chaigne, AES 90 (1991)
// preprint 3030) with a Hadamard mix, its lines 17-41 ms, each damped by a
// one-pole so the field decays in 0.45 s up to 1 kHz, 0.36 s at 4 kHz and
// 0.26 s at 8 kHz, as a treated room's does; it starts 8 ms after the direct
// sound, through four allpasses that make its echoes dense within 20 ms, as
// a small room's mixing time does. What goes in is filtered by a 9 kHz
// one-pole and the reflections by a 6.5 kHz one; the room carries no detail
// above them.
namespace
{
struct RoomTap
{
    float milliseconds;
    float left;
    float right;
};
constexpr std::size_t roomTapTotal = 10;
constexpr std::size_t roomLineTotal = 8;
constexpr std::array<RoomTap, roomTapTotal> roomTaps { {
    { 4.97f, 0.0973f, 0.0973f },  // floor
    { 8.70f, 0.0542f, 0.0542f },  // wall behind the player
    { 9.03f, 0.0406f, 0.0406f },  // ceiling
    { 10.33f, 0.0408f, 0.0408f }, // floor, then the wall behind
    { 12.52f, 0.0510f, 0.0207f }, // left wall
    { 13.37f, 0.0342f, 0.0342f }, // wall behind the microphones
    { 13.73f, 0.0395f, 0.0198f }, // floor, then the left wall
    { 14.51f, 0.0279f, 0.0279f }, // floor, then the wall behind them
    { 16.01f, 0.0165f, 0.0405f }, // right wall
    { 16.99f, 0.0154f, 0.0335f }, // floor, then the right wall
} };
constexpr std::array<float, roomLineTotal> roomLineMilliseconds {
    17.3f, 19.7f, 23.1f, 26.3f, 29.9f, 33.7f, 37.1f, 41.3f
};
// The late field's input passes four allpasses (Schroeder, JAES 10 (1962)
// 219-223) at 0.65, so its echoes are dense from their start.
constexpr std::array<float, 4> roomDiffuserMilliseconds { 5.3f, 3.7f, 2.3f, 1.3f };
constexpr float roomDiffusion = 0.65f;
constexpr float roomLowSeconds = 0.45f;
constexpr float roomNyquistSeconds = 0.15f;
constexpr float roomLateMilliseconds = 8.0f;
// The late field's energy over the reflections' at the microphones.
constexpr float roomLateShare = 1.5f;
constexpr double roomMaximumRate = 64000.0;

bool roomPrime(int value) noexcept
{
    if (value < 2)
        return false;
    for (int divisor = 2; divisor * divisor <= value; ++divisor)
        if (value % divisor == 0)
            return false;
    return true;
}

// The eight lines' Hadamard mix, in place, normalised to stay lossless.
void roomHadamard(std::array<float, roomLineTotal>& values) noexcept
{
    for (std::size_t span = 1; span < values.size(); span *= 2)
        for (std::size_t start = 0; start < values.size(); start += 2 * span)
            for (std::size_t index = start; index < start + span; ++index)
            {
                const float a = values[index];
                const float b = values[index + span];
                values[index] = a + b;
                values[index + span] = a - b;
            }
    for (auto& value : values)
        value *= 0.35355339f;
}
} // namespace

void AcustraEngine::RoomAmbience::prepare(double hostRate) noexcept
{
    static_assert(roomTapTotal == static_cast<std::size_t>(tapCount)
                  && roomLineTotal == static_cast<std::size_t>(lineCount));
    decimation = std::max(1, static_cast<int>(std::ceil(hostRate / roomMaximumRate)));
    inverseDecimation = 1.0f / static_cast<float>(decimation);
    const double rate = hostRate / decimation;
    const auto samples = [rate] (float milliseconds)
    {
        return static_cast<int>(std::lround(static_cast<double>(milliseconds)
                                            * 1.0e-3 * rate));
    };
    for (std::size_t tap = 0; tap < roomTaps.size(); ++tap)
    {
        tapDelays[tap] = std::clamp(samples(roomTaps[tap].milliseconds), 1,
                                    earlyCapacity - 1);
        tapLeft[tap] = roomTaps[tap].left;
        tapRight[tap] = roomTaps[tap].right;
    }
    lateDelay = std::clamp(samples(roomLateMilliseconds), 1, earlyCapacity - 1);
    longest = lateDelay;
    for (std::size_t diffuser = 0; diffuser < diffuserLengths.size(); ++diffuser)
    {
        int length = std::clamp(samples(roomDiffuserMilliseconds[diffuser]), 2,
                                diffuserCapacity - 1);
        while (!roomPrime(length) && length < diffuserCapacity - 1)
            ++length;
        diffuserLengths[diffuser] = length;
        longest += length;
    }
    for (std::size_t line = 0; line < lengths.size(); ++line)
    {
        // Each line a prime number of samples, so no two share a period.
        int length = std::clamp(samples(roomLineMilliseconds[line]), 2,
                                lineCapacity - 1);
        while (!roomPrime(length) && length < lineCapacity - 1)
            ++length;
        lengths[line] = length;
        longest = std::max(longest, length + lateDelay + diffuserLengths[0]
                           + diffuserLengths[1] + diffuserLengths[2] + diffuserLengths[3]);
        const double perLow = std::pow(10.0, -3.0 * length / (roomLowSeconds * rate));
        const double perHigh = std::pow(10.0, -3.0 * length / (roomNyquistSeconds * rate));
        const double pole = (perLow - perHigh) / (perLow + perHigh);
        absorptionPole[line] = static_cast<float>(pole);
        absorptionGain[line] = static_cast<float>(perLow * (1.0 - pole));
    }
    inputCoefficient = static_cast<float>(-std::expm1(-2.0 * piDouble * 9000.0 / hostRate));
    earlyCoefficient = static_cast<float>(-std::expm1(-2.0 * piDouble
                                                      * std::min(6500.0, 0.4 * rate) / rate));
    // Normalise: the reflections and the late field at their share, then
    // the whole room to unit energy gain for a signal weighted as a guitar's
    // is, its power falling above 1 kHz (a one-pole's): the room keeps its
    // longest decay where that power is, so a white signal's gain would read
    // the room 6 dB quieter than a guitar hears it. The gain is a ratio of
    // energies at the room's own rate, which a host-rate signal keeps too.
    outputScale = 1.0f;
    const int length = static_cast<int>(1.4 * rate);
    const double weighting = -std::expm1(-2.0 * piDouble * 1000.0 / rate);
    double weightedEnergy = 0.0;
    {
        double value = weighting;
        for (int index = 0; index < length; ++index)
        {
            weightedEnergy += value * value;
            value *= 1.0 - weighting;
        }
    }
    double earlyEnergy = 0.0, lateEnergy = 0.0;
    for (int pass = 0; pass < 2; ++pass)
    {
        reset();
        double energy = 0.0;
        double drive = weighting;
        // Pass 0 measures the reflections alone (no late input), pass 1 the
        // late field alone.
        const auto savedLeft = tapLeft;
        const auto savedRight = tapRight;
        if (pass == 1)
        {
            tapLeft.fill(0.0f);
            tapRight.fill(0.0f);
        }
        lateInput = pass == 1 ? 1.0f : 0.0f;
        for (int index = 0; index < length; ++index)
        {
            step(static_cast<float>(drive));
            drive *= 1.0 - weighting;
            if (drive < 1.0e-12)
                drive = 0.0;
            energy += 0.5 * (static_cast<double>(currentLeft) * currentLeft
                             + static_cast<double>(currentRight) * currentRight);
        }
        tapLeft = savedLeft;
        tapRight = savedRight;
        (pass == 0 ? earlyEnergy : lateEnergy) = energy;
    }
    lateInput = static_cast<float>(std::sqrt(roomLateShare * earlyEnergy
                                             / std::max(lateEnergy, 1.0e-30)));
    outputScale = static_cast<float>(std::sqrt(weightedEnergy / std::max(
        earlyEnergy * (1.0 + roomLateShare), 1.0e-30)));
    reset();
}

void AcustraEngine::RoomAmbience::reset() noexcept
{
    for (auto& line : lines)
        line.fill(0.0f);
    heads.fill(0);
    absorptionState.fill(0.0f);
    early.fill(0.0f);
    earlyHead = 0;
    for (auto& diffuser : diffusers)
        diffuser.fill(0.0f);
    diffuserHeads.fill(0);
    inputState = 0.0f;
    earlyLeft = earlyRight = 0.0f;
    phase = 0;
    accumulator = 0.0f;
    previousLeft = previousRight = 0.0f;
    currentLeft = currentRight = 0.0f;
    quietSamples = 0;
    active = false;
}

void AcustraEngine::RoomAmbience::step(float input) noexcept
{
    early[static_cast<std::size_t>(earlyHead)] = input;
    const auto delayed = [this] (int delay)
    {
        int index = earlyHead - delay;
        if (index < 0)
            index += earlyCapacity;
        return early[static_cast<std::size_t>(index)];
    };
    float reflectedLeft = 0.0f;
    float reflectedRight = 0.0f;
    for (std::size_t tap = 0; tap < tapDelays.size(); ++tap)
    {
        const float value = delayed(tapDelays[tap]);
        reflectedLeft += tapLeft[tap] * value;
        reflectedRight += tapRight[tap] * value;
    }
    float late = lateInput * delayed(lateDelay);
    earlyHead = earlyHead + 1 == earlyCapacity ? 0 : earlyHead + 1;
    earlyLeft += earlyCoefficient * (reflectedLeft - earlyLeft);
    earlyRight += earlyCoefficient * (reflectedRight - earlyRight);

    for (std::size_t diffuser = 0; diffuser < diffusers.size(); ++diffuser)
    {
        auto& line = diffusers[diffuser];
        int& head = diffuserHeads[diffuser];
        const float stored = line[static_cast<std::size_t>(head)];
        float written = late + roomDiffusion * stored;
        if (exact::abs(written) < 1.0e-20f)
            written = 0.0f;
        line[static_cast<std::size_t>(head)] = written;
        head = head + 1 == diffuserLengths[diffuser] ? 0 : head + 1;
        late = stored - roomDiffusion * written;
    }
    std::array<float, roomLineTotal> outputs {};
    float lateLeft = 0.0f;
    float lateRight = 0.0f;
    bool silent = input == 0.0f && late == 0.0f;
    for (std::size_t line = 0; line < outputs.size(); ++line)
    {
        const float stored = lines[line][static_cast<std::size_t>(heads[line])];
        silent = silent && stored == 0.0f;
        float& state = absorptionState[line];
        state = absorptionGain[line] * stored + absorptionPole[line] * state;
        if (exact::abs(state) < 1.0e-20f)
            state = 0.0f;
        outputs[line] = state;
        // Two orthogonal sign patterns, so left and right decorrelate.
        lateLeft += line < 4 ? state : -state;
        lateRight += (line & 1u) == 0u ? state : -state;
    }
    roomHadamard(outputs);
    for (std::size_t line = 0; line < outputs.size(); ++line)
    {
        // The input enters every line with its own sign (a third pattern).
        const float sign = ((line >> 1u) & 1u) == 0u ? 1.0f : -1.0f;
        float written = outputs[line] + sign * 0.35355339f * late;
        if (exact::abs(written) < 1.0e-20f)
            written = 0.0f;
        lines[line][static_cast<std::size_t>(heads[line])] = written;
        heads[line] = heads[line] + 1 == lengths[line] ? 0 : heads[line] + 1;
    }
    if (exact::abs(earlyLeft) < 1.0e-20f)
        earlyLeft = 0.0f;
    if (exact::abs(earlyRight) < 1.0e-20f)
        earlyRight = 0.0f;
    currentLeft = outputScale * (earlyLeft + 0.35355339f * lateLeft);
    currentRight = outputScale * (earlyRight + 0.35355339f * lateRight);
    silent = silent && earlyLeft == 0.0f && earlyRight == 0.0f;
    quietSamples = silent ? quietSamples + 1 : 0;
    // Past the longest path with nothing in or out, every state is zero.
    if (quietSamples > longest + earlyCapacity)
        active = false;
}

void AcustraEngine::RoomAmbience::process(float input, float& left,
                                          float& right) noexcept
{
    // A room that has rung out holds exact zeros everywhere, so silence in
    // is silence out without running it.
    if (!active && input == 0.0f)
    {
        left = right = 0.0f;
        return;
    }
    active = true;
    inputState += inputCoefficient * (input - inputState);
    if (exact::abs(inputState) < 1.0e-20f)
        inputState = 0.0f;
    accumulator += inputState;
    if (++phase >= decimation)
    {
        phase = 0;
        previousLeft = currentLeft;
        previousRight = currentRight;
        step(accumulator * inverseDecimation);
        accumulator = 0.0f;
    }
    // Across a decimated step the output moves linearly to the new sample.
    const float share = static_cast<float>(phase + 1) * inverseDecimation;
    left = previousLeft + share * (currentLeft - previousLeft);
    right = previousRight + share * (currentRight - previousRight);
}

void AcustraEngine::processIdleFlush(float samplePeak) noexcept
{
    // The residue is a string-bridge equilibrium kept up by rounding: each
    // loop holds a quasi-DC value near 1e-13 and the bridge a matching
    // static displacement, so flushing any one state alone does not hold.
    // Every instrument signal before the output gain (body, direct path and
    // piezo) under 1e-11, about 190 dB under a played note, for 80 ms with
    // no string played, held, sounding a tail or about to be plucked, is
    // that residue: clear the whole instrument once, as All Sound Off does
    // when nothing is playing, and again only after it has sounded.
    constexpr float idleFloor = 1.0e-11f;
    constexpr float idleFlushSeconds = 0.08f;
    // Counted sample by sample, so the flush lands on the same sample
    // whatever blocks the host or the player split the audio into.
    if (!(samplePeak < idleFloor))
    {
        idleQuietSamples_ = 0;
        idleFlushed_ = false;
        return;
    }
    if (idleFlushed_)
        return;
    bool idle = true;
    for (const auto& voice : voices_)
        idle = idle && !voice.played && !voice.keyDown && !voice.pedalHeld
            && !voice.tailActive && voice.pluckDelay == 0
            && !voice.repluckPending && !voice.contactTravel.active
            && !voice.contactNoiseTravel.active && voice.contactNoiseSamples == 0
            && !voice.tailContactTravel.active
            && !voice.tailContactNoiseTravel.active
            && voice.repluckArrivals.remaining == 0
            && voice.tailRepluckArrivals.remaining == 0
            && !voice.legatoContactTravel.active && voice.legatoContactSamples == 0
            && !voice.tailLegatoContactTravel.active && voice.tailLegatoContactSamples == 0;
    if (!idle)
    {
        idleQuietSamples_ = 0;
        idleFlushed_ = false;
        return;
    }
    ++idleQuietSamples_;
    if (static_cast<float>(idleQuietSamples_)
        < idleFlushSeconds * static_cast<float>(sampleRate_))
        return;
    for (int string = 0; string < stringCount; ++string)
        returnToOpenString(voices_[static_cast<std::size_t>(string)], string, true);
    resetSoundState();
    idleFlushed_ = true;
}

int AcustraEngine::getActiveVoiceCount() const noexcept
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(),
        [] (const Voice& voice) { return voice.played; }));
}

int AcustraEngine::getSympatheticStringCount() const noexcept
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(),
        [] (const Voice& voice)
        {
            return !voice.played && voice.level > 2.0e-7f;
        }));
}

std::array<AcustraEngine::StringActivity, AcustraEngine::stringCount>
AcustraEngine::getStringActivity() const noexcept
{
    std::array<StringActivity, stringCount> activity {};
    for (std::size_t string = 0; string < activity.size(); ++string)
    {
        const auto& voice = voices_[string];
        auto& current = activity[string];
        current.openMidi = voice.openMidi;
        current.midiNote = voice.midiNote;
        current.fret = voice.fret;
        current.harmonic = voice.harmonic;
        current.keyDown = voice.keyDown;
        current.played = voice.played;
        current.pedalHeld = voice.pedalHeld;
        current.level = std::max(voice.level,
            voice.tailActive ? voice.tailLevel : 0.0f);
    }
    return activity;
}

float AcustraEngine::getLastBridgeVelocity() const noexcept
{
    return lastBridgeVelocity_;
}

float AcustraEngine::getLastBridgeReactionForce() const noexcept
{
    return lastBridgeReactionForce_;
}

float AcustraEngine::getLastBridgeBodyForce() const noexcept
{
    return lastBridgeBodyForce_;
}

float AcustraEngine::getLastBridgeTailForce() const noexcept
{
    return lastBridgeTailForce_;
}

float AcustraEngine::getLastLongitudinalForce() const noexcept
{
    return lastLongitudinalForce_;
}

float AcustraEngine::getLastPiezoVoltage() const noexcept
{
    return lastPiezoVoltage_;
}

double AcustraEngine::getPiezoNewtonsPerUnit() const noexcept
{
    return piezoNewtonsPerUnit_;
}

AcustraEngine::PiezoProbe AcustraEngine::getLastPiezoProbe() const noexcept
{
    return { lastPiezoOpen_, lastPiezoVoltage_, lastPiezoInput_, lastPiezoDrive_ };
}

float AcustraEngine::getLastBridgePower() const noexcept
{
    return lastBridgePower_;
}

float AcustraEngine::getLastBridgeBodyPower() const noexcept
{
    return lastBridgeBodyPower_;
}

float AcustraEngine::getLastBridgeTailPower() const noexcept
{
    return lastBridgeTailPower_;
}

} // namespace acustra
