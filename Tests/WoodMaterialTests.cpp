#include "DSP/AcustraEngine.h"
#include "DSP/MeasuredBodyData.h"
#include "DSP/MeasuredBridgeData.h"
#include "DSP/MicrophoneBalanceData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static auto properties(BodyMaterial wood) { return AcustraEngine::bodyWoodProperties(wood); }
    static auto factors(const EngineParameters& p) { return AcustraEngine::bodyWoodFactors(p); }
    static auto pole(const EngineParameters& p, int i)
    { return AcustraEngine::radiationModePole(p, fittedPhysicalCalibration, i); }
    static auto bridge(const AcustraEngine& e) { return e.bridgeLoad_; }
    static auto table(const AcustraEngine& e) { return e.bridgeMobilityTable(); }
    static std::complex<double> residue(const AcustraEngine& e, int i)
    { return { e.bodyBank_.rightReal[std::size_t(i)], e.bodyBank_.rightImaginary[std::size_t(i)] }; }
    static std::complex<float> port(const AcustraEngine& e, float f, int string)
    { return e.bridgePortMobility(f, string).normal; }
    static double radius(const AcustraEngine& e, int i)
    { return std::hypot(double(e.bodyBank_.poleReal[std::size_t(i)]), double(e.bodyBank_.poleImaginary[std::size_t(i)])); }
    static int count(const AcustraEngine& e) { return e.bodyBank_.count; }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Access = acustra::AcustraEngineTestAccess;
using Parameters = acustra::EngineParameters;
using Wood = acustra::BodyMaterial;
constexpr double pi = 3.141592653589793238462643383279502884;
int failures = 0;
void expect(bool pass, const std::string& message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct Material
{
    double rho, longitudinal, radial, shear, poisson;
};
// Independent source values: USDA Wood Handbook 2021, Tables 5-1, 5-2,
// 5-3a and 5-5a, representative clear wood at 12% moisture, L/R plane:
// https://research.fs.usda.gov/download/treesearch/62244.pdf
// Bending
// E receives the handbook's approximate 10% axial correction. Spruce/Maple
// SG12 converts from oven-dry mass at current volume with x1.12. Mahogany
// uses Queensland's 530 kg/m3 at 12% for American mahogany (Swietenia).
// https://www.business.qld.gov.au/industries/farms-fishing-forestry/forests-wood/properties-timbers/american-mahogany
// These are representative specimens, not guarantees about a guitar's wood.
constexpr std::array<Material, 3> materials {{
    { 0.40 * 1120.0, 10.8e9 * 1.1, 10.8e9 * 1.1 * .078, 10.8e9 * 1.1 * .064, .372 },
    { 530.0, 10.3e9 * 1.1, 10.3e9 * 1.1 * .107, 10.3e9 * 1.1 * .066, .314 },
    { 0.63 * 1120.0, 12.6e9 * 1.1, 12.6e9 * 1.1 * .132, 12.6e9 * 1.1 * .111, .424 }
}};

double effectiveRigidity(const Material& m)
{
    const double denominator = 1.0 - m.poisson * m.poisson * m.radial / m.longitudinal;
    const double d11 = m.longitudinal / denominator, d22 = m.radial / denominator;
    const double d12 = m.poisson * m.radial / denominator;
    const auto reciprocalWaveSpeed = [&](double theta)
    {
        const double c2 = std::pow(std::cos(theta), 2), s2 = 1.0 - c2;
        return 1.0 / std::sqrt(d11*c2*c2 + 2.0*(d12 + 2.0*m.shear)*c2*s2 + d22*s2*s2);
    };
    // A dense double Simpson integral is independent of the production
    // quadrature. The common equal-thickness h^3/12 cancels in ratios.
    constexpr int intervals = 8192;
    const double step = (pi / 2.0) / intervals;
    double integral = reciprocalWaveSpeed(0.0) + reciprocalWaveSpeed(pi / 2.0);
    for (int i = 1; i < intervals; ++i)
        integral += (i % 2 == 0 ? 2.0 : 4.0) * reciprocalWaveSpeed(i * step);
    const double mean = integral * step / 3.0 / (pi / 2.0);
    return 1.0 / (mean * mean);
}

struct Ratios { double mass, stiffness, frequency, residue, floor; };
Ratios ratios(int wood)
{
    const auto& m = materials[std::size_t(wood)];
    const double mass = m.rho / materials[0].rho;
    const double stiffness = effectiveRigidity(m) / effectiveRigidity(materials[0]);
    const double frequency = std::sqrt(stiffness / mass);
    return { mass, stiffness, frequency, frequency / mass, 1.0 / std::sqrt(stiffness * mass) };
}

struct Geometry { double width, length, depth, hole, outline; };
constexpr std::array<Geometry,4> geometries {{
    { .343, .467, .108, .1016, .72 }, { .381, .492, .105, .1016, .72 },
    { .397, .508, .111, .1016, .75 }, { .432, .533, .124, .1016, .72 }
}};
double helmholtz(int shape)
{
    const auto& g = geometries[std::size_t(shape)];
    const double radius = g.hole * .5;
    const double volume = g.width * g.length * g.outline * g.depth;
    return 343.0 / (2.0*pi) * std::sqrt(pi*radius*radius / (volume*(.003 + 1.7*radius)));
}

struct Pair
{
    std::array<double,2> frequency, bridge, radiation;
    double trace, determinant;
};
Pair solve(double plateSquared, double cavitySquared, double holeSquared)
{
    // Symmetric mass-normalized stiffness A=M^-1/2 K M^-1/2.
    // Spectral projectors give participation without using production's
    // phi_h=1 eigenvector construction or its normalization formula.
    const double a = plateSquared + cavitySquared, d = holeSquared;
    const double b = std::sqrt(cavitySquared * holeSquared);
    const double gap = std::hypot(a-d, 2.0*b);
    const std::array<double,2> lambda { .5*(a+d-gap), .5*(a+d+gap) };
    Pair result {}; result.trace = a+d; result.determinant = a*d-b*b;
    for (int i=0;i<2;++i)
    {
        const double other = lambda[std::size_t(1-i)];
        const double denominator = lambda[std::size_t(i)]-other;
        const double p11 = (a-other)/denominator, p12 = b/denominator;
        result.frequency[std::size_t(i)] = std::sqrt(lambda[std::size_t(i)]);
        result.bridge[std::size_t(i)] = cavitySquared*p11;
        result.radiation[std::size_t(i)] = std::abs(cavitySquared*p11
            + std::sqrt(cavitySquared*holeSquared)*p12) * result.frequency[std::size_t(i)];
    }
    return result;
}

std::unique_ptr<Engine> engine(Parameters p, double rate=48000.0)
{
    auto e = std::make_unique<Engine>(); e->setParameters(p); e->prepare(rate,127); return e;
}

void materialPropertiesAndPlateLaw()
{
    double worst = 0.0;
    for (int wood=0;wood<3;++wood)
    {
        const auto p = Access::properties(static_cast<Wood>(wood));
        const auto& expected = materials[std::size_t(wood)];
        const std::array<double,5> values { p.densityKgPerCubicMetre,
            p.longitudinalYoungModulusPa, p.radialYoungModulusPa,
            p.longitudinalRadialShearModulusPa, p.longitudinalRadialPoissonRatio };
        const std::array<double,5> reference { expected.rho, expected.longitudinal,
            expected.radial, expected.shear, expected.poisson };
        for (int i=0;i<5;++i)
            expect(std::abs(values[std::size_t(i)] / reference[std::size_t(i)] - 1.0) < 2e-7,
                   "material property differs from its independently recorded source value");
        Parameters parameters; parameters.bodyMaterial=static_cast<Wood>(wood);
        const auto actual=Access::factors(parameters); const auto r=ratios(wood);
        worst=std::max({worst,std::abs(actual[0]/r.frequency-1.0),std::abs(actual[3]/r.residue-1.0)});
        expect(actual[1]==1.0f && actual[2]==1.0f,
               "Wood invented a species damping or brightness multiplier");
        if(wood==0) expect(actual==std::array<float,4>{1,1,1,1},"Spruce material reference is not exactly identity");
        std::cout << "wood " << wood << " mass=" << r.mass << " stiffness=" << r.stiffness
                  << " frequency=" << r.frequency << " floor=" << r.floor << '\n';
    }
    expect(worst<2e-5,"orthotropic plate factors differ from independent angular integration");
    std::cout << "orthotropic factor relative error=" << worst << '\n';
}

void coupledModesAndResidues()
{
    double worstPole=0.0,worstTrace=0.0,worstProduct=0.0,worstRadiation=0.0,worstBridge=0.0;
    for(int shape=0;shape<4;++shape)
    {
        Parameters spruce; spruce.shape=static_cast<acustra::BodyShape>(shape);
        const auto s=engine(spruce); const auto baseBridge=Access::bridge(*s);
        // The measured bank's dominant low modes are rows 0 and 1. Divide
        // out the independent global calibration before identifying K/M.
        const double scale=acustra::fittedPhysicalCalibration.bodyFrequencyScale;
        const double a0=Access::pole(spruce,0)[0]/scale,t1=Access::pole(spruce,1)[0]/scale;
        const double hole=helmholtz(shape), hole2=hole*hole;
        const double plate2=a0*a0*t1*t1/hole2;
        const double cavity2=a0*a0+t1*t1-plate2-hole2;
        expect(cavity2>0.0,"Spruce pair does not identify a positive cavity spring");
        const auto reference=solve(plate2,cavity2,hole2);
        for(int wood=1;wood<3;++wood)
        {
            const auto r=ratios(wood); const auto expected=solve(plate2*r.stiffness/r.mass,cavity2/r.mass,hole2);
            auto p=spruce;p.bodyMaterial=static_cast<Wood>(wood);const auto e=engine(p);const auto b=Access::bridge(*e);
            const double fa=Access::pole(p,0)[0]/scale, ft=Access::pole(p,1)[0]/scale;
            worstTrace=std::max(worstTrace,std::abs((fa*fa+ft*ft)/expected.trace-1.0));
            worstProduct=std::max(worstProduct,std::abs(fa*fa*ft*ft/expected.determinant-1.0));
            for(int index : {0,1,5,12,30})
            {
                const double oldFrequency=Access::pole(spruce,index)[0],newFrequency=Access::pole(p,index)[0];
                const double expectedFrequency=index<2?expected.frequency[std::size_t(index)]/reference.frequency[std::size_t(index)]:r.frequency;
                worstPole=std::max(worstPole,std::abs(newFrequency/oldFrequency/expectedFrequency-1.0));
                expect(Access::pole(p,index)[1]==Access::pole(spruce,index)[1],"Wood changed unmeasured modal Q");
                const double expectedResidue=index<2?expected.radiation[std::size_t(index)]/reference.radiation[std::size_t(index)]:r.residue;
                const auto contour=[](double f) {
                    const double bass=1.0+.28*std::exp(-f/520.0);
                    const double fitted=std::pow(f/1000.0,acustra::fittedPhysicalCalibration.residueTiltDbPerOctave/6.02059991);
                    const double mic=std::pow(10.0,(.4375*std::log2(std::clamp(f,60.0,10000.0)/1000.0))/20.0);
                    return bass*fitted*mic;
                };
                const auto actual=Access::residue(*e,index)/Access::residue(*s,index);
                const double radiationRatio=actual.real()*contour(oldFrequency)/contour(newFrequency);
                worstRadiation=std::max(worstRadiation,std::abs(radiationRatio/expectedResidue-1.0));
                expect(std::abs(actual.imag())<2e-6,"Wood rotated a fixed-mode radiation residue");
            }
            int lowCount=0,plateCount=0;
            for(std::size_t slot=0;slot<acustra::detail::steelBridgeRadiationTwins.size();++slot)
            {
                const int twin=acustra::detail::steelBridgeRadiationTwins[slot];
                if(twin<0)continue;
                const double expectedRatio=twin<2?expected.bridge[std::size_t(twin)]/reference.bridge[std::size_t(twin)]:1.0/r.mass;
                if(twin<2)++lowCount;else ++plateCount;
                const std::array<double,3> old {baseBridge.residueHeave[slot],baseBridge.residueCross[slot],baseBridge.residueRock[slot]};
                const std::array<double,3> now {b.residueHeave[slot],b.residueCross[slot],b.residueRock[slot]};
                for(int field=0;field<3;++field)
                    if(std::abs(old[std::size_t(field)])>1e-20)
                        worstBridge=std::max(worstBridge,std::abs(now[std::size_t(field)]/old[std::size_t(field)]/expectedRatio-1.0));
            }
            expect(lowCount==2 && plateCount>8,"coupled bridge residue witness lost its low or plate modes");
        }
    }
    expect(worstPole<3e-5 && worstTrace<3e-5 && worstProduct<3e-5,
           "actual Wood poles violate independently solved coupled eigenvalues");
    expect(worstRadiation<5e-5,"Wood radiation does not follow the coupled eigenvector or plate mass");
    expect(worstBridge<5e-5,"Wood bridge residues do not follow mass-normalized participation");
    std::cout<<"Wood errors pole="<<worstPole<<" trace="<<worstTrace<<" product="<<worstProduct
             <<" radiation="<<worstRadiation<<" bridge="<<worstBridge<<'\n';
}

void floorAndPassivity()
{
    double worstFloor=0.0,leastConductance=1e30,largestPole=0.0;
    for(double rate:{44100.0,48000.0,96000.0})
        for(int shape=0;shape<4;++shape)
        {
            Parameters p;p.shape=static_cast<acustra::BodyShape>(shape);const auto s=engine(p,rate);
            const auto reference=Access::bridge(*s);const auto referenceTable=Access::table(*s);
            for(int wood=0;wood<3;++wood)
            {
                p.bodyMaterial=static_cast<Wood>(wood);const auto e=engine(p,rate);const auto b=Access::bridge(*e);const auto table=Access::table(*e);const auto r=ratios(wood);
                worstFloor=std::max({worstFloor,std::abs(b.residueHeave.back()/reference.residueHeave.back()/r.floor-1.0),
                    std::abs(b.residueRock.back()/reference.residueRock.back()/r.floor-1.0),
                    std::abs(table.plateWeight/referenceTable.plateWeight/r.floor-1.0)});
                expect(b.residueCross.back()==0.0f,"Wood floor gained an unsupported cross residue");
                for(int mode=0;mode<Access::count(*e);++mode)
                    largestPole=std::max(largestPole,Access::radius(*e,mode));
                for(int i=0;i<96;++i)
                    for(int string=0;string<6;++string)
                    {
                        const float f=static_cast<float>(30.0*std::pow(16000.0/30.0,i/95.0));
                        const auto y=Access::port(*e,f,string);
                        expect(std::isfinite(y.real()) && std::isfinite(y.imag()),"Wood bridge has nonfinite mobility");
                        leastConductance=std::min(leastConductance,double(y.real()));
                    }
            }
        }
    expect(worstFloor<2e-5,"dense plate mobility ignores material stiffness or mass");
    expect(leastConductance>=-2e-6,"Wood made the bridge active");
    expect(largestPole<1.0,"Wood made a radiation pole unstable");
    std::cout<<"Wood floor error="<<worstFloor<<" least real mobility="<<leastConductance<<" largest pole="<<largestPole<<'\n';
}

std::array<std::vector<double>,2> renderWood(Parameters parameters, int note, int channel)
{
    // Matched excitation and sensor: the only changed input is Wood.
    auto e=engine(parameters);e->setStringPerChannelMode(true);e->noteOn(note,.72f,channel);
    std::array<std::vector<double>,2> audio;
    std::array<float,127> left {},right {},piezo {};
    constexpr int frames=48000;
    for(int start=0;start<frames;start+=127)
    {
        const int count=std::min(127,frames-start);
        e->process(left.data(),right.data(),{piezo.data()},count);
        for(int i=0;i<count;++i)
        {
            for(float x:{left[std::size_t(i)],right[std::size_t(i)],piezo[std::size_t(i)]})
                if(!std::isfinite(x) || std::abs(x)>=.89125094f)
                    expect(false,"ordinary Wood-only probe became nonfinite or reached the safety knee");
            audio[0].push_back(left[std::size_t(i)]);audio[0].push_back(right[std::size_t(i)]);
            audio[1].push_back(piezo[std::size_t(i)]);
        }
    }
    return audio;
}

void isolatedWoodIsMoreThanAnOutputGain()
{
    double leastDifference=1e30;
    for(int shape=0;shape<4;++shape)
        for(const auto note: {std::array<int,2>{40,1},std::array<int,2>{64,6}})
        {
            Parameters p;p.shape=static_cast<acustra::BodyShape>(shape);
            const auto spruce=renderWood(p,note[0],note[1]);
            for(int wood=1;wood<3;++wood)
            {
                p.bodyMaterial=static_cast<Wood>(wood);const auto changed=renderWood(p,note[0],note[1]);
                for(int route=0;route<2;++route)
                {
                    const auto& a=spruce[std::size_t(route)];const auto& b=changed[std::size_t(route)];
                    double aa=0.0,bb=0.0,ab=0.0;
                    for(std::size_t i=0;i<a.size();++i){aa+=a[i]*a[i];bb+=b[i]*b[i];ab+=a[i]*b[i];}
                    expect(aa>1e-8 && bb>1e-8,"Wood-only probe was silent");
                    // Best possible scalar projection removes construction
                    // compensation and all arbitrary overall gain. The
                    // residual guards a real waveform/decay change only;
                    // 1% is an inactivity guard, not an audibility claim.
                    const double relative=std::sqrt(std::max(0.0,1.0-ab*ab/(aa*bb)));
                    leastDifference=std::min(leastDifference,relative);
                }
            }
        }
    expect(leastDifference>.01,"a selectable Wood changed only gain on a microphone or piezo probe");
    std::cout<<"isolated Wood minimum gain-independent residual="<<leastDifference<<'\n';
}
}

int main()
{
    materialPropertiesAndPlateLaw();coupledModesAndResidues();floorAndPassivity();
    isolatedWoodIsMoreThanAnOutputGain();
    return failures==0?0:1;
}
