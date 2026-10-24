/* Private host-only export. Read coverage is collected during the one seed;
 * no expected exit ranges are exported to the Saturn executable. */
static uint8_t reference_seed_reads[0x200000];
static int reference_record_reads;
static const char* reference_output;
static FILE *reference_hashes,*reference_inputs;
static FILE *reference_views;
static uint32_t reference_view_count;
static uint8_t* reference_screens;
static size_t reference_screen_bytes,reference_screen_capacity;
static FILE *reference_debug_values;
static int reference_debug_done;
static t3_sha256_t reference_digest;
static uint32_t reference_value_count;
static void reference_read(uint32_t address,unsigned n) {
    if(reference_record_reads && address>=0x80000000u &&
       address-0x80000000u<=sizeof(reference_seed_reads)-n)
        memset(reference_seed_reads+address-0x80000000u,1,n);
}
static void reference_value(int64_t value) {
    uint8_t bytes[8];uint64_t bits=(uint64_t)value;
    for(unsigned k=0;k<8;++k)bytes[k]=(uint8_t)(bits>>(56-8*k));
    if(reference_debug_values && !reference_debug_done)fwrite(bytes,1,8,reference_debug_values);
    t3_sha256_update(&reference_digest,bytes,8);++reference_value_count;
}
static FILE* reference_file(const char* name) {
    char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",reference_output,name);
    return n>0 && (size_t)n<sizeof(path)?fopen(path,"wb"):NULL;
}
static int reference_write32(FILE* f,uint32_t n) {
    uint8_t b[4]={(uint8_t)n,(uint8_t)(n>>8),(uint8_t)(n>>16),(uint8_t)(n>>24)};
    return fwrite(b,1,4,f)==4;
}
/* Fighter draw state the original leaves for the next tick. The file is
 * `T3VIEWS\0`, u32 sparse count, that many 38-byte view records (u32 tick,
 * 3x3 int16 matrix, 3x int32 translation, int32 H), then u32 screen count
 * and that many int16 screen-x pairs (+0x1278) in tick order.
 * Only ticks whose RoundFlow draw consumed the view get a record; the screen
 * x is exported for every tick because PairwiseDistances reads the previous
 * CameraFrame projection every tick. */
static int reference_write_view(uint32_t tick,const t3_psx_camera_matrix_t* view) {
    uint8_t b[38];unsigned at=0;
    for(unsigned j=0;j<4;++j)b[at++]=(uint8_t)(tick>>(8u*j));
    for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k) {
        uint16_t v=(uint16_t)view->m[r][k];b[at++]=(uint8_t)v;b[at++]=(uint8_t)(v>>8);
    }
    for(unsigned k=0;k<3;++k)for(unsigned j=0;j<4;++j)
        b[at++]=(uint8_t)((uint32_t)view->t[k]>>(8u*j));
    for(unsigned j=0;j<4;++j)b[at++]=(uint8_t)((uint32_t)view->h>>(8u*j));
    return fwrite(b,1,sizeof(b),reference_views)==sizeof(b);
}
static int reference_add_screen(int16_t x0,int16_t x1) {
    if(reference_screen_bytes+4>reference_screen_capacity) {
        size_t want=reference_screen_capacity?2*reference_screen_capacity:8192;
        uint8_t* grown=realloc(reference_screens,want);
        if(!grown)return 0;
        reference_screens=grown;reference_screen_capacity=want;
    }
    uint8_t* b=reference_screens+reference_screen_bytes;
    uint16_t v=(uint16_t)x0;b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);
    v=(uint16_t)x1;b[2]=(uint8_t)v;b[3]=(uint8_t)(v>>8);
    reference_screen_bytes+=4;return 1;
}
static int reference_close_views(void) {
    FILE* f=reference_views;reference_views=NULL;
    int ok=1;
    if(f) {
        ok=!fseek(f,8,SEEK_SET) && reference_write32(f,reference_view_count) &&
           !fseek(f,0,SEEK_END) && reference_write32(f,(uint32_t)(reference_screen_bytes/4)) &&
           fwrite(reference_screens,1,reference_screen_bytes,f)==reference_screen_bytes;
        if(fclose(f))ok=0;
    }
    free(reference_screens);reference_screens=NULL;
    reference_screen_bytes=reference_screen_capacity=0;
    return ok;
}
static int reference_export_seed(const uint8_t* bytes,uint32_t sx,uint32_t sz,
                                 uint32_t previous,uint32_t ramp) {
    struct {uint32_t at,size;} spans[64];unsigned count=0;
    for(unsigned gap=0;gap<=256;gap+=8) {
        count=0;
        for(uint32_t at=0;at<sizeof(reference_seed_reads);) {
            if(!reference_seed_reads[at]){++at;continue;}
            uint32_t start=at,last=at++;
            while(at<sizeof(reference_seed_reads) && at-last<=gap+1) {
                if(reference_seed_reads[at])last=at;
                ++at;
            }
            if(count<64){spans[count].at=start;spans[count].size=last-start+1;}
            ++count;
        }
        if(count<=64)break;
    }
    if(!count || count>64)return 0;
    FILE* f=reference_file("seed.ranges");if(!f)return 0;
    int ok=fwrite("T3RANGES",1,8,f)==8 && reference_write32(f,count);
    for(unsigned i=0;i<count && ok;++i)
        ok=reference_write32(f,0x80000000u+spans[i].at) && reference_write32(f,spans[i].size) &&
           fwrite(bytes+spans[i].at,1,spans[i].size,f)==spans[i].size;
    if(fclose(f))ok=0;
    f=reference_file("seed-addresses.json");if(!f)return 0;
    if(fprintf(f,"{\"step_x\":%u,\"step_z\":%u,\"previous\":%u,\"ramp\":%u}\n",sx,sz,previous,ramp)<0)ok=0;
    if(fclose(f))ok=0;
    reference_hashes=reference_file("expected.sha256");reference_inputs=reference_file("inputs.tsv");
    /* Optional per-tick fighter view; T3_REPLAY_NO_VIEW keeps old behavior. */
    if(!getenv("T3_REPLAY_NO_VIEW"))reference_views=reference_file("views.bin");
    if(reference_views && fwrite("T3VIEWS\0\0\0\0\0",1,12,reference_views)!=12)ok=0;
    return ok && reference_hashes && reference_inputs &&
        (getenv("T3_REPLAY_NO_VIEW") || reference_views);
}
