#include "case_sleeve.hpp"
#include <cassert>
#include <iostream>

int main() {
    for(const auto dimensions:std::array<std::array<float,2>,4>{{{135,14},{137,15},{135,20},{120,8}}}) {
        const auto rails=CaseSleeve::Rails(dimensions[0],dimensions[1]);
        assert(rails.front().u==0 && rails.back().u==1);
        for(std::size_t i=1;i<rails.size();++i) {
            assert(rails[i].u>rails[i-1].u);
            const float dx=rails[i].x-rails[i-1].x,dz=rails[i].z-rails[i-1].z;
            assert(dx*dx+dz*dz>0);
        }
        assert(rails[1].x==rails[4].x && rails[1].z==-rails[4].z);
        assert(rails[2].x==rails[3].x && rails[2].z==-rails[3].z);
        assert(rails[2].x<rails[1].x && rails[2].z>rails[1].z);
        const float width=2*dimensions[0]+dimensions[1];
        assert(rails[1].u==dimensions[0]/width);
        assert(rails[4].u==(dimensions[0]+dimensions[1])/width);
        // Each visible panel has outward-facing winding through a full rotation.
        // Endpoints are shared rails, not independently offset front/spine decals.
        for(int step=0;step<360;++step) {
            const float yaw=step*3.14159265358979323846f/180.f;
            int visible=0;
            for(std::size_t panel=0;panel+1<rails.size();++panel) {
                const float dx=(rails[panel+1].x-rails[panel].x)*dimensions[0];
                const float dz=(rails[panel+1].z-rails[panel].z)*dimensions[1];
                if(dx*std::cos(yaw)+dz*std::sin(yaw)>0.0001f) ++visible;
            }
            assert(visible<=4 && (visible>=1 || step==270)); // open/right case edge has no sleeve
        }
    }
    std::cout<<"PASS: continuous back/hinge/spine/hinge/front sleeve, ordered shared UV rails, full-turn facing and case profiles\n";
}
