#include "psx_reversal.h"
#include <stdio.h>
int main(int argc,char** argv) {
    uint8_t data[65536];int list,tag;
    if(argc!=2)return 2;
    FILE* input=fopen(argv[1],"rb");if(!input)return 2;
    const size_t size=fread(data,1,sizeof(data),input);
    if(ferror(input) || fgetc(input)!=EOF){fclose(input);return 2;}
    fclose(input);
    while(scanf("%d %d",&list,&tag)==2)
        printf("%d\n",t3_psx_reversal_list_has(data,size,(int16_t)list,(int16_t)tag));
    return ferror(stdin) || !feof(stdin)?2:0;
}
