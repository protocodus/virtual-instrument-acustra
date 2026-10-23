// Diagnostic actual-SDK oracle: both operations run from SDK-built modules.
// clang++ -std=c++17 -O2 -ffp-contract=off SdkRetuneMathFuzz.cpp -o fuzz
// fuzz baseline module export/divide/hypot/sqrt offsets probe module export/probe offsets cases
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
struct ComplexResult { double real,imag;char padding[16]; };
using Divide=ComplexResult(*)(const std::complex<double>&,const std::complex<double>&);
using Sqrt=ComplexResult(*)(const std::complex<double>&);
using Hypot=double(*)(double,double);
using Probe=void(*)(const double*,double*);
uint64_t bits(double v){uint64_t w;std::memcpy(&w,&v,8);return w;}
double value(uint64_t w){double v;std::memcpy(&v,&w,8);return v;}
char* load(const char*path,const char*exportOffset){void*h=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!h){puts(dlerror());std::exit(2);}auto*p=static_cast<char*>(dlsym(h,"JukeboxExport_RenderRealtime"));return p-std::strtoull(exportOffset,nullptr,16);}
template<class T>T function(char*base,const char*offset){void*p=base+std::strtoull(offset,nullptr,16);T f;std::memcpy(&f,&p,sizeof f);return f;}
double randomDouble(std::mt19937_64&r){switch(r()%4){case 0:return value(r());case 1:return std::ldexp(double(int64_t(r()>>11))*0x1p-51-2.,int(r()%80)-40);case 2:return std::ldexp(double(int64_t(r()>>11))*0x1p-51-2.,int(r()%2100)-1050);default:return double(int64_t(r()))*0x1p-40;}}
int main(int argc,char**argv){if(argc<10){puts("baseline export divide hypot sqrt candidate export probe random-cases");return 2;}
 char*base=load(argv[1],argv[2]);const auto divide=function<Divide>(base,argv[3]);const auto hypot=function<Hypot>(base,argv[4]);const auto sqrt=function<Sqrt>(base,argv[5]);char*candidate=load(argv[6],argv[7]);const auto probe=function<Probe>(candidate,argv[8]);long cases=std::atol(argv[9]);
 std::vector<double> edge{0.,-0.,1.,-1.,2.,-2.,0.5,-0.5,1e-9,-1e-9,1e-10,-1e-10,std::numeric_limits<double>::min(),-std::numeric_limits<double>::min(),std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),INFINITY,-INFINITY,value(0x7ff8000000001234ull),value(0xfff8000000005678ull)};
 for(double t:{1e-9,1e-10,std::numeric_limits<double>::min(),1.,2.})for(double dir:{0.,double(INFINITY),-double(INFINITY)}){edge.push_back(std::nextafter(t,dir));edge.push_back(-std::nextafter(t,dir));}
 for(int exponent=-1074;exponent<1024;exponent+=7){edge.push_back(std::ldexp(1.,exponent));edge.push_back(-std::ldexp(1.,exponent));}
 long totalBad=0;for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){std::fesetround(mode);std::mt19937_64 rng(20261023);long tested=0,bad[3]{},roundBad=0;
 const auto check=[&](double a,double b,double c,double d){const std::complex<double>z(a,b),w(c,d);const double in[]{a,b,c,d};double out[6]{};probe(in,out);if(argc>10 && tested==0)out[0]=value(bits(out[0])^1);const auto quotient=divide(z,w);const auto root=sqrt(z);const double expected[]{quotient.real,quotient.imag,hypot(a,b),root.real,root.imag};for(int i=0;i<5;++i){if(bits(expected[i])!=bits(out[i])){const int category=i<2?0:i==2?1:2;if(++bad[category]<4)printf("BAD mode%d item%d input(%a,%a)/(%a,%a) got%a expected%a bits%llx/%llx\n",mode,i,a,b,c,d,out[i],expected[i],(unsigned long long)bits(out[i]),(unsigned long long)bits(expected[i]));}}
 const int expectedMode=mode==FE_TONEAREST?1:mode==FE_UPWARD?2:mode==FE_DOWNWARD?3:0;if(out[5]!=expectedMode)++roundBad;++tested;};
 for(double a:edge)for(double b:edge){check(a,b,1.,-1.);check(1.,-1.,a,b);}
 for(long i=0;i<cases;++i)check(randomDouble(rng),randomDouble(rng),randomDouble(rng),randomDouble(rng));
 printf("{\"rounding\":%d,\"cases\":%ld,\"division_bit_mismatches\":%ld,\"hypot_bit_mismatches\":%ld,\"complex_sqrt_bit_mismatches\":%ld,\"mode_query_mismatches\":%ld}\n",mode,tested,bad[0],bad[1],bad[2],roundBad);fflush(stdout);totalBad+=bad[0]+bad[1]+bad[2]+roundBad;
 }return totalBad?1:0;}
