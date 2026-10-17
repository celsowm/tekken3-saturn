/* Strict reader for private compact CPU snapshots; no guest execution. */
#ifndef T3_RAM_RANGES_H
#define T3_RAM_RANGES_H
static uint8_t ram[0x200000];
static struct {uint32_t address,size;} ranges[64];
static unsigned range_count;
static int bad;
static uint32_t load32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static const uint8_t* at(uint32_t address,unsigned size) {
    for(unsigned i=0;i<range_count;++i) {
        if(address>=ranges[i].address && size<=ranges[i].size &&
           address-ranges[i].address<=ranges[i].size-size)return ram+(address-0x80000000u);
    }
    bad=1;return ram;
}
static uint8_t u8(uint32_t a) {return *at(a,1);}
static uint16_t u16(uint32_t a) {const uint8_t* p=at(a,2);return (uint16_t)(p[0]|(p[1]<<8));}
static int16_t i16(uint32_t a) {uint16_t u=u16(a);return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);}
static uint32_t u32(uint32_t a) {return load32(at(a,4));}
static int32_t i32(uint32_t a) {uint32_t u=u32(a);return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);}
static int read_blob(const char* path,void* bytes,size_t size) {
    FILE* f=fopen(path,"rb");int ok;if(!f)return 0;
    ok=fread(bytes,1,size,f)==size && fgetc(f)==EOF;fclose(f);return ok;
}
static inline int snapshot(const char* path) {
    uint8_t header[12],entry[8];FILE* f=fopen(path,"rb");
    if(!f)return 0;
    if(fread(header,1,12,f)!=12 || memcmp(header,"T3RANGES",8)!=0)goto fail;
    range_count=load32(header+8);if(range_count>64 || !range_count)goto fail;
    for(unsigned i=0;i<range_count;++i) {
        uint32_t a,n;
        if(fread(entry,1,8,f)!=8)goto fail;
        a=load32(entry);n=load32(entry+4);
        if(a<0x80000000u || n>sizeof(ram) || a-0x80000000u>sizeof(ram)-n)goto fail;
        ranges[i].address=a;ranges[i].size=n;
        if(fread(ram+(a-0x80000000u),1,n,f)!=n)goto fail;
    }
    if(fgetc(f)!=EOF)goto fail;
    fclose(f);bad=0;return 1;
fail:fclose(f);return 0;
}

static inline int snapshot_full(const char* path) {
    if(!read_blob(path,ram,sizeof(ram)))return 0;
    range_count=1;ranges[0].address=0x80000000u;ranges[0].size=sizeof(ram);bad=0;return 1;
}
#endif
