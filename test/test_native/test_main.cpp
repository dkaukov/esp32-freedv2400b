// SPDX-License-Identifier: GPL-3.0-only
#include <unity.h>
#include <FreeDv2400b.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace freedv2400b;
static const uint8_t GOLDEN_PAYLOAD[7] = {0x11,0x22,0x33,0x44,0x55,0x66,0x70};
static const char *EXPECTED[] = {
 "a3156e07b505c0","00a387b19f4270","81ac5b5f9e4550","1b50739da51200","2b4a2a47c211a0","bf3c46d9c6b150",
 "ee3fed3ff52b70","633de8ed69fe70","d539abb7c54bf0","43c1cac19590f0","9df9d55fc246f0","b58b9dfd080a10",
 "569276779c43d0","d9d52cf925cea0","c46d4dbfe79e70","8aa9ee4de8abd0","cf0d8e67d63520","f38fa8712c1170",
 "0689305f634f50","ab4b5edd46abe0","f5aa2fa7ea69e0","ead11299b5bc00","f2ab1e3fe05370","0004020d2e8a50"};
static std::vector<int16_t> wav() {
 FILE *f=fopen("test/fixtures/modem_2400b_short.wav","rb"); TEST_ASSERT_NOT_NULL(f); fseek(f,44,SEEK_SET);
 std::vector<int16_t> v(48000); TEST_ASSERT_EQUAL(48000,fread(v.data(),2,v.size(),f)); fclose(f); return v;
}
static std::vector<std::string> got;
static void frameCb(const uint8_t *p,size_t n,const FreeDv2400bDecodeResult&r){
 if(r.frameType==FreeDv2400bFrameType::VOICE){char s[15];for(size_t i=0;i<n;i++)snprintf(s+2*i,3,"%02x",p[i]);got.push_back(s);}
}
static void assertGolden(const std::vector<int16_t>&v,const int *chunks=0,size_t nc=0){
 got.clear(); FreeDv2400bDemodulator d(frameCb); size_t p=0,k=0; while(p<v.size()){size_t n=chunks?chunks[k++%nc]:v.size()-p;if(n>v.size()-p)n=v.size()-p;d.processSamples(v.data()+p,n);p+=n;}
 TEST_ASSERT_EQUAL(24,got.size()); for(size_t i=0;i<got.size();i++)TEST_ASSERT_EQUAL_STRING(EXPECTED[i],got[i].c_str());
}
void test_framing_and_exact_tx(){
 uint8_t bits[96],packed[12]={0}; detail::VhfTypeAFramer::frame(GOLDEN_PAYLOAD,bits); for(int i=0;i<96;i++)packed[i>>3]|=bits[i]<<(7-(i&7));
 const uint8_t exp[12]={0xa7,0xa7,0x11,0x22,0x33,0x67,0xad,0x44,0x55,0x66,0x72,0x72}; TEST_ASSERT_EQUAL_UINT8_ARRAY(exp,packed,12);
 std::vector<int16_t> v=wav(); int16_t out[1920]; uint8_t p[7]={0xa3,0x15,0x6e,0x07,0xb5,0x05,0xc0}; FreeDv2400bEncoder e; TEST_ASSERT_TRUE(e.encode(p,7,out)); TEST_ASSERT_EQUAL_INT16_ARRAY(v.data(),out,1920);
 p[6]|=15; int16_t alt[1920]; e.encode(p,7,alt); TEST_ASSERT_EQUAL_INT16_ARRAY(out,alt,1920);
}
void test_golden_inverted_chunking_reset(){
 std::vector<int16_t> v=wav(); assertGolden(v); for(size_t i=0;i<v.size();i++)v[i]=-v[i]; const int c[]={1,5,10,159,160,1915,1920,1925}; assertGolden(v,c,8);
 FreeDv2400bDemodulator d(frameCb); d.processSamples(v.data(),v.size()); d.reset(); got.clear(); d.processSamples(v.data(),v.size()); TEST_ASSERT_EQUAL(24,got.size());
}
void test_degraded(){
 std::vector<int16_t> in=wav(),v(in.size()); for(size_t i=0;i<v.size();i++)v[i]=in[i]/2; assertGolden(v);
 for(size_t i=0;i<v.size();i++){int x=in[i]*3/2;v[i]=x>32767?32767:(x<-32768?-32768:x);}assertGolden(v);
 for(size_t i=0;i<v.size();i++){int x=in[i]+2000;v[i]=x>32767?32767:(x<-32768?-32768:x);}assertGolden(v);
 uint32_t state=0x13579bdf;for(size_t i=0;i<v.size();i++){state=state*1664525+1013904223;int n=(int)(((uint64_t)state*1201)>>32)-600;int x=in[i]+n;v[i]=x>32767?32767:(x<-32768?-32768:x);}assertGolden(v);
 v[0]=in[0];for(size_t i=1;i+1<v.size();i++)v[i]=(in[i-1]+2*(int)in[i]+in[i+1])/4;v.back()=in.back();assertGolden(v);
}
static std::vector<int16_t> resample(const std::vector<int16_t>&in,int ppm){
 double ratio=1.0+ppm/1000000.0;size_t n=(size_t)(in.size()*ratio);std::vector<int16_t> out(n);
 for(size_t i=0;i<n;i++){double x=i/ratio;size_t a=(size_t)x;if(a+1>=in.size()){out[i]=in.back();continue;}double f=x-a;out[i]=(int16_t)(in[a]+(in[a+1]-in[a])*f);}
 return out;
}
static void assertClock(const std::vector<int16_t>&v){
 got.clear();FreeDv2400bDemodulator d(frameCb);d.processSamples(v.data(),v.size());
 TEST_ASSERT_GREATER_OR_EQUAL(23,got.size());for(size_t i=0;i<got.size();i++)TEST_ASSERT_EQUAL_STRING(EXPECTED[i],got[i].c_str());
}
void test_sample_clock_error(){std::vector<int16_t> in=wav();assertClock(resample(in,200));assertClock(resample(in,-200));}
void test_data_no_stale(){
 uint8_t b[96]; detail::VhfTypeAFramer::frame(GOLDEN_PAYLOAD,b);for(int i=0;i<16;i++)b[40+i]=(0xf1fc>>(15-i))&1;
 detail::VhfTypeADeframer d;uint8_t out[7];memset(out,0x5a,7);TEST_ASSERT_TRUE(d.accept(b,out));TEST_ASSERT_EQUAL((int)FreeDv2400bFrameType::DATA,(int)d.frameType());for(int i=0;i<7;i++)TEST_ASSERT_EQUAL_HEX8(0x5a,out[i]);
}
int main(int,char**){UNITY_BEGIN();RUN_TEST(test_framing_and_exact_tx);RUN_TEST(test_golden_inverted_chunking_reset);RUN_TEST(test_degraded);RUN_TEST(test_sample_clock_error);RUN_TEST(test_data_no_stale);return UNITY_END();}
