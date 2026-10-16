/* Host differential adapter. Table is private or synthetic, never compiled in.
 * Text records: vector[3], base[9], delta[9], weight. Output: normalized vector,
 * orthonormalized base, scaled sum, orthonormalized scaled sum (30 integers). */
#include "psx_blend_math.h"
#include <stdio.h>

int main(int argc,char** argv) {
    uint8_t data[T3_PSX_RSQRT_BYTES];
    t3_psx_rsqrt_t table={data,sizeof(data)};
    FILE* file;
    if(argc!=2 || !(file=fopen(argv[1],"rb")))return 2;
    size_t got=fread(data,1,sizeof(data),file);
    int extra=fgetc(file);fclose(file);
    if(got!=sizeof(data) || extra!=EOF)return 2;
    for(;;) {
        int x[22],v[3];int status=scanf("%d",&x[0]);
        t3_psx_matrix_t base,delta,ortho,sum,blended;
        if(status==EOF)return ferror(stdin)?1:0;
        if(status!=1)return 1;
        for(unsigned i=1;i<22;++i)if(scanf("%d",&x[i])!=1)return 1;
        for(unsigned i=3;i<22;++i)if(x[i]<-32768 || x[i]>32767)return 1;
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
            base.m[i][j]=(int16_t)x[3+i*3+j];delta.m[i][j]=(int16_t)x[12+i*3+j];
        }
        int32_t vector[3]={x[0],x[1],x[2]},normal[3];
        if(!t3_psx_vector_normal(vector,&table,normal) ||
           !t3_psx_matrix_orthonormalize(&base,&table,&ortho) ||
           !t3_psx_matrix_add_scaled(&delta,&base,(int16_t)x[21],&sum) ||
           !t3_psx_matrix_orthonormalize(&sum,&table,&blended))return 1;
        for(unsigned i=0;i<3;++i)v[i]=(int)normal[i];
        printf("%d %d %d",v[0],v[1],v[2]);
        const t3_psx_matrix_t* matrices[3]={&ortho,&sum,&blended};
        for(unsigned k=0;k<3;++k)for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
            printf(" %d",matrices[k]->m[i][j]);
        putchar('\n');
    }
}
