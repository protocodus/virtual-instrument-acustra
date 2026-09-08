// Isolated, independently written modal string experiment. No plugin linkage.
// Equations/provenance and units: NonlinearStringPrototype.md.
// Build: c++ -std=c++20 -O3 NonlinearStringPrototype.cpp -o /tmp/nonlinear-string
// Run: /tmp/nonlinear-string --self-test /tmp/new-output-directory
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace experiment
{
using Vector = std::vector<double>;
constexpr double pi = std::numbers::pi;
enum class Potential { linear, cubic, exact, kirchhoff };

const char* name(Potential p)
{
    switch (p) {
        case Potential::linear: return "linear";
        case Potential::cubic: return "cubic";
        case Potential::exact: return "exact";
        case Potential::kirchhoff: return "kirchhoff";
    }
    return "unknown";
}

struct Physical
{
    // Plain G3 nylon from the current engine, avoiding a wound-string axial
    // modulus inferred from composite density. Measured EI is independent EA.
    double length = 0.650;
    double density = 1140.0 * pi * 0.001 * 0.001 / 4.0; // kg/m
    double tension = density * std::pow(2.0 * length * 195.99771799, 2.0);
    double axial = 2.7e9 * pi * 0.001 * 0.001 / 4.0; // EA, N
    double bending = 310e-6;                         // EI, N m^2
};

double dot(const Vector& a, const Vector& b)
{
    double result = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) result += a[i] * b[i];
    return result;
}

struct Model
{
    Physical physical;
    Potential potential;
    int modes, intervals;
    double mass;                       // kg, for u=sum q_n sin(n*pi*x/L)
    Vector waveNumber, stiffness, slopeMatrix, weights, output, forceShape;

    Model(Potential kind, int count = 12, int grid = 64, Physical p = {})
        : physical(p), potential(kind), modes(count), intervals(grid),
          mass(p.density * p.length / 2.0), waveNumber(count), stiffness(count),
          slopeMatrix(static_cast<std::size_t>((grid + 1) * count)),
          weights(grid + 1), output(count), forceShape(count)
    {
        if (count < 1 || grid < 2 * count + 1 || !(p.axial > p.tension))
            throw std::invalid_argument("invalid modal/grid/rigidity domain");
        for (int n = 0; n < modes; ++n)
        {
            const double k = (n + 1) * pi / p.length;
            waveNumber[n] = k;
            stiffness[n] = p.length / 2.0 * (p.tension * k * k + p.bending * std::pow(k, 4));
            output[n] = std::sin((n + 1) * pi * 0.371);
            forceShape[n] = std::sin((n + 1) * pi * 0.173);
            for (int i = 0; i <= intervals; ++i)
                slopeMatrix[static_cast<std::size_t>(i * modes + n)]
                    = k * std::cos((n + 1) * pi * i / intervals);
        }
        for (int i = 0; i <= intervals; ++i)
            weights[i] = p.length / intervals * (i == 0 || i == intervals ? 0.5 : 1.0);
    }

    // Phi [J]; its exact derivative with respect to modal displacement [N].
    // The same metre weights are used in both. No state-dependent allocations.
    double evaluate(const Vector& q, Vector* gradient = nullptr) const
    {
        if (gradient) std::fill(gradient->begin(), gradient->end(), 0.0);
        if (potential == Potential::linear) return 0.0;
        const double g = physical.axial - physical.tension;
        if (potential == Potential::kirchhoff)
        {
            double slopeSquaredIntegral = 0.0;
            for (int n = 0; n < modes; ++n)
                slopeSquaredIntegral += physical.length / 2.0 * waveNumber[n] * waveNumber[n] * q[n] * q[n];
            if (gradient)
                for (int n = 0; n < modes; ++n)
                    (*gradient)[n] = g / (2.0 * physical.length) * slopeSquaredIntegral
                        * physical.length / 2.0 * waveNumber[n] * waveNumber[n] * q[n];
            return g / (8.0 * physical.length) * slopeSquaredIntegral * slopeSquaredIntegral;
        }
        double energy = 0.0;
        for (int i = 0; i <= intervals; ++i)
        {
            double slope = 0.0;
            const double* row = slopeMatrix.data() + i * modes;
            for (int n = 0; n < modes; ++n) slope += row[n] * q[n];
            const double square = slope * slope;
            double stress;
            if (potential == Potential::cubic)
            {
                energy += weights[i] * g * square * square / 8.0;
                stress = g * square * slope / 2.0;
            }
            else
            {
                const double root = std::sqrt(1.0 + square);
                const double strain = square / (root + 1.0); // no small-slope cancellation
                energy += weights[i] * g * strain * strain / 2.0;
                stress = g * strain * slope / root;
            }
            if (gradient)
                for (int n = 0; n < modes; ++n)
                    (*gradient)[n] += weights[i] * stress * row[n];
        }
        return energy;
    }

    double bridgeForce(const Vector& q) const
    {
        double slope = 0.0, force = 0.0;
        double slopeSquaredIntegral = 0.0;
        for (int n = 0; n < modes; ++n)
        {
            const double k = waveNumber[n];
            slope += k * q[n];
            force += (physical.tension * k + physical.bending * k * k * k) * q[n];
            slopeSquaredIntegral += physical.length / 2.0 * k * k * q[n] * q[n];
        }
        const double g = physical.axial - physical.tension;
        if (potential == Potential::cubic) force += g * slope * slope * slope / 2.0;
        if (potential == Potential::exact)
        {
            const double root = std::sqrt(1.0 + slope * slope);
            force += g * slope * slope / (root + 1.0) * slope / root;
        }
        if (potential == Potential::kirchhoff)
            force += g / (2.0 * physical.length) * slopeSquaredIntegral * slope;
        return force;
    }

    Vector pluck(double amplitude, bool oneMode = false) const
    {
        Vector q(modes, 0.0);
        if (oneMode) { q[0] = amplitude; return q; }
        constexpr double position = 0.173, sigmaFraction = 0.012;
        for (int n = 0; n < modes; ++n)
        {
            const double order = n + 1;
            q[n] = 2.0 * amplitude * std::sin(pi * order * position)
                / (pi * pi * order * order * position * (1.0 - position))
                * std::exp(-0.5 * pi * pi * order * order * sigmaFraction * sigmaFraction);
        }
        return q;
    }
};

struct Sav
{
    const Model& model;
    double dt, lambda, gauge, psi;
    Vector q, previous, next, gradient, midpoint, g, inverseG, rhs;
    double inputWork = 0.0, dissipatedWork = 0.0;
    double smallestDenominator = std::numeric_limits<double>::max(), largestDenominator = 0.0;
    double minimumPsi = std::numeric_limits<double>::max();

    Sav(const Model& m, double rate, double regulation = 1000.0, double shift = 1e-12)
        : model(m), dt(1.0 / rate), lambda(regulation), gauge(shift), psi(0.0),
          q(m.modes), previous(m.modes), next(m.modes), gradient(m.modes),
          midpoint(m.modes), g(m.modes), inverseG(m.modes), rhs(m.modes)
    {
        if (!(shift > 0.0) || regulation < 0.0 || regulation >= rate)
            throw std::invalid_argument("invalid gauge/regulator domain");
        for (double k : m.stiffness)
            if (!(dt * dt * k / m.mass < 4.0))
                throw std::invalid_argument("linear CFL positivity condition violated");
    }

    void initialise(const Vector& displacement)
    {
        q = displacement;
        model.evaluate(q, &gradient);
        // Consistent rest preload: q(-dt)=q(0)+dt^2*a(0)/2, not q(-dt)=q(0).
        for (int n = 0; n < model.modes; ++n)
        {
            previous[n] = q[n] - dt * dt / (2.0 * model.mass)
                * (model.stiffness[n] * q[n] + gradient[n]);
            midpoint[n] = 0.5 * (q[n] + previous[n]);
        }
        psi = std::sqrt(2.0 * model.evaluate(midpoint) + gauge);
        inputWork = dissipatedWork = 0.0;
        smallestDenominator = std::numeric_limits<double>::max();
        largestDenominator = 0.0;
        minimumPsi = psi;
    }

    double energy() const
    {
        double e = 0.5 * (psi * psi - gauge);
        for (int n = 0; n < model.modes; ++n)
            e += 0.5 * model.mass * std::pow((q[n] - previous[n]) / dt, 2)
               + 0.5 * model.stiffness[n] * q[n] * previous[n];
        return e;
    }

    double driftEnergy()
    {
        for (int n = 0; n < model.modes; ++n) midpoint[n] = 0.5 * (q[n] + previous[n]);
        return 0.5 * (psi * psi - gauge) - model.evaluate(midpoint);
    }

    // Force in N at the fixed declared excitation location. eta [s^-1] gives
    // modal viscous C=2*m*eta; changing it does not reset any mechanical state.
    void step(double force = 0.0, double eta = 0.0)
    {
        const double phi = model.evaluate(q, &gradient);
        const double truth = std::sqrt(2.0 * phi + gauge);
        double velocityNorm = 0.0;
        for (int n = 0; n < model.modes; ++n)
        {
            g[n] = gradient[n] / truth;
            midpoint[n] = 0.5 * (q[n] + previous[n]);
            velocityNorm += std::abs((q[n] - previous[n]) / dt);
        }
        if (lambda > 0.0 && velocityNorm > 1e-16)
        {
            const double error = psi - std::sqrt(2.0 * model.evaluate(midpoint) + gauge);
            const double correction = lambda * error / velocityNorm;
            for (int n = 0; n < model.modes; ++n)
                g[n] -= correction * ((q[n] > previous[n]) - (q[n] < previous[n]));
        }
        const double alpha = dt * dt / (4.0 * model.mass);
        const double inverseDiagonal = 1.0 / (1.0 + eta * dt);
        const double gp = dot(g, previous);
        for (int n = 0; n < model.modes; ++n)
        {
            rhs[n] = inverseDiagonal * (2.0 * q[n] - (1.0 - eta * dt) * previous[n]
                - dt * dt / model.mass * model.stiffness[n] * q[n]
                + alpha * g[n] * (gp - 4.0 * psi)
                + dt * dt / model.mass * model.forceShape[n] * force);
            inverseG[n] = inverseDiagonal * g[n];
        }
        const double denominator = 1.0 + alpha * dot(g, inverseG);
        smallestDenominator = std::min(smallestDenominator, denominator);
        largestDenominator = std::max(largestDenominator, denominator);
        const double rankScale = alpha * dot(g, rhs) / denominator;
        double psiChange = 0.0;
        for (int n = 0; n < model.modes; ++n)
        {
            next[n] = rhs[n] - rankScale * inverseG[n];
            const double velocity = (next[n] - previous[n]) / (2.0 * dt);
            psiChange += 0.5 * g[n] * (next[n] - previous[n]);
            inputWork += dt * force * model.forceShape[n] * velocity;
            dissipatedWork += dt * 2.0 * model.mass * eta * velocity * velocity;
        }
        psi += psiChange;
        minimumPsi = std::min(minimumPsi, psi);
        previous.swap(q);
        q.swap(next);
    }
};

struct Rk4
{
    const Model& model;
    Vector q, v, trialQ, trialV, gradient;
    std::array<Vector, 4> dq, dv;
    Rk4(const Model& m, const Vector& initial)
        : model(m), q(initial), v(m.modes), trialQ(m.modes), trialV(m.modes), gradient(m.modes)
    {
        for (auto& x : dq) x.resize(m.modes);
        for (auto& x : dv) x.resize(m.modes);
    }
    void step(double dt)
    {
        for (int stage = 0; stage < 4; ++stage)
        {
            const double fraction = stage == 3 ? 1.0 : 0.5;
            for (int n = 0; n < model.modes; ++n)
            {
                trialQ[n] = q[n] + (stage == 0 ? 0.0 : fraction * dt * dq[stage - 1][n]);
                trialV[n] = v[n] + (stage == 0 ? 0.0 : fraction * dt * dv[stage - 1][n]);
            }
            model.evaluate(trialQ, &gradient);
            for (int n = 0; n < model.modes; ++n)
            {
                dq[stage][n] = trialV[n];
                dv[stage][n] = -(model.stiffness[n] * trialQ[n] + gradient[n]) / model.mass;
            }
        }
        for (int n = 0; n < model.modes; ++n)
        {
            q[n] += dt / 6.0 * (dq[0][n] + 2.0 * dq[1][n] + 2.0 * dq[2][n] + dq[3][n]);
            v[n] += dt / 6.0 * (dv[0][n] + 2.0 * dv[1][n] + 2.0 * dv[2][n] + dv[3][n]);
        }
    }
};

struct Metrics
{
    double maximumBalance = 0.0, maximumDrift = 0.0, minimumEnergy = 1e100;
    double minimumPsi = 1e100, displacementRms = 0.0, forceRms = 0.0, dcDb = 0.0;
    double modal3Energy = 0.0, finalEnergy = 0.0;
    double minimumDenominator = 0.0, maximumDenominator = 0.0;
};

Metrics render(const Model& model, int rate, double seconds, double amplitude,
               double lambda, double gauge, bool repeated, bool oneMode,
               const std::filesystem::path& csv = {})
{
    Sav solver(model, rate, lambda, gauge);
    solver.initialise(model.pluck(amplitude, oneMode));
    const double initial = solver.energy();
    Metrics metrics;
    double sum = 0.0, squares = 0.0, forceSquares = 0.0;
    std::ofstream stream;
    if (!csv.empty()) { stream.open(csv); stream << std::setprecision(17)
        << "time,displacement_m,bridge_force_n,mode1_m,mode3_m,energy_j,drift_j,psi_sqrt_j\n"; }
    const int frames = static_cast<int>(rate * seconds);
    for (int sample = 0; sample < frames; ++sample)
    {
        const double t = static_cast<double>(sample) / rate;
        const double u = dot(model.output, solver.q);
        const double force = model.bridgeForce(solver.q);
        const double drift = solver.driftEnergy();
        if (stream) stream << t << ',' << u << ',' << force << ',' << solver.q[0]
                           << ',' << (model.modes >= 3 ? solver.q[2] : 0.0)
                           << ',' << solver.energy() << ',' << drift << ',' << solver.psi << '\n';
        sum += u; squares += u * u; forceSquares += force * force;
        metrics.maximumDrift = std::max(metrics.maximumDrift, std::abs(drift));
        if (model.modes >= 3)
        {
            const double v3 = (solver.q[2] - solver.previous[2]) * rate;
            metrics.modal3Energy = std::max(metrics.modal3Energy,
                0.5 * model.mass * v3 * v3 + 0.5 * model.stiffness[2] * solver.q[2] * solver.q[2]);
        }
        double input = 0.0;
        if (repeated)
            for (const double start : { 0.050, 0.100, 0.150 })
            {
                const double local = t - start;
                if (local >= 0.0 && local < 0.001)
                    input += 0.5 * (1.0 - std::cos(2.0 * pi * local / 0.001));
            }
        const double damping = repeated ? (t >= 0.20 ? 100.0 : 1.0) : 0.0;
        solver.step(input, damping);
        const double e = solver.energy();
        const double scale = std::max(initial + std::abs(solver.inputWork), 1e-30);
        metrics.maximumBalance = std::max(metrics.maximumBalance,
            std::abs(e - initial - solver.inputWork + solver.dissipatedWork) / scale);
        metrics.minimumEnergy = std::min(metrics.minimumEnergy, e);
        if (!std::isfinite(e) || !std::isfinite(solver.psi))
            throw std::runtime_error("nonfinite nonlinear trajectory");
    }
    metrics.maximumDrift /= std::max(initial, 1e-30);
    metrics.modal3Energy /= std::max(initial, 1e-30);
    metrics.minimumPsi = solver.minimumPsi;
    metrics.minimumDenominator = solver.smallestDenominator;
    metrics.maximumDenominator = solver.largestDenominator;
    metrics.displacementRms = std::sqrt(squares / frames);
    metrics.forceRms = std::sqrt(forceSquares / frames);
    metrics.dcDb = 20.0 * std::log10(std::max(std::abs(sum / frames)
        / std::max(metrics.displacementRms, 1e-30), 1e-30));
    metrics.finalEnergy = solver.energy() / std::max(initial, 1e-30);
    return metrics;
}

struct Audit
{
    std::ofstream json;
    int failures = 0, rows = 0;
    explicit Audit(const std::filesystem::path& path) : json(path)
    { json << std::setprecision(17) << "{\n\"measurements\":[\n"; }
    void check(bool condition, const char* message)
    { if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
    void add(const std::string& fields)
    { json << (rows++ ? ",\n" : "") << '{' << fields << '}'; json.flush(); }
    void finish() { json << "\n],\"failures\":" << failures << "}\n"; }
};

std::string number(double value)
{
    std::ostringstream s; s << std::setprecision(17) << value; return s.str();
}

struct Difference
{
    double error = 0.0, norm = 0.0, forceError = 0.0, forceNorm = 0.0;
    void add(const Model& model, const Vector& candidate, const Vector& reference)
    {
        for (int n = 0; n < model.modes; ++n)
        { error += std::pow(candidate[n] - reference[n], 2); norm += reference[n] * reference[n]; }
        const double a = model.bridgeForce(candidate), b = model.bridgeForce(reference);
        forceError += (a - b) * (a - b); forceNorm += b * b;
    }
    double displacement() const { return std::sqrt(error / std::max(norm, 1e-30)); }
    double force() const { return std::sqrt(forceError / std::max(forceNorm, 1e-30)); }
};

// Mean period between linearly interpolated rising zero crossings. The
// frequency experiment uses one spatial mode, so a single period is defined.
double measuredFrequency(const Model& model, int rate, double height, double lambda)
{
    Sav solver(model, rate, lambda);
    solver.initialise(model.pluck(height, true));
    double first = 0.0, last = 0.0, previous = solver.q[0];
    int crossings = 0;
    for (int n = 1; n <= rate / 2; ++n)
    {
        solver.step();
        const double current = solver.q[0];
        if (previous < 0.0 && current >= 0.0)
        {
            const double crossing = (n - 1 + (-previous) / (current - previous)) / rate;
            if (crossings++ == 0) first = crossing;
            last = crossing;
        }
        previous = current;
    }
    if (crossings < 3) throw std::runtime_error("insufficient frequency crossings");
    return (crossings - 1) / (last - first);
}

std::string metricFields(const Metrics& m)
{
    std::ostringstream s; s << std::setprecision(17)
        << "\"balance_relative\":" << m.maximumBalance
        << ",\"drift_energy_relative\":" << m.maximumDrift
        << ",\"minimum_energy_j\":" << m.minimumEnergy
        << ",\"minimum_psi\":" << m.minimumPsi
        << ",\"minimum_rank_denominator\":" << m.minimumDenominator
        << ",\"maximum_rank_denominator\":" << m.maximumDenominator
        << ",\"displacement_rms_m\":" << m.displacementRms
        << ",\"force_rms_n\":" << m.forceRms
        << ",\"finite_window_dc_db\":" << m.dcDb
        << ",\"mode3_peak_energy_fraction\":" << m.modal3Energy
        << ",\"final_energy_fraction\":" << m.finalEnergy;
    return s.str();
}

int selfTest(const std::filesystem::path& output)
{
    if (!std::filesystem::create_directory(output))
        throw std::invalid_argument("output directory must be new");
    Audit audit(output / "report.json");
    std::cout << "Checking potential gradients and spatial quadrature...\n" << std::flush;
    for (auto kind : { Potential::cubic, Potential::exact, Potential::kirchhoff })
    {
        double worst = 0.0;
        for (double height : { 1e-7, 1e-4, 0.01 })
        {
            Model model(kind);
            Vector q = model.pluck(height), direction(model.modes), gradient(model.modes);
            for (int n = 0; n < model.modes; ++n) direction[n] = std::cos(0.73 * n) / (n + 1);
            model.evaluate(q, &gradient);
            const double exact = dot(gradient, direction), h = height * 1e-5;
            Vector above = q, below = q;
            for (int n = 0; n < model.modes; ++n) { above[n] += h * direction[n]; below[n] -= h * direction[n]; }
            const double finite = (model.evaluate(above) - model.evaluate(below)) / (2.0 * h);
            worst = std::max(worst, std::abs(finite - exact) / std::max(std::abs(exact), 1e-30));
        }
        audit.check(worst < 1e-6, "potential/gradient unit or weight mismatch");
        audit.add("\"test\":\"directional_gradient\",\"model\":\"" + std::string(name(kind))
                  + "\",\"maximum_relative_error\":" + number(worst));
    }

    // Odd displacement/force symmetry is a meaningful intrinsic-bias check.
    // A finite render's mean alone depends on its cut phase and is not DC.
    for (double lambda : { 0.0, 1000.0 })
    {
        Model model(Potential::exact);
        Sav positive(model, 48000, lambda), negative(model, 48000, lambda);
        positive.initialise(model.pluck(0.004));
        negative.initialise(model.pluck(-0.004));
        double displacementError = 0.0, forceError = 0.0, auxiliaryError = 0.0;
        for (int n = 0; n < 12000; ++n)
        {
            for (int mode = 0; mode < model.modes; ++mode)
                displacementError = std::max(displacementError, std::abs(positive.q[mode] + negative.q[mode]));
            forceError = std::max(forceError, std::abs(model.bridgeForce(positive.q) + model.bridgeForce(negative.q)));
            auxiliaryError = std::max(auxiliaryError, std::abs(positive.psi - negative.psi));
            const double force = n >= 2400 && n < 2448 ? 0.5 * (1.0 - std::cos(2.0 * pi * (n - 2400) / 48.0)) : 0.0;
            const double eta = n >= 9600 ? 100.0 : 1.0;
            positive.step(force, eta); negative.step(-force, eta);
        }
        audit.check(displacementError < 1e-14 && forceError < 1e-10 && auxiliaryError < 1e-12,
                    "nonlinear solver violates signed force/displacement symmetry");
        audit.add("\"test\":\"signed_symmetry\",\"lambda\":" + number(lambda)
            + ",\"maximum_displacement_sum_m\":" + number(displacementError)
            + ",\"maximum_force_sum_n\":" + number(forceError)
            + ",\"maximum_auxiliary_difference\":" + number(auxiliaryError));
    }
    for (auto kind : { Potential::cubic, Potential::exact })
        for (double height : { 0.01, 0.10 })
        {
            Model reference(kind, 12, 768);
            Vector q(reference.modes), referenceGradient(reference.modes);
            // Dense modal stress case, deliberately much larger than a guitar
            // pluck at 0.10 m. This checks quadrature, not physical calibration.
            for (int n = 0; n < reference.modes; ++n)
                q[n] = height * std::cos(0.73 * n) / std::pow(n + 1, 1.3);
            const double referenceEnergy = reference.evaluate(q, &referenceGradient);
            for (int intervals : { 25, 48, 96, 192 })
            {
                Model candidate(kind, 12, intervals);
                Vector gradient(candidate.modes);
                const double energy = candidate.evaluate(q, &gradient);
                double error = 0.0;
                for (int n = 0; n < candidate.modes; ++n)
                    error += std::pow(gradient[n] - referenceGradient[n], 2);
                const double energyError = std::abs(energy / referenceEnergy - 1.0);
                const double gradientError = std::sqrt(error / dot(referenceGradient, referenceGradient));
                if (kind == Potential::cubic || intervals == 192)
                    audit.check(energyError < 1e-9 && gradientError < 1e-9,
                                "spatial potential/gradient quadrature did not converge");
                audit.add("\"test\":\"spatial_quadrature\",\"model\":\"" + std::string(name(kind))
                    + "\",\"height_m\":" + number(height) + ",\"intervals\":" + std::to_string(intervals)
                    + ",\"relative_energy_error\":" + number(energyError)
                    + ",\"relative_gradient_error\":" + number(gradientError));
            }
        }

    {
        Model model(Potential::exact);
        Sav rest(model, 48000);
        rest.initialise(model.pluck(0.0));
        for (int n = 0; n < 48000; ++n) rest.step(0.0, n < 24000 ? 0.0 : 100.0);
        const bool zeroDisplacement = dot(rest.q, rest.q) == 0.0;
        // sqrt(gauge)^2 need not round back to gauge exactly in binary64.
        audit.check(zeroDisplacement && std::abs(rest.energy())
                    <= std::numeric_limits<double>::epsilon() * rest.gauge,
                    "zero-state solver generated displacement or energy");
        bool rejected = false;
        try { Sav invalid(model, 1000, 0.0); } catch (const std::invalid_argument&) { rejected = true; }
        audit.check(rejected, "solver accepted a rate beyond its linear CFL domain");
        audit.add("\"test\":\"zero_state_and_cfl\",\"zero_displacement\":"
                  + std::string(zeroDisplacement ? "true" : "false")
                  + ",\"energy_roundoff_j\":" + number(rest.energy()) + ",\"invalid_rate_rejected\":"
                  + std::string(rejected ? "true" : "false"));
    }

    std::cout << "Checking linear poles, energy and amplitude limit...\n" << std::flush;
    Model linear(Potential::linear);
    for (int rate : { 48000, 96000, 192000 })
    {
        Sav solver(linear, rate);
        solver.initialise(linear.pluck(0.001, true));
        const double omega = std::sqrt(linear.stiffness[0] / linear.mass);
        const double theta = 2.0 * std::asin(omega / (2.0 * rate));
        double maximum = 0.0;
        for (int n = 0; n < rate / 5; ++n)
        {
            maximum = std::max(maximum, std::abs(solver.q[0] - 0.001 * std::cos(theta * n)));
            solver.step();
        }
        audit.check(maximum < 1e-12, "linear recurrence differs from its exact discrete pole");
        std::ostringstream row; row << std::setprecision(17)
            << "\"test\":\"linear\",\"rate\":" << rate << ",\"max_displacement_error_m\":" << maximum
            << ",\"physical_frequency_error_cents\":" << 1200.0 * std::log2(theta * rate / omega);
        audit.add(row.str());
    }
    for (auto kind : { Potential::cubic, Potential::exact, Potential::kirchhoff })
        for (double lambda : { 0.0, 1000.0 })
        {
            Model model(kind);
            const auto metrics = render(model, 48000, 0.30, 0.004, lambda, 1e-12, false, true,
                output / (std::string(name(kind)) + "-lambda" + std::to_string(static_cast<int>(lambda)) + ".csv"));
            audit.check(metrics.maximumBalance < 1e-8, "lossless SAV energy balance failed");
            audit.check(metrics.minimumPsi >= 0.0 && metrics.minimumDenominator >= 1.0,
                        "lossless trajectory violated auxiliary sign or rank denominator domain");
            if (kind == Potential::kirchhoff)
                audit.check(metrics.modal3Energy < 1e-25, "Kirchhoff unexpectedly transfers mode1 into mode3");
            else
                audit.check(metrics.modal3Energy > 1e-8, "local slope model did not transfer intermodal energy");
            audit.add("\"test\":\"lossless_single_mode\",\"model\":\"" + std::string(name(kind))
                + "\",\"lambda\":" + std::to_string(lambda) + ',' + metricFields(metrics));
        }
    double previousAmplitudeError = 0.0;
    for (double height : { 1e-4, 5e-5, 2.5e-5 })
    {
        Model exact(Potential::exact);
        Sav nonlinear(exact, 48000), baseline(linear, 48000);
        nonlinear.initialise(exact.pluck(height)); baseline.initialise(linear.pluck(height));
        double error = 0.0, norm = 0.0;
        for (int n = 0; n < 4800; ++n)
        {
            const double a = dot(exact.output, nonlinear.q), b = dot(linear.output, baseline.q);
            error += (a - b) * (a - b); norm += b * b;
            nonlinear.step(); baseline.step();
        }
        const double relative = std::sqrt(error / norm);
        audit.check(relative < 0.01, "small-amplitude nonlinear limit is not linear");
        if (previousAmplitudeError > 0.0)
            audit.check(relative < 0.30 * previousAmplitudeError,
                        "normalized nonlinear error does not vanish quadratically with amplitude");
        previousAmplitudeError = relative;
        std::ostringstream row; row << std::setprecision(17) << "\"test\":\"amplitude_limit\",\"height_m\":"
            << height << ",\"relative_displacement_error\":" << relative; audit.add(row.str());
    }

    std::cout << "Checking single-mode pitch against weak Duffing theory...\n" << std::flush;
    for (int rate : { 48000, 192000 })
        for (auto kind : { Potential::cubic, Potential::exact, Potential::kirchhoff })
        {
            Model model(kind, 1, 8), baseline(Potential::linear, 1, 8);
            const double linearFrequency = measuredFrequency(baseline, rate, 0.001, 1000.0);
            const double k = model.waveNumber[0], g = model.physical.axial - model.physical.tension;
            const double beta = g * model.physical.length * std::pow(k, 4)
                * (kind == Potential::kirchhoff ? 1.0 / 8.0 : 3.0 / 16.0);
            double previousShift = 0.0;
            for (double height : { 0.001, 0.002, 0.004 })
            {
                const double frequency = measuredFrequency(model, rate, height, 1000.0);
                const double shift = frequency / linearFrequency - 1.0;
                const double theory = 3.0 * beta * height * height / (8.0 * model.stiffness[0]);
                audit.check(shift > previousShift, "single-mode pitch does not rise with pluck amplitude");
                audit.check(std::abs(shift / theory - 1.0) < 0.03,
                            "single-mode pitch differs from weak Duffing prediction");
                previousShift = shift;
                audit.add("\"test\":\"single_mode_pitch\",\"model\":\"" + std::string(name(kind))
                    + "\",\"rate\":" + std::to_string(rate) + ",\"height_m\":" + number(height)
                    + ",\"linear_frequency_hz\":" + number(linearFrequency)
                    + ",\"frequency_hz\":" + number(frequency)
                    + ",\"shift_cents\":" + number(1200.0 * std::log2(1.0 + shift))
                    + ",\"weak_theory_shift_cents\":" + number(1200.0 * std::log2(1.0 + theory)));
            }
        }

    std::cout << "Comparing time steps with independent RK4...\n" << std::flush;
    Model exact(Potential::exact);
    constexpr int referenceRate = 768000;
    constexpr int baseRate = 48000;
    constexpr int samples = baseRate / 10;
    std::vector<Vector> reference(samples);
    Rk4 rk(exact, exact.pluck(0.004));
    for (int n = 0; n < samples * (referenceRate / baseRate); ++n)
    {
        if (n % (referenceRate / baseRate) == 0) reference[n / (referenceRate / baseRate)] = rk.q;
        rk.step(1.0 / referenceRate);
    }
    // Independent reference self-convergence must be much finer than the
    // smallest candidate step. Report rather than assume RK4 is exact.
    Rk4 coarseReference(exact, exact.pluck(0.004));
    Difference referenceDifference;
    for (int n = 0; n < samples * (referenceRate / 2 / baseRate); ++n)
    {
        if (n % (referenceRate / 2 / baseRate) == 0)
            referenceDifference.add(exact, coarseReference.q, reference[n / (referenceRate / 2 / baseRate)]);
        coarseReference.step(2.0 / referenceRate);
    }
    audit.check(referenceDifference.displacement() < 1e-6 && referenceDifference.force() < 1e-5,
                "independent RK4 reference is not sufficiently converged");
    audit.add("\"test\":\"rk4_self_convergence\",\"rates\":[384000,768000],\"relative_modal_displacement_error\":"
        + number(referenceDifference.displacement()) + ",\"relative_bridge_force_error\":"
        + number(referenceDifference.force()));
    for (double lambda : { 0.0, 1000.0 })
    {
        Difference previousError;
        for (int rate : { 48000, 96000, 192000 })
        {
            Sav solver(exact, rate, lambda);
            solver.initialise(exact.pluck(0.004));
            Difference difference;
            for (int n = 0; n < samples * (rate / baseRate); ++n)
            {
                if (n % (rate / baseRate) == 0)
                {
                    difference.add(exact, solver.q, reference[n / (rate / baseRate)]);
                }
                solver.step();
            }
            if (rate > baseRate)
                audit.check(difference.displacement() < 0.5 * previousError.displacement()
                            && difference.force() < 0.5 * previousError.force(),
                            "SAV displacement or bridge force failed time-step convergence");
            std::ostringstream row; row << std::setprecision(17)
                << "\"test\":\"time_convergence\",\"rate\":" << rate << ",\"lambda\":" << lambda
                << ",\"relative_modal_displacement_error\":" << difference.displacement()
                << ",\"relative_bridge_force_error\":" << difference.force();
            if (rate > baseRate)
                row << ",\"observed_displacement_order\":"
                    << std::log2(previousError.displacement() / difference.displacement())
                    << ",\"observed_force_order\":" << std::log2(previousError.force() / difference.force());
            previousError = difference;
            audit.add(row.str());
        }
    }

    std::cout << "Checking repeated forcing, release damping and gauge...\n" << std::flush;
    for (auto kind : { Potential::exact, Potential::kirchhoff })
        for (double lambda : { 0.0, 1000.0 })
            for (double gauge : { 1e-14, 1e-10 })
            {
                Model model(kind);
                const auto csv = gauge == 1e-14
                    ? output / ("repeated-" + std::string(name(kind)) + "-lambda"
                        + std::to_string(static_cast<int>(lambda)) + ".csv")
                    : std::filesystem::path{};
                const auto metrics = render(model, 48000, 0.4, 0.002, lambda, gauge, true, false, csv);
                audit.check(metrics.maximumBalance < 1e-8, "forced/damped power balance failed");
                audit.check(metrics.minimumPsi >= 0.0 && metrics.minimumDenominator >= 1.0,
                            "forced/damped trajectory violated auxiliary sign or rank denominator domain");
                const bool releaseGatePassed = std::abs(metrics.finalEnergy) < 1e-4;
                if (lambda > 0.0)
                    audit.check(releaseGatePassed, "regulated release damping failed to remove the excited energy");
                else if (kind == Potential::exact)
                    audit.check(!releaseGatePassed, "unregulated local-slope negative control lost its residual-energy sensitivity");
                std::ostringstream row; row << std::setprecision(17)
                    << "\"test\":\"repeated_release\",\"model\":\"" << name(kind)
                    << "\",\"lambda\":" << lambda << ",\"gauge_j\":" << gauge
                    << ",\"role\":\"" << (lambda > 0.0 ? "candidate" : "unregulated_control")
                    << "\",\"release_gate_passed\":" << (releaseGatePassed ? "true" : "false")
                    << ',' << metricFields(metrics);
                audit.add(row.str());
            }
    audit.finish();
    std::cout << "Prototype audit failures: " << audit.failures << '\n';
    return audit.failures == 0 ? 0 : 1;
}
} // namespace experiment

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--self-test")
            return experiment::selfTest(argv[2]);
        std::cerr << "usage: NonlinearStringPrototype --self-test NEW_OUTPUT_DIRECTORY\n";
        return 2;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
