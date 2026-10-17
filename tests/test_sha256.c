#include "sha256.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static void expect(t3_sha256_t* s,const char* hex) {
    uint8_t out[32];t3_sha256_finish(s,out);
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;++i){assert(digits[out[i]>>4]==hex[2*i]);assert(digits[out[i]&15]==hex[2*i+1]);}
}
int main(void) {
    t3_sha256_t s;t3_sha256_init(&s);t3_sha256_update(&s,0,0);
    expect(&s,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    t3_sha256_init(&s);t3_sha256_update(&s,"abc",3);
    expect(&s,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* text="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for(unsigned chunk=1;chunk<=56;++chunk) {
        t3_sha256_init(&s);
        for(unsigned at=0;at<56;) {unsigned n=56-at;if(n>chunk)n=chunk;t3_sha256_update(&s,text+at,n);at+=n;}
        expect(&s,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    }
    t3_sha256_init(&s);char a[1000];memset(a,'a',sizeof(a));
    for(unsigned i=0;i<1000;++i)t3_sha256_update(&s,a,sizeof(a));
    expect(&s,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    return 0;
}
