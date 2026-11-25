#include "iskra_lantern.h"
#include "iskra_shadows.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int Width = 192;
constexpr int Height = 480;
using Image = std::vector<unsigned char>;
Image flat() { return Image(size_t(Width) * Height * 3, 120); }
int red(const Image& frame,int x,int y) { return frame[(size_t(y)*Width+x)*3]; }
void check(bool pass,const char* message) { if(!pass) throw std::runtime_error(message); }
}
int main() {
    try {
        iskra::ShadowGround ground(3);
        // The left ledge ends at screen x=29. The next column is an empty drop.
        ground.record(0,4,0x54); ground.record(1,4,0x54);
        ground.record(1,5,0x54); // Interior face of the same ledge.
        ground.record(2,4,0xc2); // Collectible hovering over the pit.
        auto onGround=flat();
        ground.project(onGround,Width,28,96,16,false,false,0);
        check(red(onGround,108,388)<80,"missing contact shadow on ledge");
        check(red(onGround,118,388)==120,"shadow crossed the edge of a pit");
        check(red(onGround,108,384-1)==120,"shadow in empty air");
        auto inAir=flat(), far=flat();
        ground.project(inAir,Width,28,73,16,true,false,0);
        check(red(inAir,108,388)>red(onGround,108,388) && red(inAir,108,388)<120,
              "airborne shadow should fade on actual support");
        ground.project(far,Width,22,36,16,true,false,0);
        check(far==flat(),"distant actor shaded the ground");
        // The wooden deck is eight native pixels wide, with a stone floor below it.
        iskra::ShadowGround lift;
        lift.record(0,4,0x54);
        lift.record(1,4,0x54);
        lift.recordPlatform(6,14,90,90*4+3);
        lift.recordPlatform(5,14,83,83*4+3);
        auto overLift=flat();
        lift.project(overLift,Width,17,82,16,false,false,0);
        const auto* mask=lift.maskFor(5);
        check(mask && *std::max_element(mask,mask+32*32)>90,"no contact on upper moving deck");
        check(!lift.maskFor(6),"shadow passed through first deck");
        check(red(overLift,17*4,388)==120,"floor shaded through wooden platform");
        check(red(overLift,13*4,388)<120,"visible stone beside the deck lost its shadow");
        lift.reset(0);
        check(!lift.maskFor(5) && !lift.maskFor(6),"moving deck retained old frame mask");
        lift.recordPlatform(2,-6,83,83*4+3);
        auto leftEdge=flat();
        lift.project(leftEdge,Width,0,83,14,false,false,0);
        check(lift.maskFor(2)!=nullptr && leftEdge==flat(),"partial platform at left screen edge");
        // Negative top-left coordinates occur when a brush crosses the camera edge.
        iskra::LanternLight lamp;
        lamp.place(0,0,2,0);
        unsigned char row[]={100,100,100,100,100,100,100,100,100};
        lamp.illuminate(row,0,3,0);
        check(row[0]>row[1] && row[1]>row[2] && row[2]>100,"lantern reflection missing");
        check(!lamp.intersects(160,110,64,64),"distant brush entered light cone");
        auto noSurface=flat();
        iskra::ShadowGround pit(0);
        pit.project(noSurface,Width,0,90,16,true,true,0);
        check(noSurface==flat(),"shadow in pit");
        std::cout<<"Light falloff, shadow receivers and clipping passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
