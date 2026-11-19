#include "ball_save.h"
static const char name[]="T3BALL";
sat_result_t t3_ball_save_load(t3_ball_choice_t* out,void* workspace,unsigned bytes){
    if(!out)return SAT_ERR_INVALID_ARG;
    SAT_TRY(sat_save_init_in(workspace,bytes));
    uint8_t data[T3_BALL_PROGRESS_BYTES];uint32_t read=0;
    const sat_result_t st=sat_save_read(SAT_SAVE_INTERNAL,name,data,sizeof data,&read);
    if(st==SAT_ERR_NOT_FOUND)return SAT_OK;
    if(st!=SAT_OK)return st;
    return t3_ball_progress_unpack(data,read,out)?SAT_OK:SAT_ERR_VERSION;
}
sat_result_t t3_ball_save_store(const t3_ball_choice_t* choice,void* workspace,unsigned bytes){
    uint8_t data[T3_BALL_PROGRESS_BYTES];
    if(!t3_ball_progress_pack(choice,data))return SAT_ERR_INVALID_ARG;
    SAT_TRY(sat_save_init_in(workspace,bytes));
    const sat_save_record_t record={name,"BALL DATA",SAT_SAVE_ENGLISH,0};
    SAT_TRY(sat_save_write(SAT_SAVE_INTERNAL,&record,data,sizeof data,1));
    return sat_save_verify(SAT_SAVE_INTERNAL,name,data,sizeof data);
}
