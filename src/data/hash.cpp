#include "data/hash.h"
#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <limits>
#endif
namespace mtv { namespace {
#ifdef __APPLE__
struct Sha {
 CC_SHA256_CTX context;Sha(){CC_SHA256_Init(&context);}
 void add(ByteView bytes){while(!bytes.empty()){auto count=CC_LONG(std::min(bytes.size(),size_t(std::numeric_limits<CC_LONG>::max())));CC_SHA256_Update(&context,bytes.data(),count);bytes=bytes.subspan(count);}}
 std::string finish(){std::array<unsigned char,CC_SHA256_DIGEST_LENGTH> digest{};CC_SHA256_Final(digest.data(),&context);std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto v:digest)out<<std::setw(2)<<unsigned(v);return out.str();}
};
#else
constexpr uint32_t K[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
struct Sha {
 std::array<uint32_t,8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};std::array<uint8_t,64> block{};size_t used{};uint64_t length{};
 void compress(){uint32_t w[64]{};for(int i=0;i<16;++i)for(int j=0;j<4;++j)w[i]=(w[i]<<8)|block[i*4+j];for(int i=16;i<64;++i){auto a=w[i-15],b=w[i-2];w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10));}auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];for(int i=0;i<64;++i){auto t=v+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+K[i]+w[i];auto u=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;}
 void add(ByteView bytes){length+=bytes.size();for(auto b:bytes){block[used++]=std::to_integer<uint8_t>(b);if(used==64){compress();used=0;}}}
 std::string finish(){auto bits=length*8;block[used++]=128;if(used>56){while(used<64)block[used++]=0;compress();used=0;}while(used<56)block[used++]=0;for(int i=7;i>=0;--i)block[used++]=uint8_t(bits>>(i*8));compress();std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto v:h)out<<std::setw(8)<<v;return out.str();}
};
#endif
}
std::string sha256(ByteView bytes){Sha s;s.add(bytes);return s.finish();}
Result<std::string> sha256_file(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);if(!f)return DataError{path.string(),"",0,"cannot open hash source"};Sha s;std::array<char,65536> b;while(f.read(b.data(),b.size())||f.gcount())s.add(std::as_bytes(std::span(b.data(),size_t(f.gcount()))));if(!f.eof())return DataError{path.string(),"",0,"hash read failed"};return s.finish();}
}
