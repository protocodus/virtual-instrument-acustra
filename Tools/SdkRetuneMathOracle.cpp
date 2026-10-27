// Actual-SDK oracle: reference operators and candidate helpers run from SDK
// modules, avoiding the host compiler's complex ABI and libm implementation.
// Build: clang++ -std=c++17 -O2 -ffp-contract=off SdkRetuneMathOracle.cpp -o oracle
// Usage: oracle BASELINE EXPORT_OFF DIVIDE_OFF HYPOT_OFF SQRT_OFF
//               CANDIDATE EXPORT_OFF PROBE_OFF RANDOM_CASES
//               [--predicate] [--corrupt] [--corrupt-predicate]
// Offsets are verified nm addresses (hex); the diagnostic probe's source and
// relink arguments are retained beside the evidence described in the audit.
#include <cfenv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <random>
#include <vector>

namespace {
// The phdsp64 ABI returns complex<double> through a hidden result pointer.
// A host struct larger than 16 bytes uses the same convention on arm64.
struct ComplexResult { double real, imaginary; char padding[16]; };
using Divide = ComplexResult (*)(const std::complex<double>&, const std::complex<double>&);
using Sqrt = ComplexResult (*)(const std::complex<double>&);
using Hypot = double (*)(double, double);
using Probe = void (*)(const double*, double*);

std::uint64_t bits(double value)
{
    std::uint64_t word;
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

double fromBits(std::uint64_t word)
{
    double value;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

char* loadModule(const char* path, const char* exportOffset)
{
    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) { std::puts(dlerror()); std::exit(2); }
    auto* exported = static_cast<char*>(dlsym(handle, "JukeboxExport_RenderRealtime"));
    if (!exported) { std::puts("Missing SDK export"); std::exit(2); }
    return exported - std::strtoull(exportOffset, nullptr, 16);
}

template <class Function>
Function functionAt(char* base, const char* offset)
{
    void* address = base + std::strtoull(offset, nullptr, 16);
    Function function;
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

double randomDouble(std::mt19937_64& rng)
{
    switch (rng() % 4)
    {
        case 0: return fromBits(rng());
        case 1: return std::ldexp(double(std::int64_t(rng() >> 11)) * 0x1p-51 - 2., int(rng() % 80) - 40);
        case 2: return std::ldexp(double(std::int64_t(rng() >> 11)) * 0x1p-51 - 2., int(rng() % 2100) - 1050);
        default: return double(std::int64_t(rng())) * 0x1p-40;
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 10)
    {
        std::puts("BASELINE EXPORT DIVIDE HYPOT SQRT CANDIDATE EXPORT PROBE RANDOM_CASES [--predicate] [--corrupt]");
        return 2;
    }
    char* baseline = loadModule(argv[1], argv[2]);
    const auto divide = functionAt<Divide>(baseline, argv[3]);
    const auto hypot = functionAt<Hypot>(baseline, argv[4]);
    const auto sqrt = functionAt<Sqrt>(baseline, argv[5]);
    const auto probe = functionAt<Probe>(loadModule(argv[6], argv[7]), argv[8]);
    const long randomCases = std::atol(argv[9]);
    bool predicates = false, corrupt = false, corruptPredicate = false;
    for (int i = 10; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--predicate") == 0) predicates = true;
        else if (std::strcmp(argv[i], "--corrupt") == 0) corrupt = true;
        else if (std::strcmp(argv[i], "--corrupt-predicate") == 0) corruptPredicate = true;
        else { std::puts("Unknown option"); return 2; }
    }
    if (corruptPredicate && !predicates) return 2;

    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> edges { 0., -0., 1., -1., 2., -2., 0.5, -0.5,
        1e-9, -1e-9, 1e-10, -1e-10,
        std::numeric_limits<double>::min(), -std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
        infinity, -infinity, fromBits(0x7ff8000000001234ull), fromBits(0xfff8000000005678ull) };
    for (double threshold : { 1e-9, 1e-10, std::numeric_limits<double>::min(), 1., 2. })
        for (double direction : { 0., infinity, -infinity })
        {
            edges.push_back(std::nextafter(threshold, direction));
            edges.push_back(-std::nextafter(threshold, direction));
        }
    for (int exponent = -1074; exponent < 1024; exponent += 7)
    {
        edges.push_back(std::ldexp(1., exponent));
        edges.push_back(-std::ldexp(1., exponent));
    }

    long totalMismatches = 0;
    const int initialMode = std::fegetround();
    for (int mode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO })
    {
        std::fesetround(mode);
        std::mt19937_64 rng(20261023);
        long tested = 0, mismatches[3] {}, modeMismatches = 0, predicateMismatches = 0;
        const auto check = [&] (double a, double b, double c, double d)
        {
            const std::complex<double> z(a, b), w(c, d);
            const double input[] { a, b, c, d };
            double actual[8] {};
            probe(input, actual);
            if (tested == 0 && corrupt) actual[0] = fromBits(bits(actual[0]) ^ 1);
            if (tested == 0 && corruptPredicate) actual[6] = 1. - actual[6];
            const auto quotient = divide(z, w), root = sqrt(z);
            const double magnitude = hypot(a, b);
            const double expected[] { quotient.real, quotient.imaginary, magnitude, root.real, root.imaginary };
            for (int item = 0; item < 5; ++item)
                if (bits(expected[item]) != bits(actual[item]))
                {
                    const int category = item < 2 ? 0 : item == 2 ? 1 : 2;
                    if (++mismatches[category] <= 3)
                        std::printf("BAD mode=%d item=%d (%a,%a)/(%a,%a) actual=%a expected=%a\n",
                            mode, item, a, b, c, d, actual[item], expected[item]);
                }
            if (predicates && (actual[6] != double(magnitude < 1e-9) || actual[7] != double(magnitude < 1e-10)))
                if (++predicateMismatches <= 3)
                    std::printf("PREDICATE BAD mode=%d (%a,%a)\n", mode, a, b);
            const int expectedMode = mode == FE_TONEAREST ? 1 : mode == FE_UPWARD ? 2 : mode == FE_DOWNWARD ? 3 : 0;
            if (actual[5] != expectedMode) ++modeMismatches;
            ++tested;
        };
        for (double a : edges)
            for (double b : edges)
            {
                check(a, b, 1., -1.);
                check(1., -1., a, b);
            }
        if (predicates)
            for (double threshold : { 1e-9, 1e-10 })
                for (double bound : { 1., 2. })
                    for (int angle = 0; angle < 201; ++angle)
                        for (int step = -20; step <= 20; ++step)
                        {
                            const double theta = 6.283185307179586 * angle / 201.;
                            double radius = threshold * bound;
                            for (int i = 0; i < std::abs(step); ++i)
                                radius = std::nextafter(radius, step < 0 ? 0. : infinity);
                            check(radius * std::cos(theta), radius * std::sin(theta), 1., -1.);
                        }
        for (long i = 0; i < randomCases; ++i)
            check(randomDouble(rng), randomDouble(rng), randomDouble(rng), randomDouble(rng));
        std::printf("{\"rounding\":%d,\"cases\":%ld,\"division_mismatches\":%ld,\"hypot_mismatches\":%ld,"
                    "\"complex_sqrt_mismatches\":%ld,\"mode_query_mismatches\":%ld,\"predicate_mismatches\":%ld}\n",
            mode, tested, mismatches[0], mismatches[1], mismatches[2], modeMismatches, predicateMismatches);
        std::fflush(stdout);
        totalMismatches += mismatches[0] + mismatches[1] + mismatches[2] + modeMismatches + predicateMismatches;
    }
    std::fesetround(initialMode);
    return totalMismatches ? 1 : 0;
}
