#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace iskra {
class ShadowGround {
public:
    explicit ShadowGround(int cameraX = 0) { reset(cameraX); }

    void reset(int cameraX) {
        offset = cameraX & 15;
        tops.fill(0);
        platforms.clear();
        platformForSlot.fill(-1);
    }

    void record(unsigned column, unsigned row, unsigned material) {
        if (column >= tops.size() || row >= 13) return;
        const bool receives = (material >= 0x10 && material <= 0x21) ||
            (material >= 0x51 && material <= 0x5e) ||
            (material >= 0x61 && material <= 0x69) || material == 0x6b || material == 0x6c ||
            material == 0x88 || material == 0x89 || material == 0xc0 || material == 0xc1 || material == 0xc4;
        if (receives) tops[column] |= std::uint16_t(1u << row);
    }

    // x/y are the latched 8x8 sprite cell's native screen coordinates. The
    // painted top of the deck (topPx) may be a fraction of a native pixel below y.
    void recordPlatform(unsigned slot, int x, int y, int topPx) {
        if (slot >= platformForSlot.size() || platformForSlot[slot] >= 0 ||
            topPx < y * Scale || topPx >= (y + 8) * Scale) return;
        platformForSlot[slot] = static_cast<int>(platforms.size());
        platforms.emplace_back(x, y, topPx);
    }

    const unsigned char* maskFor(unsigned slot) const {
        if (slot >= platformForSlot.size() || platformForSlot[slot] < 0) return nullptr;
        const auto& platform = platforms[platformForSlot[slot]];
        return platform.shaded ? platform.mask.data() : nullptr;
    }

    void project(std::vector<unsigned char>& image, int width, float center, float feet,
                 float bodyWidth, bool airborne, bool underwater, float lightDirection) {
        if (width <= 0 || bodyWidth <= 0 || feet >= 240 || feet < -32) return;
        const float halfBody = std::clamp(.53f * bodyWidth, 4.f, 13.f);
        const int left = std::max(0, int(std::floor(center - halfBody - 14)));
        const int right = std::min(width / Scale, int(std::ceil(center + halfBody + 14)));
        const int height = static_cast<int>(image.size() / (size_t(width)*3));
        // Split the footprint where its receiver changes. Even half a moving
        // deck can occlude the floor below while the other half overhangs a gap.
        for (int x=left; x<right;) {
            const Receiver face=receiverAt(x, feet);
            int end=x+1;
            while (end<right && receiverAt(end,feet)==face) ++end;
            if (face.top < height && face.top / float(Scale) - feet < Reach) {
                const float lift = std::max(0.f, face.top / float(Scale) - feet) / Reach;
                const float cx = center + lightDirection * (1.f + 7.f*lift);
                const float rx = halfBody + lift*3.8f, ry = 2.45f + lift*1.30f;
                const float strength = (underwater ? .39f : .63f) * (1.f-lift) * (airborne ? .93f : 1.f);
                const int startX = std::max(x*Scale, int(std::floor((cx-rx)*Scale)));
                const int endX = std::min(end*Scale, int(std::ceil((cx+rx)*Scale)));
                auto* platform = face.platform >= 0 ? &platforms[face.platform] : nullptr;
                int endY = std::min(height, face.top + int(std::ceil((1.f+ry)*Scale)));
                if (platform) endY=std::min(endY,(platform->y+8)*Scale);
                for (int py=std::max(0,face.top); py<endY; ++py) {
                    const float dy = ((py-face.top+.5f)/Scale-1.f)/ry;
                    for (int px=startX; px<endX; ++px) {
                        const float dx=((px+.5f)/Scale-cx)/rx;
                        const float falloff=1.f-dx*dx-dy*dy;
                        if (falloff<=0.f) continue;
                        const int shade=int(255*strength*falloff*falloff);
                        if (platform) {
                            auto& value=platform->mask[(py-platform->y*Scale)*32 + px-platform->x*Scale];
                            value += static_cast<unsigned char>(((255-value)*shade+127)/255);
                            platform->shaded=true;
                        } else {
                            auto* pixel=image.data()+(size_t(py)*width+px)*3;
                            pixel[0]-=(pixel[0]*shade)>>8;
                            pixel[1]-=(pixel[1]*shade)>>8;
                            pixel[2]-=(pixel[2]*shade*7)>>11;
                        }
                    }
                }
            }
            x=end;
        }
    }
private:
    static constexpr int Scale=4;
    static constexpr float Reach=54.f;
    struct Platform {
        Platform(int left, int screenY, int topPx) : x(left), y(screenY), top(topPx) {}
        int x,y,top;
        bool shaded=false;
        std::array<unsigned char,32*32> mask{};
    };
    struct Receiver {
        int top=240*Scale, platform=-1;
        bool operator==(Receiver other) const { return top==other.top && platform==other.platform; }
    };

    Receiver receiverAt(int x, float feet) const {
        Receiver result;
        const int col=(x+offset)/16;
        if (col<0 || col>=static_cast<int>(tops.size())) return result;
        const unsigned exposed=tops[col] & ~(unsigned(tops[col])<<1);
        const float minY=(feet-3.f)*Scale;
        for (int row=0; row<13; ++row) {
            const int top=(32+16*row)*Scale;
            if ((exposed & (1u<<row)) && top>=minY) { result.top=top; break; }
        }
        for (int i=0; i<static_cast<int>(platforms.size()); ++i) {
            const auto& p=platforms[i];
            if (x>=p.x && x<p.x+8 && p.top>=minY && p.top<result.top)
                result={p.top,i};
        }
        return result;
    }
    int offset=0;
    std::array<std::uint16_t,33> tops{};
    std::array<int,64> platformForSlot{};
    std::vector<Platform> platforms;
};
}
