#include "world_background.h"
#include "world_assets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#define MARIO_BACKGROUND_SSE2 1
#endif

namespace mario {
namespace {
constexpr int W = WorldBackground::Width, H = WorldBackground::Height;
constexpr float Pi = 3.14159265358979323846f;
struct Color { int r, g, b; };
struct Point { float x, y; };
Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {int(a.r+(b.r-a.r)*t), int(a.g+(b.g-a.g)*t), int(a.b+(b.b-a.b)*t)};
}
float smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t*t*(3-2*t); }
unsigned hash(unsigned n) {
    n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; return n ^ (n >> 16);
}
float random(unsigned n) { return float(hash(n) & 65535u) / 65535.0f; }
float wrap(float n, float period) { return n-std::floor(n/period)*period; }

class Canvas {
public:
    explicit Canvas(std::vector<unsigned char>& rgb, int width) : rgb_(rgb), width_(width) { rgb_.resize(std::size_t(width_)*H*3); }
    int width() const { return width_; }
    void span(int y, int x0, int x1, Color c, int opacity=255) {
        if (y<0 || y>=H || x1<0 || x0>=width_ || opacity<=0) return;
        x0=std::max(x0,0); x1=std::min(x1,width_-1); opacity=std::min(opacity,255);
        auto* p=rgb_.data()+(std::size_t(y)*width_+x0)*3;
#if defined(MARIO_BACKGROUND_SSE2)
        // Sixteen RGB pixels fit three unaligned SSE2 stores. This keeps full
        // 1024x960 quality without rescaling scenery or spending the frame
        // budget on millions of individual channel blends.
        const auto a=_mm_setr_epi8((char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r);
        const auto b=_mm_setr_epi8((char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g);
        const auto d=_mm_setr_epi8((char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b,(char)c.r,(char)c.g,(char)c.b);
        if(opacity==255) {
            for(;x0+15<=x1;x0+=16,p+=48) {
                _mm_storeu_si128((__m128i*)(p),a);_mm_storeu_si128((__m128i*)(p+16),b);_mm_storeu_si128((__m128i*)(p+32),d);
            }
        } else {
            const auto zero=_mm_setzero_si128(),weight=_mm_set1_epi16((short)opacity),inverse=_mm_set1_epi16((short)(255-opacity));
            const auto rounding=_mm_set1_epi16(128);
            const auto blend=[&](unsigned char* dst,__m128i src) {
                const auto before=_mm_loadu_si128((const __m128i*)dst);
                auto lo=_mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(before,zero),inverse),_mm_mullo_epi16(_mm_unpacklo_epi8(src,zero),weight));
                auto hi=_mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(before,zero),inverse),_mm_mullo_epi16(_mm_unpackhi_epi8(src,zero),weight));
                lo=_mm_add_epi16(lo,rounding);hi=_mm_add_epi16(hi,rounding);
                lo=_mm_srli_epi16(_mm_add_epi16(lo,_mm_srli_epi16(lo,8)),8);
                hi=_mm_srli_epi16(_mm_add_epi16(hi,_mm_srli_epi16(hi,8)),8);
                _mm_storeu_si128((__m128i*)dst,_mm_packus_epi16(lo,hi));
            };
            for(;x0+15<=x1;x0+=16,p+=48) {blend(p,a);blend(p+16,b);blend(p+32,d);}
        }
#endif
        if(opacity==255) for(int x=x0;x<=x1;++x,p+=3) {p[0]=(unsigned char)c.r;p[1]=(unsigned char)c.g;p[2]=(unsigned char)c.b;}
        else for(int x=x0;x<=x1;++x,p+=3) {
            p[0]=(unsigned char)((p[0]*(255-opacity)+c.r*opacity+127)/255);
            p[1]=(unsigned char)((p[1]*(255-opacity)+c.g*opacity+127)/255);
            p[2]=(unsigned char)((p[2]*(255-opacity)+c.b*opacity+127)/255);
        }
    }
    void gradient(Color top,Color middle,Color bottom,float stop=.68f) {
        for(int y=0;y<H;++y) {
            const float t=float(y)/(H-1);
            Color c=t<stop?mix(top,middle,smooth(t/stop)):mix(middle,bottom,smooth((t-stop)/(1-stop)));
            span(y,0,width_-1,c);
        }
    }
    void image(const WorldAssetFrame& source,float cx,float base,float scale,Color tint,int opacity=255) {
        if(source.rgba.empty() || scale<=0 || opacity<=0)return;
        const int imageWidth=std::max(1,int(std::lround(source.width*scale)));
        const int imageHeight=std::max(1,int(std::lround(source.height*scale)));
        const int left=int(std::lround(cx-imageWidth*.5f)),top=int(std::lround(base-imageHeight));
        const int x0=std::max(left,0),x1=std::min(left+imageWidth,width_);
        const int y0=std::max(top,0),y1=std::min(top+imageHeight,H);
        if(x0>=x1 || y0>=y1)return;
        opacity=std::min(opacity,255);
        // The nearest-source column is independent of destination row. Avoid
        // hundreds of thousands of integer divisions while compositing each
        // mountain, tree or cloud. Fixed scratch arrays cannot grow the cache.
        std::array<unsigned,WorldBackground::Width*2> columns;
        const int visibleWidth=x1-x0;
        for(int column=0;column<visibleWidth;++column)
            columns[column]=((x0+column-left)*source.width/imageWidth)*4;
        std::array<unsigned char,256> alphaTable,redTable,greenTable,blueTable;
        for(unsigned value=0;value<256;++value) {
            alphaTable[value]=(unsigned char)((value*opacity+127)/255);
            redTable[value]=(unsigned char)((value*tint.r+127)/255);
            greenTable[value]=(unsigned char)((value*tint.g+127)/255);
            blueTable[value]=(unsigned char)((value*tint.b+127)/255);
        }
        for(int y=y0;y<y1;++y) {
            const auto* row=source.rgba.data()+std::size_t((y-top)*source.height/imageHeight)*source.width*4;
            auto* target=rgb_.data()+(std::size_t(y)*width_+x0)*3;
            for(int column=0;column<visibleWidth;++column,target+=3) {
                const auto* pixel=row+columns[column];
                const unsigned alpha=alphaTable[pixel[3]];
                if(!alpha)continue;
                const unsigned red=redTable[pixel[0]],green=greenTable[pixel[1]],blue=blueTable[pixel[2]];
                if(alpha==255) {target[0]=(unsigned char)red;target[1]=(unsigned char)green;target[2]=(unsigned char)blue;}
                else {
                    const unsigned inverse=255-alpha;
                    target[0]=(unsigned char)((target[0]*inverse+red*alpha+127)/255);
                    target[1]=(unsigned char)((target[1]*inverse+green*alpha+127)/255);
                    target[2]=(unsigned char)((target[2]*inverse+blue*alpha+127)/255);
                }
            }
        }

    }
    void ellipse(float cx,float cy,float rx,float ry,Color top,Color bottom,int opacity=255) {
        if(rx<=0 || ry<=0 || cx+rx<0 || cx-rx>=width_ || cy+ry<0 || cy-ry>=H) return;
        int y0=std::max(0,int(std::floor(cy-ry))),y1=std::min(H-1,int(std::ceil(cy+ry)));
        for(int y=y0;y<=y1;++y) {
            float norm=(y+.5f-cy)/ry;
            if(std::abs(norm)>=1) continue;
            float dx=rx*std::sqrt(std::max(0.0f,1-norm*norm)),left=cx-dx,right=cx+dx;
            int a=int(std::ceil(left)),b=int(std::floor(right));
            Color c=mix(top,bottom,(norm+1)*.5f);
            span(y,a,b,c,opacity);
            if(a>b) continue;
            span(y,a-1,a-1,c,int(opacity*(a-left)));
            span(y,b+1,b+1,c,int(opacity*(right-b)));
        }
    }
    void oval(float cx,float cy,float rx,float ry,Color c,int opacity=255) {ellipse(cx,cy,rx,ry,c,c,opacity);}
    void polygon(const std::vector<Point>& p,Color top,Color bottom,int opacity=255) {
        if(p.size()<3)return;
        float minY=float(H),maxY=0;
        for(const auto& v:p) {minY=std::min(minY,v.y);maxY=std::max(maxY,v.y);}
        int y0=std::max(0,int(std::floor(minY))),y1=std::min(H-1,int(std::ceil(maxY)));
        if(y0>y1)return;
        // Bucket only the scanlines each edge actually crosses. The rounded
        // hillside has 130 vertices, but a scanline sees only two intersections;
        // checking every edge against every row would waste the scrolling frame.
        std::array<std::array<float,16>,H> intersections;
        std::array<unsigned char,H> counts{};
        for(std::size_t i=0,j=p.size()-1;i<p.size();j=i++) {
            auto a=p[j],b=p[i];if(a.y>b.y)std::swap(a,b);
            if(b.y-a.y<.0001f)continue;
            int first=std::max(y0,int(std::ceil(a.y-.5f))),last=std::min(y1,int(std::ceil(b.y-.5f))-1);
            float step=(b.x-a.x)/(b.y-a.y),xx=a.x+(first+.5f-a.y)*step;
            for(int y=first;y<=last;++y,xx+=step)if(counts[y]<16)intersections[y][counts[y]++]=xx;
        }
        for(int y=y0;y<=y1;++y) {
            const int count=counts[y];if(count<2)continue;
            auto& row=intersections[y];
            std::sort(row.begin(),row.begin()+count);float yy=y+.5f;
            Color c=mix(top,bottom,(yy-minY)/std::max(1.0f,maxY-minY));
            for(int i=0;i+1<count;i+=2) {
                float a=row[i],b=row[i+1];
                int left=int(std::ceil(a)),right=int(std::floor(b));
                span(y,left,right,c,opacity);
                span(y,left-1,left-1,c,int(opacity*(left-a)));
                span(y,right+1,right+1,c,int(opacity*(b-right)));
            }
        }
    }
    void line(float x0,float y0,float x1,float y1,float width,Color c,int opacity=255) {
        float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy);
        if(len<.01f) {oval(x0,y0,width*.5f,width*.5f,c,opacity);return;}
        float nx=-dy/len*width*.5f,ny=dx/len*width*.5f;
        polygon({{x0+nx,y0+ny},{x1+nx,y1+ny},{x1-nx,y1-ny},{x0-nx,y0-ny}},c,c,opacity);
        oval(x0,y0,width*.5f,width*.5f,c,opacity);oval(x1,y1,width*.5f,width*.5f,c,opacity);
    }
private:
    std::vector<unsigned char>& rgb_;
    int width_;
};

// Motif sizes and spacing retain their original scale. Wider canvases reveal
// repeated world positions rather than scaling a 1024-pixel picture to fit.
// Default-width calls keep the historical composition byte-exact.
template<class Draw>
void horizontalCopies(Canvas& c, float x, float radius, float period, Draw draw) {
    draw(x);
    if (c.width() <= W) return;
    for (float copy = x + period; copy - radius < c.width(); copy += period)
        if (copy + radius >= W) draw(copy);
}


bool asset(Canvas& c,WorldAsset object,unsigned frame,float x,float base,float scale,Color tint,int opacity=255) {
    const auto info=worldAssetInfo(object);
    if(!info.width || !info.height)return false;
    const float halfWidth=info.width*scale*.5f;
    if(x+halfWidth<0 || x-halfWidth>=c.width() || base<0 || base-info.height*scale>=H)return true;
    const auto& image=worldAssetFrame(object,frame);
    if(image.rgba.empty())return false;
    c.image(image,x,base,scale,tint,opacity);
    return true;
}

// Near foliage belongs to the native ground plane. Its full root footprint
// must be supported; a canopy may overhang a pit while its trunk remains safe.
bool supportedRoots(const Canvas& c,const WorldScene& scene,int left,int right) {
    if(!scene.groundSupportKnown)return true;
    left=std::max(left,0);right=std::min(right,c.width()-1);
    if(left>right)return true; // Offscreen roots cannot float in the viewport.
    for(int x=left/4;x<=right/4;++x)
        if(x>=int(scene.groundSupport.size()) || !scene.groundSupport[x])return false;
    return true;
}
struct AssetRoots { int last=-1,left=0,right=0; };
const AssetRoots& assetRoots(WorldAsset object,unsigned frame,const WorldAssetFrame& image) {
    // Metadata only: keep no extra RGBA copies and scan each supplied frame once.
    // This is independent of the scene clock, camera and save/load history.
    static std::array<std::vector<AssetRoots>,9> metadata;
    auto& entries=metadata[unsigned(object)];
    if(entries.size()<=frame)entries.resize(frame+1);
    auto& result=entries[frame];
    if(result.last>=0)return result;
    for(int y=int(image.height)-1;y>=0 && result.last<0;--y)
        for(unsigned x=0;x<image.width;++x)
            if(image.rgba[(std::size_t(y)*image.width+x)*4+3]>=128) {result.last=y;break;}
    result.left=int(image.width);result.right=-1;
    if(result.last>=0)for(int y=std::max(0,result.last-4);y<=result.last;++y)
        for(unsigned x=0;x<image.width;++x)
            if(image.rgba[(std::size_t(y)*image.width+x)*4+3]>=128) {
                result.left=std::min(result.left,int(x));result.right=std::max(result.right,int(x));
            }
    return result;
}
bool groundedAsset(Canvas& c,const WorldScene& scene,WorldAsset object,unsigned frame,
                   float x,float oldBase,float scale,Color tint,int opacity=255) {
    if(!scene.groundSupportKnown)return asset(c,object,frame,x,oldBase,scale,tint,opacity);
    const auto info=worldAssetInfo(object);
    if(!info.width || !info.height)return false;
    const int imageWidth=std::max(1,int(std::lround(info.width*scale)));
    const int imageHeight=std::max(1,int(std::lround(info.height*scale)));
    const int left=int(std::lround(x-imageWidth*.5f));
    if(left+imageWidth<=0 || left>=c.width())return true;
    const auto& image=worldAssetFrame(object,frame);
    if(image.rgba.empty())return false;
    const auto& roots=assetRoots(object,frame,image);
    if(roots.last<0)return true;
    // Match Canvas::image's nearest-source mapping exactly. The final opaque
    // source row lands on y831, immediately against the native floor at y832.
    const auto firstPixel=[](int source,int extent,int sourceExtent) {
        return (source*extent+sourceExtent-1)/sourceExtent;
    };
    const int rootLeft=left+firstPixel(roots.left,imageWidth,int(image.width));
    const int rootRight=left+firstPixel(roots.right+1,imageWidth,int(image.width))-1;
    if(!supportedRoots(c,scene,rootLeft,rootRight))return true;
    int lastRow=firstPixel(roots.last+1,imageHeight,int(image.height))-1;
    // A downscale can skip the lowest source pixel horizontally. Locate the
    // actual last opaque destination row rather than leaving a one-pixel gap.
    for(;lastRow>=0;--lastRow) {
        const unsigned sourceY=unsigned(lastRow)*image.height/unsigned(imageHeight);
        bool opaque=false;
        for(int dx=std::max(0,rootLeft-left);dx<=std::min(imageWidth-1,rootRight-left);++dx) {
            const unsigned sourceX=unsigned(dx)*image.width/unsigned(imageWidth);
            if(image.rgba[(std::size_t(sourceY)*image.width+sourceX)*4+3]>=128) {opaque=true;break;}
        }
        if(opaque)break;
    }
    if(lastRow<0)return true;
    const int base=208*4-1-lastRow+imageHeight;
    c.image(image,x,float(base),scale,tint,opacity);
    return true;
}

void cloud(Canvas& c,float x,float y,float size,bool night) {
    const Color shadow=night?Color{107,127,163}:Color{180,211,221};
    const Color cream=night?Color{150,171,196}:Color{255,252,236};
    // Separate soft shadow, rounded body and thin sunlit tops. These silhouettes
    // have no pixel outlines and stay quieter than the platform geometry.
    c.oval(x,y+size*.20f,size*1.53f,size*.38f,shadow,45);
    constexpr float lobes[][4]={{-1.08f,.10f,.50f,.28f},{-.60f,-.11f,.56f,.44f},{-.05f,-.29f,.66f,.64f},{.56f,-.02f,.62f,.43f},{1.03f,.14f,.43f,.27f}};
    for(const auto& l:lobes) c.ellipse(x+l[0]*size,y+l[1]*size,l[2]*size,l[3]*size,cream,shadow,night?120:235);
    if(!night) {
        c.oval(x-size*.30f,y-size*.55f,size*.31f,size*.055f,{255,255,249},170);
        c.oval(x+size*.62f,y-size*.23f,size*.22f,size*.040f,{255,255,249},145);
    }
    c.oval(x+size*.1f,y+size*.26f,size*1.20f,size*.055f,night?Color{155,174,200}:Color{235,240,221},90);
}

void hill(Canvas& c,float camera,float parallax,float base,float amplitude,float period,float phase,Color top,Color bottom) {
    std::vector<Point> p;p.reserve(132);
    for(int x=-8;x<=c.width()+8;x+=8) {
        float t=(x+camera*parallax)/period*2*Pi+phase;
        float y=base+std::sin(t)*amplitude+std::sin(t*2.0f+phase*.61f)*amplitude*.19f;
        p.push_back({float(x),y});
    }
    p.push_back({float(c.width()+8),float(H+1)});p.push_back({-8,float(H+1)});
    c.polygon(p,top,bottom);
}

void distantTree(Canvas& c,float x,float ground,float h,float phase,bool night) {
    const Color shade=night?Color{24,54,68}:Color{64,120,111};
    const Color light=night?Color{38,74,78}:Color{112,158,122};
    c.line(x,ground,x+std::sin(phase)*h*.015f,ground-h*.58f,h*.055f,shade);
    c.ellipse(x,ground-h*.48f,h*.29f,h*.36f,light,shade);
    c.ellipse(x-h*.17f,ground-h*.32f,h*.23f,h*.28f,mix(light,shade,.25f),shade);
    c.ellipse(x+h*.15f,ground-h*.35f,h*.23f,h*.29f,light,shade);
}

void shrub(Canvas& c,float x,float ground,float size,unsigned seed,float time,bool night) {
    float sway=std::sin(time*1.65f+seed*.3f)*size*.026f;
    Color shadow=night?Color{24,56,55}:Color{38,91,62};
    Color mid=night?Color{49,94,74}:Color{90,149,81};
    Color light=night?Color{83,126,91}:Color{162,187,104};
    c.oval(x,ground+2,size*.94f,size*.10f,{24,64,54},night?70:45);
    constexpr float lobes[][4]={{-.61f,-.20f,.39f,.26f},{-.26f,-.38f,.43f,.34f},{.19f,-.36f,.44f,.34f},{.59f,-.21f,.39f,.27f},{.0f,-.16f,.68f,.28f}};
    for(unsigned i=0;i<5;++i) {
        const auto* l=lobes[i];
        float variant=.93f+random(seed+i)*.13f;
        float xx=x+l[0]*size+sway*(.4f-l[1]);
        float yy=ground+l[1]*size;
        c.ellipse(xx,yy,l[2]*size*variant,l[3]*size*variant,mid,shadow);
        c.ellipse(xx-size*.08f,yy-l[3]*size*.28f,l[2]*size*.64f,l[3]*size*.36f,light,mid,150);
    }
    for(unsigned j=0;j<9;++j) {
        float xx=x+(random(seed+j*13)-.5f)*size*1.52f+sway;
        float yy=ground-size*(.10f+random(seed+j*29)*.31f);
        c.ellipse(xx,yy,size*.056f,size*.032f,light,mid,120);
        if(j%3==0)c.line(xx-2,yy+5,xx+5,yy+8,1.2f,shadow,100);
    }
}

void flower(Canvas& c,float x,float ground,float size,float phase,bool night,Color petals) {
    float sway=std::sin(phase)*size*.16f;
    Color stem=night?Color{48,107,81}:Color{61,115,64};
    c.line(x,ground,x+sway,ground-size,1.5f,stem,220);
    c.ellipse(x-2,ground-size*.48f,3.2f,1.4f,stem,stem,210);
    float y=ground-size;
    for(int i=0;i<5;++i) {
        float a=i*2*Pi/5;
        c.oval(x+sway+std::cos(a)*2.6f,y+std::sin(a)*2.6f,2.1f,2.1f,petals,night?160:235);
    }
    c.oval(x+sway,y,1.5f,1.5f,night?Color{204,185,107}:Color{251,211,99});
}

void butterfly(Canvas& c,float x,float y,float t,Color wings,bool night) {
    float flap=.22f+.78f*std::abs(std::sin(t*7.0f));
    c.ellipse(x-3*flap,y-2,4*flap,4.5f,wings,mix(wings,{92,91,84},.5f),night?95:175);
    c.ellipse(x+3*flap,y-2,4*flap,4.5f,wings,mix(wings,{92,91,84},.5f),night?95:175);
    c.line(x,y-3,x,y+2,1.3f,night?Color{164,170,153}:Color{90,99,70},190);
}

void overworld(Canvas& c,const WorldScene& s,float time,float camera) {
    const bool night=s.night;
    c.gradient(night?Color{20,37,80}:Color{60,150,214},night?Color{76,109,149}:Color{178,222,222},night?Color{156,167,163}:Color{243,232,175},.72f);
    float sunX=832-camera*.025f;
    sunX=std::clamp(sunX,650.0f,930.0f);
    if(night) {
        for(int i=0;i<35;++i) {
            float x=wrap(random(i+822)*1400-camera*.025f,1400)-80;
            horizontalCopies(c,x,3,1400,[&](float x) {
                float y=140+random(i+913)*365;
                int alpha=80+int(45*std::sin(time*.8f+i*1.7f));
                c.oval(x,y,1.2f+(i%4==0?1.1f:0),1.2f+(i%4==0?1.1f:0),{220,233,242},alpha);
            });
        }
        for(int r=76;r>35;r-=8)c.oval(sunX,208,float(r),float(r),{212,228,244},5);
        c.oval(sunX,208,33,33,{231,237,222});
        c.oval(sunX-9,201,28,29,{227,235,227},170);
        c.oval(sunX+12,210,5,5,{182,205,210},100);
        c.oval(sunX-7,189,3,3,{182,205,210},100);
    } else {
        for(int r=160;r>38;r-=12)c.oval(sunX,219,float(r),float(r),{255,245,188},6);
        c.oval(sunX,219,36,36,{255,252,214},235);
        c.oval(sunX-8,210,23,23,{255,255,231},165);
        // Long haze rays are deliberately very faint, behind every native tile.
        c.polygon({{sunX-8,235},{sunX+14,233},{sunX+265,748},{sunX+60,748}},{255,243,194},{250,225,154},7);
        c.polygon({{sunX-19,229},{sunX-2,232},{sunX-70,778},{sunX-268,778}},{255,243,194},{250,225,154},6);
    }
    const auto scenerySample=s.frame/5;
    for(int i=0;i<5;++i) {
        const float period=1740.0f;
        float x=wrap(130+i*367.0f-camera*.045f-time*(2.8f+i*.48f),period)-220;
        const WorldAsset object=i%3==0?WorldAsset::Cloud:i%3==1?WorldAsset::CloudStill1:WorldAsset::CloudStill2;
        const unsigned frame=worldAssetSample(object,scenerySample,(i%3)*9);
        const float scale=object==WorldAsset::Cloud?.86f+.14f*random(i+52):.52f+.20f*random(i+52);
        horizontalCopies(c,x,225,1740,[&](float x) {
            if(!asset(c,object,frame,x,295+random(i+51)*190,scale,
                      night?Color{148,179,210}:Color{255,255,255},night?145:238))
                cloud(c,x,220+random(i+51)*240,47+random(i+88)*25,night);
        });
    }
    // Textured photographs continue down behind the plants to native ground.
    // The near plane is larger and its feathered crop base lies below the
    // complete canvas; no flat procedural hillside covers the source texture.
    // A 1643px photograph repeats every 1360px, leaving 283px of side overlap.
    const float farShift=wrap(camera*.065f,1840),nearShift=wrap(camera*.13f,1360);
    for(int i=-1;i<=c.width()/1840+1;++i)
        asset(c,WorldAsset::MountainFar,0,i*1840.0f+760-farShift,930,1.80f,
              night?Color{86,124,161}:Color{191,214,214},night?190:216);
    for(int i=-1;i<=c.width()/1360+1;++i)
        asset(c,WorldAsset::MountainNear,0,i*1360.0f+425-nearShift,1040,1.55f,
              night?Color{79,118,136}:Color{234,243,219},255);
    const Color foliageTint=night?Color{101,143,156}:Color{238,250,225};
    // Trees rooted on the actual level floor scroll with that floor. Far
    // mountains/clouds retain their independent parallax and animation.
    const float treeParallax=s.groundSupportKnown?1.0f:.30f;
    const float plantParallax=s.groundSupportKnown?1.0f:.55f;
    for(int i=0;i<11;++i) {
        float x=wrap(i*245.0f+random(i+61)*50-camera*treeParallax,2695)-100;
        const float scale=.66f+(i%3)*.16f;
        horizontalCopies(c,x,105,2695,[&](float x) {
            // Native overworld ground starts at y208. The supplied tree has
            // three transparent source rows below its opaque trunk.
            const float ground=208.0f*4;
            if(!groundedAsset(c,s,WorldAsset::Tree,worldAssetSample(WorldAsset::Tree,scenerySample,(i%3)*9),x,ground+3.0f*scale,scale,foliageTint,218) &&
               supportedRoots(c,s,int(std::floor(x-3)),int(std::ceil(x+3))))
                distantTree(c,x,ground,64,time*.7f+i,night);
        });
    }
    for(int i=0;i<11;++i) {
        float x=wrap(i*240.0f+random(i+450)*56-camera*plantParallax,2640)-110;
        const float scale=.58f+random(i+82)*.37f;
        horizontalCopies(c,x,95,2640,[&](float x) {
            if(!groundedAsset(c,s,WorldAsset::Shrub,worldAssetSample(WorldAsset::Shrub,scenerySample,(i%3)*9),x,824,scale,foliageTint) &&
               supportedRoots(c,s,int(std::floor(x-32)),int(std::ceil(x+32))))
                shrub(c,x,s.groundSupportKnown?208.0f*4:820,52,unsigned(i+500),time,night);
        });
    }
    for(int i=0;i<32;++i) {
        float x=wrap(i*83.0f+random(i+785)*22-camera*plantParallax,2656)-50;
        const float y=829+random(i+982)*2,scale=.58f+random(i+78)*.35f;
        horizontalCopies(c,x,48,2656,[&](float x) {
            groundedAsset(c,s,WorldAsset::Grass,worldAssetSample(WorldAsset::Grass,scenerySample,(i%3)*9),x,y,scale,foliageTint,237);
            if(i%5==0 && supportedRoots(c,s,int(std::floor(x+12)),int(std::ceil(x+16))))
                flower(c,x+14,s.groundSupportKnown?208.0f*4:y,10,time*1.8f+i,night,i%2?Color{248,219,200}:Color{249,242,219});
        });
    }
    for(int i=0;i<3;++i) {
        float x=wrap(225+i*520.0f-camera*.32f+std::sin(time*.39f+i)*50,1580)-70;
        horizontalCopies(c,x,18,1580,[&](float x) {
            const float y=706+std::sin(time*.51f+i*2)*31;
            if(!asset(c,WorldAsset::Butterfly,worldAssetSample(WorldAsset::Butterfly,scenerySample,i*9),x,y,.72f+(i%2)*.24f,
                      night?Color{130,146,167}:Color{255,255,255},night?135:230))
                butterfly(c,x,y,time+i,Color{242,194,132},night);
        });
    }
    for(int i=0;i<12;++i) {
        float x=wrap(i*147.0f-camera*.2f+time*3,1764)-40;
        horizontalCopies(c,x,3,1764,[&](float x) {
            float y=600+wrap(random(i+61)*240-time*1.6f,240);
            c.oval(x,y,1.4f,1.4f,night?Color{214,227,162}:Color{255,246,202},night?90:70);
        });
    }

}

void underwater(Canvas& c,const WorldScene&,float time,float camera) {
    c.gradient({37,117,152},{29,87,123},{21,50,88},.63f);
    for(int i=0;i<6;++i) {
        float x=wrap(i*307.0f-camera*.05f+std::sin(time*.22f+i)*20,1842)-130;
        horizontalCopies(c,x,400,1842,[&](float x) {
            c.polygon({{x,0},{x+32,0},{x+225,780},{x+94,780}},{182,225,221},{47,109,136},14);
            c.polygon({{x+50,0},{x+69,0},{x+326,830},{x+260,830}},{157,220,219},{48,111,129},7);
        });
    }
    hill(c,camera,.09f,765,45,1010,.9f,{55,112,121},{32,72,91});
    hill(c,camera,.19f,848,25,900,2.2f,{42,97,109},{25,63,81});
    for(int i=0;i<19;++i) {
        float x=wrap(i*117.0f-camera*.29f,2223)-45;
        horizontalCopies(c,x,100,2223,[&](float x) {
            float base=850+random(i+81)*26;
            float h=38+random(i+713)*69;
            for(int n=0;n<3;++n) {
                std::vector<Point> p;
                for(int j=0;j<=9;++j) {
                    float v=j/9.0f;
                    float xx=x+n*10+std::sin(time*.9f+i+v*3)*v*11;
                    p.push_back({xx-3.5f*(1-v*.82f),base-v*h});
                }
                for(int j=9;j>=0;--j) {
                    float v=j/9.0f;
                    float xx=x+n*10+std::sin(time*.9f+i+v*3)*v*11;
                    p.push_back({xx+3.5f*(1-v*.82f),base-v*h});
                }
                c.polygon(p,{61,144,128},{34,100,111},210);
            }
            if(i%4==0)c.ellipse(x+25,base-8,16,10,{91,132,143},{39,91,113});
        });
    }
    for(int i=0;i<24;++i) {
        float x=wrap(random(i+542)*2100-camera*.2f+std::sin(time*.6f+i)*8,2100)-90;
        horizontalCopies(c,x,8,2100,[&](float x) {
            float y=122+wrap(random(i+44)*750-time*(6+i%4*2),750);
            float r=2+random(i+77)*4;
            c.oval(x,y,r,r,{164,217,229},70);
            c.oval(x,y,r-1,r-1,{36,101,136},110);
            c.oval(x-r*.3f,y-r*.4f,1,1,{218,239,237},110);
        });
    }
}

void cavern(Canvas& c,const WorldScene&,float time,float camera) {
    c.gradient({20,27,51},{35,50,73},{26,46,53},.65f);
    // Distant cavern shelves have broad curves; interactable bricks keep all
    // their original native boundaries and are composited later by the caller.
    hill(c,camera,.07f,730,70,1420,1.2f,{59,79,92},{31,48,64});
    hill(c,camera,.19f,857,52,1010,2.4f,{48,79,82},{24,44,54});
    for(int i=0;i<13;++i) {
        float x=wrap(i*189.0f-camera*.10f,2457)-80;
        horizontalCopies(c,x,45,2457,[&](float x) {
            float h=42+random(i+51)*72;
            c.polygon({{x-28,128},{x+28,128},{x+9,128+h},{x,128+h+24},{x-8,128+h}},{37,49,66},{46,63,78});
        });
    }
    for(int i=0;i<8;++i) {
        float x=wrap(i*305.0f+88-camera*.27f,2440)-90;
        horizontalCopies(c,x,70,2440,[&](float x) {
            float y=826+random(i+812)*19;
            float h=20+random(i+233)*20;
            float glow=.72f+.16f*std::sin(time*.85f+i);
            for(int r=48;r>13;r-=7)c.oval(x,y-h*.5f,float(r),float(r)*.8f,{76,157,164},int(3*glow));
            c.polygon({{x-10,y},{x-13,y-h*.7f},{x-3,y-h},{x+8,y-h*.62f},{x+7,y}},{97,183,174},{35,94,125});
            c.polygon({{x-3,y-h},{x-1,y},{x+7,y},{x+8,y-h*.62f}},{163,216,197},{57,124,142},180);
            c.line(x-3,y-h,x-1,y,1,{171,225,207},150);
        });
    }
    for(int i=0;i<20;++i) {
        float x=wrap(random(i+764)*2100-camera*.15f+time*1.1f,2100)-70;
        horizontalCopies(c,x,3,2100,[&](float x) {
            float y=280+wrap(random(i+84)*530-time*1.5f,530);
            c.oval(x,y,1.4f,1.4f,{120,177,186},40+int(28*std::sin(time+i)));
        });
    }
}

void castle(Canvas& c,const WorldScene&,float time,float camera) {
    c.gradient({30,24,42},{55,37,53},{93,58,46},.77f);
    for(int i=0;i<7;++i) {
        float x=wrap(i*368.0f-camera*.08f,2576)-180;
        horizontalCopies(c,x,330,2576,[&](float x) {
            c.polygon({{x-46,151},{x+46,151},{x+46,867},{x-46,867}},{45,35,53},{73,47,53});
            c.polygon({{x-54,147},{x+54,147},{x+54,171},{x-54,171}},{64,48,63},{49,37,56});
            c.line(x+31,182,x+31,825,4,{91,65,69},65);
            // An arched opening far behind the actual level architecture.
            c.ellipse(x+174,445,60,99,{23,25,41},{33,30,44});
            c.polygon({{x+114,444},{x+234,444},{x+234,840},{x+114,840}},{27,26,41},{42,34,46});
            c.line(x+174,392,x+174,602,3,{83,55,55},100);
        });
    }
    for(int i=0;i<4;++i) {
        float x=wrap(i*570.0f+205-camera*.11f,2280)-110;
        horizontalCopies(c,x,120,2280,[&](float x) {
            float y=683;
            for(int r=95;r>12;r-=12)c.oval(x,y,float(r),float(r),{189,101,52},3);
            float f=std::sin(time*4+i)*3;
            c.polygon({{x-8,y+9},{x-11,y-9},{x+f,y-34},{x+8,y-13},{x+9,y+8}},{247,176,89},{164,82,51},210);
            c.ellipse(x,y-2,5,10,{252,213,122},{233,123,60},210);
            c.line(x-15,y+11,x+15,y+11,4,{106,85,84});
        });
    }
    hill(c,camera,.19f,893,26,940,.8f,{74,48,52},{43,31,43});
    for(int i=0;i<14;++i) {
        float x=wrap(i*171.0f-camera*.19f+std::sin(time*.3f+i)*15,2394)-80;
        horizontalCopies(c,x,3,2394,[&](float x) {
            float y=370+wrap(random(i+531)*485-time*(3+i%3),485);
            int alpha=55+int(35*std::sin(time+i));
            c.oval(x,y,1.5f,2.2f,{247,159,92},alpha);
        });
    }
}
} // namespace

void WorldBackground::render(const WorldScene& scene,std::vector<unsigned char>& rgb,int width) {
    width = std::clamp(width, Width, Width*2);
    // Cache a complete 12 fps scenery frame. A camera move regenerates the
    // stateless parallax; repeated paused or stationary gameplay frames copy
    // only the RGB surface. No native RAM, random generator, or clock is read.
    const auto animationFrame=scene.frame/5;
    const int camera=scene.cameraX; // Native integer pixels; do not coarsen scrolling.
    const unsigned area=std::min(scene.areaType,3u);
    if(!valid_ || width!=width_ || animationFrame!=frame_ || camera!=camera_ || area!=area_ || scene.night!=night_ || scene.enabled!=enabled_ ||
       scene.groundSupportKnown!=groundSupportKnown_ || (scene.groundSupportKnown && scene.groundSupport!=groundSupport_)) {
        Canvas c(cache_, width);
        float time=float(animationFrame%864000u)/12.0f;
        float parallaxCamera=float(camera)*4.0f;
        if(!scene.enabled) {
            if(area==1)c.gradient({142,136,255},{142,136,255},{142,136,255});
            else c.gradient({0,0,0},{0,0,0},{0,0,0});
        } else if(area==0)underwater(c,scene,time,parallaxCamera);
        else if(area==2)cavern(c,scene,time,parallaxCamera);
        else if(area==3)castle(c,scene,time,parallaxCamera);
        else overworld(c,scene,time,parallaxCamera);
        frame_=animationFrame;camera_=camera;width_=width;area_=area;night_=scene.night;enabled_=scene.enabled;valid_=true;
        groundSupportKnown_=scene.groundSupportKnown;groundSupport_=scene.groundSupport;
    }
    rgb=cache_;
}
} // namespace mario
