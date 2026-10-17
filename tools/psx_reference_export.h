/* Private host-only export. Read coverage is collected during the one seed;
 * no expected exit ranges are exported to the Saturn executable. */
static uint8_t reference_seed_reads[0x200000];
static int reference_record_reads;
static const char* reference_output;
static FILE *reference_hashes,*reference_inputs;
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
    return ok && reference_hashes && reference_inputs;
}
