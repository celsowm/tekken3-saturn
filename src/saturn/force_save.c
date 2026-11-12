#include "force_save.h"
#include "saturn/save.h"
static const char name[]="T3FORCE";
sat_result_t t3_force_save_load(t3_force_progress_t* out){
    if(!out)return SAT_ERR_INVALID_ARG;
    *out=(t3_force_progress_t){0};
    SAT_TRY(sat_save_init());
    uint8_t data[T3_FORCE_PROGRESS_BYTES];uint32_t bytes=0;
    const sat_result_t st=sat_save_read(SAT_SAVE_INTERNAL,name,data,sizeof data,&bytes);
    if(st==SAT_ERR_NOT_FOUND)return SAT_OK;
    if(st!=SAT_OK)return st;
    return t3_force_progress_unpack(data,bytes,out)?SAT_OK:SAT_ERR_VERSION;
}
sat_result_t t3_force_save_store(const t3_force_progress_t* progress){
    uint8_t data[T3_FORCE_PROGRESS_BYTES];
    if(!t3_force_progress_pack(progress,data))return SAT_ERR_INVALID_ARG;
    SAT_TRY(sat_save_init());
    const sat_save_record_t record={name,"FORCE DATA",SAT_SAVE_ENGLISH,0};
    SAT_TRY(sat_save_write(SAT_SAVE_INTERNAL,&record,data,sizeof data,1));
    return sat_save_verify(SAT_SAVE_INTERNAL,name,data,sizeof data);
}
