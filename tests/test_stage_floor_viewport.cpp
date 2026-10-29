/* Host check of the SDK's virtual-height floor parameters against rays in
 * the mapped PS1 viewport. Uses only synthetic camera and floor data. */
#include "saturn/vdp2_floor.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <initializer_list>

static int32_t fx(double value) { return static_cast<int32_t>(std::lround(value*65536.0)); }
static double coefficient(uint32_t word) { return static_cast<int32_t>(word<<8)>>8; }

int main() {
    double worst=0;unsigned checked=0;
    for(double pitch : {0.15,0.3,0.5}) for(double yaw : {-0.7,0.0,0.9}) {
        const double fxn=2.7,fyn=3.5,cy=std::cos(yaw),sy=std::sin(yaw);
        const double cp=std::cos(pitch),sp=std::sin(pitch);
        const double right[3]={cy,0,-sy},up[3]={sy*sp,cp,cy*sp},forward[3]={sy*cp,-sp,cy*cp};
        sat_mat4_t m={};
        for(int i=0;i<3;++i) {m.m[i]=fx(fxn*right[i]);m.m[4+i]=fx(fyn*up[i]);m.m[12+i]=fx(forward[i]);}
        sat_mat4_t virtual_view=m;
        for(int i=4;i<8;++i)virtual_view.m[i]=static_cast<int32_t>(static_cast<int64_t>(m.m[i])*20/19);
        const sat_vec3_t eye={fx(0.4),fx(2.0),fx(-1.0)};
        sat_fx16_t far_values[228];uint32_t alias[228],separate[228];
        for(unsigned i=0;i<228;++i)far_values[i]=i%7?fx(15.0):0;
        for(unsigned i=0;i<228;++i)alias[i]=static_cast<uint32_t>(far_values[i]);
        sat_vdp2_floor_t floor={320,228,0,fx(1.0),0,0,0,far_values,1};
        sat_vdp2_rotation_params_t p={};uint32_t k[228]={};
        if(sat_vdp2_floor_from_view(&floor,&virtual_view,&eye,fx(0.25),fx(40.0),&p,k)!=SAT_OK)return 1;
        floor.far_lines=reinterpret_cast<sat_fx16_t*>(alias);
        sat_vdp2_rotation_params_t alias_params={};
        if(sat_vdp2_floor_from_view(&floor,&virtual_view,&eye,fx(0.25),fx(40.0),&alias_params,alias)!=SAT_OK)return 1;
        for(unsigned i=0;i<228;++i)if(alias[i]!=k[i])return 1;
        /* With no far cut, verify full-screen geometry against analytic rays. */
        floor.far_lines=nullptr;
        if(sat_vdp2_floor_from_view(&floor,&virtual_view,&eye,fx(0.25),fx(40.0),&p,separate)!=SAT_OK)return 1;
        for(unsigned i=0;i<228;++i)k[i]=separate[i];
        p.xst-=7*65536;p.yst+=4*p.dyst;
        for(int y=0;y<224;y+=3) {
            if(k[y+4]&0x80000000u)continue;
            for(int x=16;x<304;x+=11) {
                const double nx=(x-167)/160.0,ny=(110-y)/112.0;
                const double fy_effective=fyn*15.0/14.0;
                const double dx=forward[0]+nx/fxn*right[0]+ny/fy_effective*up[0];
                const double dy=forward[1]+nx/fxn*right[1]+ny/fy_effective*up[1];
                const double dz=forward[2]+nx/fxn*right[2]+ny/fy_effective*up[2];
                if(dy>=0)continue;
                const double t=2.0/-dy,ex=0.4+t*dx,ez=-1.0+t*dz;
                const double xs=(p.xst+x*p.dx)/65536.0,ys=(p.yst+y*p.dyst)/65536.0;
                const double cv=coefficient(k[y+4])/65536.0;
                const double ax=cv*(p.matrix[0]*xs+p.matrix[1]*ys)/65536.0+p.mx/65536.0;
                const double az=cv*(p.matrix[3]*xs+p.matrix[4]*ys)/65536.0+p.my/65536.0;
                double err=std::hypot(ax-ex,az-ez);if(err>worst)worst=err;++checked;
            }
        }
    }
    std::printf("floor viewport: %u points, maximum error %.5f map units\n",checked,worst);
    return checked<1000 || worst>0.15;
}
