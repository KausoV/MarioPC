#include "iskra_world.h"
#include "iskra_detail_atlas.h"
#include "iskra_elderwood.h"
#include "iskra_forest_props.h"
#include "iskra_highlands.h"
#include "iskra_landmarks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <thread>
#include <limits>
#include <vector>

namespace iskra {
namespace {
using Color = unsigned;
struct Point { float x, y; };
constexpr float Tau = 6.283185307f;
Color mix(Color a, Color b, float f) {
    f=std::clamp(f,0.f,1.f);
    auto channel=[&](int shift){return unsigned(float((a>>shift)&255)*(1-f)+float((b>>shift)&255)*f+0.5f);};
    return channel(16)<<16 | channel(8)<<8 | channel(0);
}
float noise(unsigned n) {
    n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
    return float(n & 65535)/65535.f;
}
int wrap(int x, int period){ return (x%period+period)%period; }
struct Palette {
    Color zenith, upper, horizon, mist, distant, deep, bark, moss, light, jewel, moon;
};
// The source painting's plum twilight, jade ruin and peach dawn anchor eight chapters.
constexpr Palette Chapters[]={
 {0x17152e,0x514273,0xc88a9f,0xe5b299,0x827394,0x223747,0x38555c,0x80a99a,0xcbdcc0,0xffcb80,0xffda99},
 {0x10283f,0x24515e,0x8baaa1,0xbbbd9f,0x668a91,0x1e3b48,0x395b58,0x75a58e,0xb8dbac,0x8bedd0,0xf9e2ae},
 {0x142938,0x32575a,0x88a493,0xc9bf95,0x6b9083,0x25433f,0x345b53,0x94b784,0xd8ddb0,0xaaf1c3,0xe5deb0},
 {0x15233e,0x475b82,0xaab5c2,0xd6c9c4,0x8899ae,0x293a51,0x465c68,0x97bab7,0xe2e5cf,0xa4e0eb,0xf5eacd},
 {0x343750,0x81627a,0xd39c83,0xf3c999,0xab8081,0x3f3747,0x655657,0xb19c71,0xead3a0,0xffc278,0xffdf9d},
 {0x201a38,0x554765,0xa17b9a,0xd9acac,0x85718f,0x302b43,0x51495c,0x9a9c9c,0xdac9c4,0xdfb4fb,0xffd5a8},
 {0x11182f,0x30365d,0x66678e,0x9c8ea6,0x5b6283,0x1e2b41,0x344654,0x819a99,0xbdcfc7,0xb3c7ff,0xf5deba},
 {0x344967,0x8a7896,0xdca49b,0xf8d6a2,0xae8794,0x333e53,0x505964,0xa6b397,0xe6ddad,0xffdda0,0xffedb6}
};
int chapter(const Scenery&s){return std::clamp(s.world-1,0,7);}
Palette colors(const Scenery&s){
 auto p=Chapters[chapter(s)];
 if(s.area==2){p.zenith=mix(0x101c31,p.zenith,.15f);p.upper=mix(0x203148,p.upper,.28f);p.horizon=mix(0x587b8a,p.horizon,.26f);p.deep=mix(0x1a293d,p.deep,.35f);p.moss=mix(p.moss,0x76bcc7,.52f);}
 else if(s.area==3){p.zenith=mix(0x1b1b2b,p.zenith,.16f);p.upper=mix(0x453649,p.upper,.4f);p.horizon=mix(0xb37d78,p.horizon,.36f);p.deep=mix(0x2c2535,p.deep,.4f);p.moss=mix(p.moss,0xc39d77,.60f);}
 else if(s.area==0){p.zenith=mix(0x406a76,p.zenith,.35f);p.upper=mix(0x448590,p.upper,.35f);p.horizon=mix(0x619190,p.horizon,.22f);p.deep=mix(0x163945,p.deep,.30f);p.moss=mix(p.moss,0x62c4bc,.60f);}
 else if(s.night){p.upper=mix(p.upper,0x182745,.40f);p.horizon=mix(p.horizon,0x657997,.37f);}
 return p;
}

// Coordinates within each brush are local; the same paths scale with the canvas.
struct Canvas {
 std::vector<unsigned char>& rgb; int width,height; float zoom=1,ox=0,oy=0;
 int X(float v)const{return int(std::floor(ox+v*zoom+.5f));}
 int Y(float v)const{return int(std::floor(oy+v*zoom+.5f));}
 void span(int y,int left,int right,Color c) {
    if(y<0||y>=height)return;
    left=std::max(left,0);right=std::min(right,width);
    if(right<=left)return;
    unsigned char* d=rgb.data()+(size_t(y)*width+left)*3;
    const unsigned char r=c>>16,g=c>>8,b=c;
    for(int x=left;x<right;++x){*d++=r;*d++=g;*d++=b;}
 }
 void box(float x,float y,float w,float h,Color c){int x0=X(x),x1=X(x+w);for(int i=std::max(0,Y(y));i<std::min(height,Y(y+h));++i)span(i,x0,x1,c);}
 void oval(float x,float y,float rx,float ry,Color c){
    const float px=ox+x*zoom,py=oy+y*zoom,a=rx*zoom,b=ry*zoom;
    if(a<=0||b<=0)return;
    for(int i=std::max(0,int(std::floor(py-b)));i<std::min(height,int(std::ceil(py+b)));++i){
       float d=(i+.5f-py)/b;if(std::abs(d)>=1)continue;
       float e=a*std::sqrt(1-d*d);span(i,int(std::ceil(px-e-.5f)),int(std::ceil(px+e-.5f)),c);
    }
 }
 void polygon(std::initializer_list<Point> points,Color c){
    if(points.size()<3||points.size()>24)return;
    std::array<Point,24> v{};int n=0;float low=1e10f,high=-1e10f;
    for(auto p:points){p={ox+p.x*zoom,oy+p.y*zoom};v[n++]=p;low=std::min(low,p.y);high=std::max(high,p.y);}
    for(int row=std::max(0,int(std::floor(low)));row<std::min(height,int(std::ceil(high)));++row){
       const float y=row+.5f;std::array<float,24> hits{};int count=0;
       for(int i=0,j=n-1;i<n;j=i++){auto a=v[j],b=v[i];if((a.y>y)!=(b.y>y))hits[count++]=a.x+(b.x-a.x)*(y-a.y)/(b.y-a.y);}
       std::sort(hits.begin(),hits.begin()+count);
       for(int i=0;i+1<count;i+=2)span(row,int(std::ceil(hits[i]-.5f)),int(std::ceil(hits[i+1]-.5f)),c);
    }
 }
 void stroke(float x,float y,float x1,float y1,float thick,Color c){
    float dx=x1-x,dy=y1-y,len=std::hypot(dx,dy);if(len<.01f)return;
    dx*=thick*.5f/len;dy*=thick*.5f/len;
    polygon({{x-dy,y+dx},{x1-dy,y1+dx},{x1+dy,y1-dx},{x+dy,y-dx}},c);
 }
 void glow(float x,float y,float r,Color c,float strength){
    const float cx=ox+x*zoom,cy=oy+y*zoom,rad=r*zoom;
    if(rad<=0)return;
    for(int iy=std::max(0,int(cy-rad));iy<std::min(height,int(cy+rad+1));++iy){
       const float dy=(iy+.5f-cy)/rad, extent=rad*std::sqrt(std::max(0.f,1-dy*dy));
       for(int ix=std::max(0,int(cx-extent));ix<std::min(width,int(cx+extent+1));++ix){
          const float dx=(ix+.5f-cx)/rad,w=std::max(0.f,1.f-std::sqrt(dx*dx+dy*dy));float a=w*w*strength;
          auto*d=&rgb[(size_t(iy)*width+ix)*3];for(int j=0;j<3;++j)d[j]=static_cast<unsigned char>(d[j]+(int((c>>(16-8*j))&255)-d[j])*a);
       }
    }
 }
 void ring(float x,float y,float r,float thick,Color c){for(int i=0;i<80;++i){float a=i*Tau/80,b=(i+1)*Tau/80;stroke(x+r*std::cos(a),y+r*std::sin(a),x+r*std::cos(b),y+r*std::sin(b),thick,c);}}
};

// Resample in premultiplied alpha so the outer moss and fine roots retain their edge colour.
template<typename Painting>
std::uint32_t samplePainting(const Painting& art,float u,float v,int light){
    const float sx=std::clamp(u*art.width-.5f,0.f,float(art.width-1));
    const float sy=std::clamp(v*art.height-.5f,0.f,float(art.height-1));
    const int x=int(sx),y=int(sy),nextX=std::min(x+1,art.width-1),nextY=std::min(y+1,art.height-1);
    const float fx=sx-x,fy=sy-y;
    const unsigned colors[]={art.rgba[y*art.width+x],art.rgba[y*art.width+nextX],
                             art.rgba[nextY*art.width+x],art.rgba[nextY*art.width+nextX]};
    const float weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
    float a=0,r=0,g=0,b=0;
    for(int n=0;n<4;++n){
        const auto c=colors[n];const float w=(c&255)*weights[n];
        a+=w;r+=(c>>24)*w;g+=(c>>16&255)*w;b+=(c>>8&255)*w;
    }
    if(a<.5f)return 0;
    r/=a;g/=a;b/=a;
    if(light==0){r=r*.78f+2;g=g*.88f+7;b=b*.90f+13;}
    if(light==2){r=r*.85f+2;g=g*.90f+3;b=b*.96f+9;}
    if(light==3){r=r*.96f+4;g=g*.88f+2;b=b*.86f+5;}
    if(light==4){r=r*.77f+4;g=g*.82f+4;b=b*.90f+9;}
    return unsigned(r+.5f)<<24 | unsigned(g+.5f)<<16 | unsigned(b+.5f)<<8 | unsigned(a+.5f);
}

template<typename Painting>
void stampPropRegion(Canvas& canvas, const Painting& art, const Scenery& scene,
                     int x, int y, int width, int height,
                     int sx, int sy, int sw, int sh, bool flip=false) {
    if (width<=0 || height<=0 || sw<=0 || sh<=0) return;
    const int left=canvas.X(float(x)), right=canvas.X(float(x+width));
    const int top=canvas.Y(float(y)), bottom=canvas.Y(float(y+height));
    const int light=(scene.area==1 && scene.night)?4:scene.area;
    for (int py=std::max(0,top);py<std::min(canvas.height,bottom);++py) {
        const float v=(sy+sh*(py-top+.5f)/(bottom-top))/art.height;
        for(int px=std::max(0,left);px<std::min(canvas.width,right);++px) {
            float t=(px-left+.5f)/(right-left);
            const float u=(sx+sw*(flip?1.f-t:t))/art.width;
            const auto rgba=samplePainting(art,u,v,light);
            const unsigned alpha=rgba&255;
            if(!alpha)continue;
            auto* dst=canvas.rgb.data()+(size_t(py)*canvas.width+px)*3;
            // An all-black pixel denotes a transparent run in an unbaked brush.
            // Keep the cutout edge straight-alpha instead of baking a black matte.
            const bool empty=!(dst[0]||dst[1]||dst[2]);
            if(empty && alpha<90)continue;
            for(int k=0;k<3;++k){
                const unsigned color=(rgba>>(24-8*k))&255;
                dst[k]=static_cast<unsigned char>(empty || alpha==255 ? color :
                    (color*alpha+dst[k]*(255-alpha)+127)/255);
            }
        }
    }
}

template<typename Painting>
void stampProp(Canvas& canvas,const Painting& art,const Scenery& scene,
               int x,int y,int width,int height,bool flip=false) {
    stampPropRegion(canvas,art,scene,x,y,width,height,0,0,art.width,art.height,flip);
}


void stampScenery(Canvas& canvas,const highlands::Image& art,int x,int y,int w,int h,
                  Color tint,unsigned tintAmount,bool flip=false) {
    const int left=canvas.X(float(x)),right=canvas.X(float(x+w));
    const int top=canvas.Y(float(y)),bottom=canvas.Y(float(y+h));
    if (right<=left || bottom<=top) return;
    const float invW=1.f/(right-left),invH=1.f/(bottom-top);
    const int x0=std::max(left,0),x1=std::min(right,canvas.width);
    const unsigned tone=std::min(tintAmount,255u);
    const int tr=(tint>>16)&255,tg=(tint>>8)&255,tb=tint&255;
    if (x0>=x1) return;
    for(int py=std::max(0,top);py<std::min(bottom,canvas.height);++py) {
        float v=(py-top+.5f)*invH;
        auto* dst=canvas.rgb.data()+(size_t(py)*canvas.width+x0)*3;
        for(int px=x0;px<x1;++px,dst+=3) {
            const float u=(px-left+.5f)*invW;
            const auto src=samplePainting(art,flip?1.f-u:u,v,1);
            unsigned a=src&255;
            if(!a)continue;
            // Layer::prepare stores a transparent run as black. Keep straight
            // edge colours on an empty layer and blend over already painted trees.
            if(!(dst[0]|dst[1]|dst[2])) {
                if(a<96)continue;
                a=255;
            }
            const int r=((src>>24)*(255-tone)+tr*tone+127)/255;
            const int g=(((src>>16)&255)*(255-tone)+tg*tone+127)/255;
            const int b=(((src>>8)&255)*(255-tone)+tb*tone+127)/255;
            dst[0]=static_cast<unsigned char>((r*a+dst[0]*(255-a)+127)/255);
            dst[1]=static_cast<unsigned char>((g*a+dst[1]*(255-a)+127)/255);
            dst[2]=static_cast<unsigned char>((b*a+dst[2]*(255-a)+127)/255);
        }
    }
}

using detail::Sprite;

// Object coordinates denote a ground contact. Cropping the transparent source
// margin before scaling keeps every specimen rooted and gives equal visual heights.
void specimen(Canvas& g, Sprite id, float cx, float ground, float height,
              Color shadow, Color light, float tint, bool mirror = false) {
    const auto& pixels=detail::rgba[static_cast<unsigned>(id)];
    struct Bounds { int x0=16,y0=16,x1=0,y1=0; };
    static const auto bounds=[]{
        std::array<Bounds,static_cast<unsigned>(Sprite::Count)> result{};
        for (size_t i=0;i<result.size();++i) {
            auto& b=result[i];
            for(int y=0;y<16;++y)for(int x=0;x<16;++x)
                if((detail::rgba[i][y*16+x]&255)>16){
                    b.x0=std::min(b.x0,x);b.x1=std::max(b.x1,x+1);
                    b.y0=std::min(b.y0,y);b.y1=std::max(b.y1,y+1);
                }
        }
        return result;
    }();
    const auto b=bounds[static_cast<unsigned>(id)];
    if(b.y1<=b.y0) return;
    // Cap very wide specimens (a fossil or a fallen stone) to their allotted footprint.
    const float z=std::min(height/(b.y1-b.y0),height*1.40f/(b.x1-b.x0));
    const float left=cx-.5f*z*(b.x1-b.x0);
    for(int sy=b.y0;sy<b.y1;++sy){
        const float y=ground-(b.y1-sy)*z;
        for(int sx=b.x0;sx<b.x1;++sx){
            const auto pixel=pixels[sy*16+sx]; if((pixel&255)<=16)continue;
            const Color raw=pixel>>8;
            const float value=(((raw>>16)&255)*.26f+((raw>>8)&255)*.52f+(raw&255)*.22f)/255.f;
            // Retain the source's texture; black keylines on an icon need reflected
            // local light outdoors, particularly when seen through a second plane of haze.
            const bool rim=sx==0 || sx==15 || sy==0 || sy==15 ||
                !(pixels[sy*16+sx-1]&255) || !(pixels[sy*16+sx+1]&255) ||
                !(pixels[(sy-1)*16+sx]&255) || !(pixels[(sy+1)*16+sx]&255);
            Color color=mix(raw,mix(shadow,light,value),tint);
            if(rim && value<.42f){
                float lift=1.f-value/.42f;
                color=mix(color,mix(shadow,light,.23f),lift*(.77f+.20f*tint));
            }
            g.box(left+z*(mirror?b.x1-1-sx:sx-b.x0),y,z,z,color);
        }
    }
}

void forestTree(Canvas& g,float x,float ground,float height,Color haze,unsigned seed,int row){
    const auto& tree=noise(seed+39)<.53f?highlands::spruce:highlands::pine;
    const int h=std::max(1,int(std::lround(height)));
    const float spread=.87f+.23f*noise(seed+65);
    const int w=std::max(1,int(std::lround(h*float(tree.width)/tree.height*spread)));
    // Each parallax row keeps a different amount of the original needle colour.
    constexpr unsigned veil[]={196,153,109,72};
    stampScenery(g,tree,int(std::lround(x))-w/2,int(std::lround(ground))-h,
                 w,h,haze,veil[row],noise(seed+75)>.53f);
}

void crystal(Canvas&g,float x,float y,float h,Color c,float tilt){
    float r=h*.2f;
    g.polygon({{x-r,y-4},{x-r+tilt,y-h*.7f},{x+tilt,y-h},{x+r+tilt,y-h*.78f},{x+r,y-4},{x,y}},mix(c,0x26384f,.32f));
    g.polygon({{x,y},{x+tilt,y-h},{x+r+tilt,y-h*.78f},{x+r,y-4}},c);
    g.stroke(x-r+tilt,y-h*.7f,x+tilt,y-h,1.1f,mix(c,0xffffff,.55f));
}
void arch(Canvas&g,float x,float base,float span,float h,Color c){
    float y=base-h,r=span*.5f,th=span*.1f,cy=y+r;
    for(int i=0;i<14;++i){
        float a=3.141593f+i*3.141593f/14,b=3.141593f+(i+1)*3.141593f/14;
        auto at=[&](float t,float rad){return Point{x+r+rad*std::cos(t),cy+rad*std::sin(t)};};
        g.polygon({at(a,r),at(b,r),at(b,r-th),at(a,r-th)},mix(c,0xc5c2af,.025f*(i%4)));
        if(i>0){
            auto outer=at(a,r-.5f),inner=at(a,r-th+.25f);
            g.stroke(outer.x,outer.y,inner.x,inner.y,.8f,mix(c,0x162037,.35f));
        }
    }
    for(float xx:{x,x+span-th}){
        g.box(xx,cy,th,base-cy,c);
        g.box(xx-2,cy,th+4,4,mix(c,0xc8c1aa,.11f));
        g.box(xx-3,base-7,th+6,7,c);
        for(float yy=cy+18;yy<base;yy+=18)g.box(xx,yy,th,1,mix(c,0x182b35,.18f));
        g.stroke(xx+3,cy+6,xx+3,base-11,1.3f,mix(c,0xd2cec0,.09f));
    }
}

struct Run {int begin,end;};
struct Layer {
    int width=0,height=0;
    std::vector<unsigned char> pixels;
    std::vector<std::vector<Run>> bands;
    void reset(int w,int h){width=w;height=h;pixels.assign(size_t(w)*h*3,0);bands.assign(h,{});}
    void prepare(){
        for(int row=0;row<height;++row){
            auto p=&pixels[size_t(row)*width*3];
            int mark=-1;
            for(int x=0;x<width;++x){bool visible=p[3*x]||p[3*x+1]||p[3*x+2];if(visible&&mark<0)mark=x;else if(!visible&&mark>=0){bands[row].push_back({mark,x});mark=-1;}}
            if(mark>=0)bands[row].push_back({mark,width});
        }
    }
    void overlay(std::vector<unsigned char>&rgb,int out,int offset)const{
        int shift=wrap(offset,width);
        for(int y=0;y<height;++y)for(int origin=-shift;origin<out;origin+=width){
            const auto* src=pixels.data()+size_t(y)*width*3;auto*dst=rgb.data()+size_t(y)*out*3;
            for(auto run:bands[y]){int a=std::max(run.begin,-origin),b=std::min(run.end,out-origin);if(b>a)std::memcpy(dst+(origin+a)*3,src+a*3,size_t(b-a)*3);}
        }
    }
};

void patina(Layer&l,const Palette&p,int j,int stage){
    // Tonal depth and low mist are baked into each band and travel at its own
    // parallax speed. Feathered pools vary continuously across the wrap seam.
    std::vector<float> ridge(l.width),amount(l.width);
    for(int x=0;x<l.width;++x){
        float u=x*Tau/l.width;
        ridge[x]=j==0?392.f:485.f+j*39;
        ridge[x]+=19*std::sin(u*3+j+stage*.12f)+9*std::sin(u*7-j*.43f);
        amount[x]=.47f+.53f*(.5f+.5f*std::sin(u*2+j*1.9f));
    }
    for(int y=0;y<l.height;++y){
        float yy=y*720.f/l.height;
        
        for(int x=0;x<l.width;++x){
            auto*q=&l.pixels[(size_t(y)*l.width+x)*3];if(!(q[0]|q[1]|q[2]))continue;
            float v=(yy-ridge[x])/(j==0?95.f:53.f);
            float fog=std::max(0.f,1.f-v*v);
            fog*=fog*amount[x]*(j==0?.22f:j==4?.16f:.32f);
            float shadow=std::clamp((yy-ridge[x]-22.f)/136.f,0.f,1.f)*.27f;
            for(unsigned k=0;k<3;++k){float c=q[k];c+=(float((p.mist>>(16-8*k))&255)-c)*fog;
                c+=(float((p.deep>>(16-8*k))&255)-c)*shadow;q[k]=static_cast<unsigned char>(c);}
        }
    }
}

// Join the paintings through a mountain pass by blending both the skyline height
// and the rock beneath it. The last pass joins the first at the parallax wrap.
struct RidgePainting {
    const highlands::Image& art;
    struct Edge { int top, bottom; };
    std::vector<Edge> edges;
    explicit RidgePainting(const highlands::Image& source) : art(source), edges(source.width) {
        for(int x=0;x<art.width;++x) {
            int top=0, bottom=art.height-1;
            while(top<=bottom && (art.rgba[top*art.width+x]&255)<64) ++top;
            while(bottom>top && (art.rgba[bottom*art.width+x]&255)<64) --bottom;
            edges[x]={top,bottom};
        }
    }
};
struct RidgeSlice {
    const RidgePainting* painting;
    int x, first, last;
    float top, bottom;
    unsigned sample(float depth) const {
        const int sy=first+int(depth*(last-first));
        return painting->art.rgba[sy*painting->art.width+x];
    }
};
void ridges(Canvas& g,const Palette& p,float period,unsigned seed,bool foreground) {
    static const RidgePainting alpine(highlands::ridge), rolling(highlands::foothill);
    constexpr float overlap=.24f;
    constexpr int pieces=3;
    const float stride=period/pieces;
    const float phase=noise(seed+(foreground?73:9))*period;

    auto slice=[&](int index,float u){
        index=wrap(index,pieces);
        const unsigned id=seed+index*173+(foreground?349u:0u);
        const auto& painting=(foreground ? index==2 : index!=1) ? alpine : rolling;
        // Different shoulders and heights break the silhouette repetition without
        // reversing the directional light painted onto the rock faces.
        const bool flank=foreground ? index==0 : index==2;
        const float crop=flank ? .54f+.08f*noise(id+11) : .85f+.13f*noise(id+11);
        const float begin=flank ? (foreground?.02f:.35f) : (1.f-crop)*noise(id+91);
        const int x=std::clamp(int((begin+u*crop)*painting.art.width),0,painting.art.width-1);
        const auto edge=painting.edges[x];
        const float height=(foreground?104.f:157.f)+(foreground?64.f:76.f)*noise(id+47);
        const float base=(foreground?495.f:441.f)+15.f*noise(id+3);
        int top=std::min(edge.top,painting.art.height-1),bottom=std::max(top,edge.bottom);
        return RidgeSlice{&painting,x,top,bottom,base-height+height*top/painting.art.height,base};
    };

    const Color shadow=foreground?mix(p.deep,p.distant,.36f):mix(p.distant,p.upper,.22f);
    const Color snow=foreground?mix(p.distant,p.mist,.60f):mix(p.mist,0xe4dfe1,.42f);
    const Color haze=foreground?mix(p.distant,p.deep,.30f):mix(p.distant,p.mist,.14f);
    std::array<std::array<unsigned char,256>,3> grading{};
    for(int v=0;v<256;++v){
        const Color graded=mix(shadow,snow,v/255.f);
        for(int c=0;c<3;++c)grading[c][v]=static_cast<unsigned char>(graded>>(16-c*8));
    }
    const int fillFrom=g.Y(foreground?515.f:461.f);
    g.box(0,foreground?515.f:461.f,period,280,haze);
    for(int px=0;px<g.width;++px){
        const float position=((px+.5f)/g.zoom+phase)/stride;
        const int i=int(std::floor(position));const float t=position-i;
        const auto entering=slice(i,t/(1.f+overlap));
        const bool pass=t<overlap;
        const auto leaving=pass?slice(i-1,(1+t)/(1.f+overlap)):entering;
        const float f=std::min(1.f,t/overlap),blend=f*f*(3.f-2.f*f);
        const int weight=int(blend*256);
        const float top=(leaving.top+(entering.top-leaving.top)*blend)*g.zoom;
        const float base=(leaving.bottom+(entering.bottom-leaving.bottom)*blend)*g.zoom;
        const float inverse=1.f/std::max(1.f,base-top);
        for(int y=std::max(0,int(std::ceil(top-.5f)));y<std::min(g.height,fillFrom);++y){
            const float depth=std::clamp((y+.5f-top)*inverse,0.f,1.f);
            const unsigned a=entering.sample(depth), b=pass?leaving.sample(depth):a;
            // The foot of each massif disappears into the same valley mist.
            const int fog=int(256.f*std::clamp((depth-.64f)/.36f,0.f,1.f));
            auto* dst=&g.rgb[(size_t(y)*g.width+px)*3];
            for(int c=0;c<3;++c){
                const int shift=24-c*8;
                const unsigned sample=(((a>>shift)&255)*weight+((b>>shift)&255)*(256-weight))>>8;
                const int mist=(haze>>(16-8*c))&255;
                dst[c]=static_cast<unsigned char>((grading[c][sample]*(256-fog)+mist*fog)>>8);
            }
        }
    }
}
// Distant underground formations sweep along the scene as one continuous wall.
// Both silhouettes and their mineral bands meet at the edges of the repeating layer.
void basin(Canvas& g,const Palette& p,bool submerged,float width,unsigned seed) {
    const Color abyss=submerged?0x133a4c:0x242138;
    for (int band=0;band<3;++band) {
        const int samples=164;
        auto crest=[&](int i){
            const float t=wrap(i,samples)*Tau/samples;
            const float sway=std::sin(t*2.f+band*.8f+seed*.002f);
            const float swell=std::cos(t*3.f+band*1.3f);
            const float grit=std::sin(t*7.f+band)*6+std::sin(t*17.f+band)*2;
            return (submerged?369.f:394.f)+band*57+(submerged?42.f:60.f)*sway+22*swell+grit;
        };
        Color stone=mix(submerged?0x597e85:0x586279,abyss,.31f+band*.23f);
        for(int i=0;i<samples;++i) {
            float x=width*i/samples,x1=width*(i+1)/samples,top=crest(i),top1=crest(i+1);
            g.polygon({{x,top},{x1,top1},{x1,720},{x,720}},stone);
            for(int seam=0;seam<5;++seam){
                const float d=seam*seam*2.7f+seam*8;
                auto shelf=[&](int k){return 3.f+5*std::sin(wrap(k,samples)*Tau/samples*4+seam*.45f);};
                const float l=top+d+shelf(i),r=top1+d+shelf(i+1);
                const float thickness=seam==0?3.4f:1.5f;
                g.polygon({{x,l},{x1,r},{x1,r+thickness},{x,l+thickness}},
                          mix(stone,p.mist,seam==0?.17f:.078f));
            }
        }
    }
}

void beacon(Canvas&g,float x,float base,const Palette&p){
    const auto& lamp=highlands::wayside_lantern;
    constexpr int h=142;
    const int w=std::max(1,int(std::lround(float(h)*lamp.width/lamp.height)));
    stampScenery(g,lamp,int(std::lround(x))-w/2,int(std::lround(base))-h,w,h,
                 mix(p.upper,p.distant,.40f),70);
}

// A ribbed window is architecture behind the path, never a walkable shelf.
void lancet(Canvas&g,float x,float base,float w,float h,Color masonry,Color glass){
    float tip=base-h,shoulder=tip+w*.76f;
    auto aperture=[&](float inset,Color c) {
        float half=w*.5f-inset;
        g.polygon({{x-half,base},{x-half,shoulder+inset},{x-half*.76f,tip+w*.43f+inset},
                   {x,tip+inset*1.5f},{x+half*.76f,tip+w*.43f+inset},
                   {x+half,shoulder+inset},{x+half,base}},c);
    };
    aperture(0,mix(masonry,glass,.31f));aperture(4.5f,mix(masonry,0x151626,.48f));
    aperture(8,mix(glass,masonry,.47f));
    aperture(10,glass);
    g.stroke(x-w*.5f+8,base-3,x-w*.5f+8,shoulder+8,2.5f,mix(masonry,0x101624,.52f));
    g.stroke(x+w*.5f-7,base-3,x+w*.5f-7,shoulder+8,1.5f,mix(masonry,glass,.63f));
    g.stroke(x,base-1,x,tip+20,3,masonry);
    for(int side:{-1,1}){
        g.stroke(x,shoulder+25,x+side*w*.30f,shoulder+4,3,masonry);
        g.stroke(x+side*w*.30f,shoulder+4,x,tip+19,2,masonry);
        g.stroke(x+side*w*.25f,base-2,x+side*w*.25f,shoulder+10,2,masonry);
    }
    for(float yy=shoulder+30;yy<base-5;yy+=21)g.stroke(x-w*.5f+7,yy,x+w*.5f-7,yy,1.3f,masonry);
}
void sanctum(Canvas&g,const Palette&p,float width,unsigned seed){
    // One curtain wall ties the chapel, watchtower and gallery bays together.
    // Its eaves, stone courses and piers all continue behind the foreground ruins.
    const Color stone=mix(p.deep,0x625464,.50f),face=mix(stone,p.horizon,.27f);
    const Color joint=mix(face,p.deep,.25f),edge=mix(face,p.mist,.20f);
    const Color glass=mix(p.horizon,p.moon,.23f),roof=mix(stone,p.deep,.24f);
    constexpr float bays[]={0,.21f,.325f,.57f,.765f,1};
    constexpr int profile[]={1,2,0,3,0};
    float sill=469.f;
    g.box(0,343,width,720-343,face);
    for(int i=0;i<5;++i){
        const float left=bays[i]*width,right=bays[i+1]*width,span=right-left,cx=(left+right)/2;
        const int kind=profile[i];
        const float eave=kind==2?241.f:kind==1?314.f:kind==3?293.f:332.f;
        const float peak=eave-(kind==2?46.f:kind==1?78.f:54.f);
        const Color bay=mix(face,stone,.04f+.10f*noise(seed+i));
        g.box(left,eave,span,720-eave,bay);
        if(kind==1 || kind==3){
            const float shoulder=kind==3?span*.23f:0;
            g.polygon({{left,eave},{cx-shoulder,peak},{cx+shoulder,peak},{right,eave}},roof);
            g.stroke(left,eave,cx-shoulder,peak,2.5f,edge);
            g.stroke(cx+shoulder,peak,right,eave,2.1f,joint);
            g.stroke(cx-shoulder,peak,cx+shoulder,peak,2.2f,edge);
        }else{
            const int merlons=kind==2?8:13;
            const float step=span/merlons;
            for(int n=0;n<merlons;++n){
                g.box(left+n*step,eave-8,step*.5f,9,bay);
                g.box(left+n*step,eave-8,step*.5f,1.2f,edge);
            }
        }
        g.box(left,eave,span,5,mix(bay,p.deep,.38f));
        g.box(left,eave+5,span,2,edge);
        // Buttress foundations disappear into the bank instead of stopping in mid-air.
        g.polygon({{left,eave+7},{left+8,eave+7},{left+9,sill-45},{left+19,610},{left,610}},joint);
        g.box(left+1,eave+8,2,170,edge);
        for(int course=0;course<7;++course){
            const float y=eave+14+course*23;
            g.box(left+9,y,span-13,1,mix(bay,joint,.6f));
            const float step=span/5;
            for(int n=0;n<5;++n){
                float x=left+(n+(course%2?.33f:.75f))*step;
                g.box(x,y,1,23,mix(bay,joint,.6f));
            }
        }
        const int panes=kind==2?2:kind==1?3:4;
        for(int n=0;n<panes;++n){
            const float x=left+(n+.5f)*span/panes;
            const bool center=kind==1 && n==1;
            const float w=center?56.f:kind==2?33.f:34.f+4*noise(seed+n+i);
            const float h=center?147.f:std::min(sill-eave-18.f,87.f+26*noise(seed+i*17+n));
            lancet(g,x,sill,w,h,joint,mix(glass,bay,.29f+.23f*noise(seed+i*71+n)));
            g.box(x-w/2-4,sill,w+8,3,edge);
            g.box(x-w/2-2,sill+3,w+4,4,joint);
        }
        if(kind==2){
            for(int n=-1;n<=1;++n)lancet(g,cx+n*29,eave+62,22,40,joint,mix(glass,bay,.58f));
        }
    }
}

void grottoCeiling(Canvas&g,float width,const Palette&p,unsigned seed){
    constexpr int teeth=68;
    auto boundary=[&](int i) {
        int k=wrap(i,teeth);
        return 88.f+45*std::cos(k*Tau/teeth*3)+17*std::sin(k*Tau/teeth*9)+noise(seed+k*49)*9;
    };
    const float step=width/teeth;
    for(int i=0;i<teeth;++i) {
        float x=i*step,l=boundary(i),r=boundary(i+1);
        float taper=noise(seed+unsigned(i)*13);
        float tooth=std::max(l,r)+10+55*taper*taper;
        Color rock=mix(0x22263e,p.upper,.24f+noise(seed+i)*.22f);
        g.polygon({{x,0},{x+step,0},{x+step,r},{x+step*.70f,r+4},
                   {x+step*.43f,tooth},{x+step*.27f,l+4},{x,l}},rock);
        g.polygon({{x+step*.66f,r-7},{x+step*.70f,r+4},{x+step*.43f,tooth},{x+step*.46f,r+5}},mix(rock,p.moss,.17f));
    }
}
void grove(Canvas&g,const Scenery&s,const Palette&p,int row,float width,unsigned seed){
    const bool cave=s.area==2,shrine=s.area==3,water=s.area==0;
    float base=495.f+row*59;Color hue=mix(p.distant,p.deep,(row+1)*.25f);
    const bool forest=s.area==1;
    // Painted crowns are wider than individual distant spires. Give the
    // closest trees space and repeat enough neighbours to wrap their crowns.
    const int count=forest?(54-row*11):cave?34:(water?45:72-row*13);
    const int spill=forest?4:2;
    for(int k=-spill;k<count+spill;++k){
        int i=wrap(k,count);unsigned n=seed+i*67+row*131;
        float spacing=width/count,x=(k-.14f+1.28f*noise(n))*spacing,phase=i*Tau/count;
        float ground=base+17*std::sin(phase*3+row);
        float cluster=.52f+.48f*std::cos(phase*(4-row%2)+row);
        float h=25.f+row*10+(80.f+row*61)*cluster*(.32f+noise(n+2)*.68f);
        if(cave){
            const auto rock=mix(hue,p.horizon,noise(n+17)*.09f);
            float tip=x+(noise(n+2)-.5f)*12,foot=5+h*.15f,shaft=h*(row==0?.40f:.66f);
            g.polygon({{x-foot,ground+14},{x-foot*.65f,ground-shaft*.24f},{tip-3,ground-shaft*.88f},
                       {tip,ground-shaft},{tip+3,ground-shaft*.69f},{x+foot,ground+10}},rock);
            g.stroke(tip,ground-shaft,tip-1,ground-shaft*.55f,1.6f,mix(rock,p.moss,.17f));
            if(row>=1&&i%4==0){
                const Color stone=mix(p.deep,hue,.63f);
                for(int l=0;l<4;++l){
                    float bx=x+l*7-8,ht=(15+19*noise(n+l*5))*(row==3?1.32f:1);
                    crystal(g,bx,ground+4,ht,mix(stone,0x87d7ce,.32f),l*3.f-5);
                }
            }
        }else if(water){
            // Elongated kelp blades have a curved spine, folded light edge and pointed tips.
            for(int j=0;j<(row>0?4:2);++j){
                float bx=x+j*4-5,ht=h*(.52f+.47f*noise(n+j+4)),lean=(noise(n+j+79)-.5f)*35;
                auto spine=[&](float t){return bx+lean*t+std::sin(t*5.5f+n%17)*ht*.085f*t;};
                Color blade=mix(hue,0x376863,noise(n+j+43)*.31f);
                for(int k=0;k<9;++k){
                    float t=k/9.f,u=(k+1)/9.f,xx=spine(t),next=spine(u),yy=ground+6-t*ht,ny=ground+6-u*ht;
                    float a=.7f+5.6f*std::sin(t*3.14f),b=.6f+5.6f*std::sin(u*3.14f);
                    g.polygon({{xx-a,yy},{next-b,ny},{next+b,ny},{xx+a,yy}},blade);
                    if(row>=2)g.stroke(xx+a*.5f,yy,next+b*.5f,ny,1.2f,mix(blade,p.moss,.25f));
                }
            }
        }else if(shrine){
            if(row<3&&i%9==1){
                float ht=std::min(h*1.25f,base-100.f),w=36.f+14*row;
                arch(g,x-w*.5f,ground+5,w,ht,hue);
            }else{
                float tall=h*(.25f+.2f*noise(n+15)),wide=6+10*noise(n+77);
                g.polygon({{x-wide,ground+9},{x-wide*.7f,ground-tall*.85f},{x-2,ground-tall},
                           {x+wide,ground-tall+6},{x+wide,ground+6}},hue);
                g.stroke(x-2,ground-tall,x-2,ground-3,1.4f,mix(hue,p.moss,.20f));
            }
        }else if(chapter(s)==1&&i%5==0&&row>0){
            arch(g,x-22,ground+5,50+row*17,h*.88f,hue);
        }else if(chapter(s)==2&&row>1&&i%3==0){
            g.polygon({{x-4,ground+4},{x-2,ground-h*.71f},{x+5,ground-h*.72f},{x+7,ground+4}},hue);
            for(int l=-3;l<=3;++l){float u=float(l)/3;
                g.oval(x+u*h*.16f,ground-h*(.74f-.1f*std::abs(u)),h*.10f,h*.12f,hue);
                g.stroke(x+u*h*.22f,ground-h*.72f,x+u*h*.2f,ground-h*(.46f+.14f*noise(n+l)),1.8f,hue);
            }
        }else {
            const Color haze=mix(hue,p.distant,.02f+.15f*noise(n+21));
            forestTree(g,x,ground+4,h,haze,n,row);
            if(row>=2 && noise(n+44)>.66f)
                forestTree(g,x+spacing*.43f,ground+5,h*.46f,hue,n+978,row);
        }
    }
    if(shrine&&row==2){
        for(float x:{width*.19f,width*.68f})
            arch(g,x,base+28,150,235+noise(seed+int(x))*36,mix(hue,p.deep,.19f));
    }
    // Keep the amber window in the closest grove. Middle-distance crowns
    // otherwise cover it just as the player first approaches the landmark.
    if(row==3&&forest){
        float bx=width*(.39f+noise(seed+11)*.19f);
        if(chapter(s)==4 || chapter(s)==7)bx+=width*.12f;
        const float bank=base+17*std::sin(bx/width*Tau*3+row)-5;
        beacon(g,bx,bank+3,p);
    }
    for(int i=0;i<96;++i){
        float a=width*i/96.f,b=width*(i+1)/96.f;
        float y1=base+17*std::sin(i*Tau/96.f*3+row)-5,y2=base+17*std::sin((i+1)*Tau/96.f*3+row)-5;
        g.polygon({{a,y1},{b,y2},{b,720},{a,720}},hue);
    }
    if(row==2){
        constexpr int gardens=14;
        for(int k=-1;k<=gardens;++k){
            const int n=wrap(k,gardens);const unsigned id=seed+n*793;
            const float x=(k+.18f+.49f*noise(id+3))*width/gardens;
            const float ground=base+17*std::sin((x/width)*Tau*3+row)-5;
            const auto dark=mix(hue,p.distant,.065f),leafLight=mix(hue,p.moss,.38f);
            // Root the distant specimens in the contour of the bank, leaving a few
            // blades crossing their base to tie separate silhouettes into one bed.
            const float anchor=ground+2.5f;
            g.oval(x+4,anchor,17.f,3.8f,mix(hue,dark,.55f));
            if(water){
                specimen(g,n%3==0?Sprite::Sponge:Sprite::Coral,x,anchor,17+8*noise(id+2),dark,leafLight,.94f,n%2);
                specimen(g,Sprite::Seaweed,x+9,anchor+.8f,21+7*noise(id+17),dark,leafLight,.96f,n%3==0);
            }else if(cave){
                specimen(g,n%4==0?Sprite::Geode:Sprite::Crystal,x,anchor,16+7*noise(id+7),dark,leafLight,.92f,n%2);
            }else if(shrine){
                if(n%2==1)specimen(g,n%4==1?Sprite::Relic:Sprite::Urn,x,anchor,18+7*noise(id+5),dark,leafLight,.92f);
            }else{
                specimen(g,Sprite::Fern,x,anchor,16+9*noise(id+7),dark,leafLight,.94f,n%2);
                if(n%3==0)specimen(g,Sprite::Bluebell,x+10,anchor+1,12+5*noise(id+1),dark,leafLight,.94f);
            }
            if(water||s.area==1){
                for(int j=0;j<9;++j){
                    float bx=x-16+j*4,tip=4+noise(id+j*13)*6;
                    g.stroke(bx,anchor+2,bx+(noise(id+81+j)-.5f)*5,anchor-tip,1.35f,mix(hue,leafLight,.40f));
                }
            }
        }
    }
}
void sky(Canvas&g,const Scenery&s,const Palette&p,float w,unsigned seed){
    for(int y=0;y<g.height;++y){
        float v=(y+.5f)/g.height;
        Color c=v<.34f?mix(p.zenith,p.upper,v/.34f):v<.75f?mix(p.upper,p.horizon,(v-.34f)/.41f):mix(p.horizon,p.mist,(v-.75f)/.25f);
        g.span(y,0,g.width,c);
    }
    const bool exterior=s.area==1;
    const float mx=w*(.64f+.08f*noise(seed)),my=118+20*noise(seed+7);
    if(exterior){
        g.glow(mx,my,178,p.moon,.27f);g.glow(w*.25f,439,250,p.mist,.24f);
        for(int i=0;i<98;++i){
            float x=noise(seed+i*5)*w,y=22+noise(seed+i*5+1)*235;
            float z=.5f+noise(seed+i*5+2)*.9f;
            Color star=mix(p.upper,0xfff6d4,.4f+.42f*noise(i+67));
            g.oval(x,y,z,z,star);if(i%18==0){g.stroke(x-z*3,y,x+z*3,y,.7f,star);g.stroke(x,y-z*4,x,y+z*4,.7f,star);}
        }
        const int diameter=78;
        g.glow(mx,my,67,p.moon,.20f);
        stampScenery(g,highlands::moon,int(std::round(mx-diameter*.5f)),
                     int(std::round(my-diameter*.5f)),diameter,diameter,p.moon,56);
    }else if(s.area==0){
        g.glow(w*.6f,-22,355,0xc6f1d4,.6f);
        for(int i=0;i<9;++i){
            float sx=w*noise(seed+13*i), length=220+90*noise(seed+i+53);
            for(int row=0;row<g.Y(length);++row){
                float y=(row+.5f)/g.zoom,depth=1-y/length,center=sx-y*.33f;
                float half=2.5f+y*.058f;
                int begin=std::max(0,g.X(center-half)),end=std::min(g.width,g.X(center+half));
                for(int x=begin;x<end;++x){
                    float side=std::max(0.f,1-std::abs((x+.5f)/g.zoom-center)/half);
                    float amount=.13f*side*side*depth*depth;
                    auto dst=&g.rgb[(size_t(row)*g.width+x)*3];
                    // Screen a soft shaft onto the actual water colour, even inside the surface glow.
                    for(int ch=0;ch<3;++ch){
                        float light=float((0xb1f2deu>>(16-8*ch))&255)/255.f;
                        dst[ch]=static_cast<unsigned char>(dst[ch]+(255-dst[ch])*amount*light);
                    }
                }
            }
        }
    }else{
        if(s.area==2){
            // Shafts of skylight through fissures, with the cave roof painted over their source.
            for(int shaft=0;shaft<4;++shaft){
                const float source=w*(.1f+shaft*.28f)+noise(seed+shaft*7)*30;
                const float length=340+95*noise(seed+shaft),tilt=.15f+noise(seed+shaft*31)*.30f;
                for(int row=0;row<std::min(g.height,g.Y(length));++row){
                    float y=row/g.zoom,t=y/length,cx=source-tilt*y,spread=3+29*t;
                    for(int x=std::max(0,g.X(cx-spread));x<std::min(g.width,g.X(cx+spread));++x){
                        float fall=1.f-std::abs(x/g.zoom-cx)/spread;
                        float opacity=std::max(0.f,fall)*(.26f*(1-t)*(1-t));
                        auto*p=&g.rgb[(size_t(row)*g.width+x)*3];
                        for(int ch=0;ch<3;++ch)p[ch]=static_cast<unsigned char>(p[ch]+(239-p[ch])*opacity);
                    }
                }
            }
            g.glow(w*.53f,346,184,0x83c3c9,.35f);
        }else g.glow(w*.53f,338,220,0xe7a26c,.38f);
    }

    // The curtains remain fixed behind the drifting high clouds.
    if(exterior){
        const bool aurora=chapter(s)==1||chapter(s)==6;
        if(aurora){
            for(int x=0;x<g.width;++x){
                float xx=x/g.zoom,shape=std::sin(xx*.007f+.3f*s.level);
                float crest=106.f+33.f*shape+12.f*std::sin(xx*.021f),depth=24.f+19.f*(1+std::sin(xx*.009f));
                float ripple=.5f+.5f*std::sin(xx*.067f+2.3f*shape);
                for(int yy=std::max(0,g.Y(crest-depth));yy<std::min(g.height,g.Y(crest+depth));++yy){
                    float d=1.f-std::abs(yy/g.zoom-crest)/depth;
                    float a=std::max(0.f,d)*(.035f+.14f*ripple);
                    unsigned char*dst=&g.rgb[(size_t(yy)*g.width+x)*3];
                    Color target=mix(0x7fe7bd,0xaf9cd8,.5f+.5f*std::sin(xx*.004f));
                    for(int j=0;j<3;++j)dst[j]=static_cast<unsigned char>(dst[j]+(int((target>>(16-j*8))&255)-dst[j])*a);
                }
            }
        }

    }

}

// Clouds are small, prefiltered RGBA patches. Their integer offsets are part of
// the background cache key, so a paused scene keeps exactly the same sky.
struct SkyCloud {
    int x=0,y=0,width=0,height=0;
    bool high=false;
    std::vector<std::uint32_t> pixels;
    void paint(std::vector<unsigned char>& sky,int screenWidth,int screenHeight,int offset,int period)const {
        const int center=wrap(x+offset,period);
        for(int left=center-width/2-period;left<screenWidth;left+=period){
            const int begin=std::max(0,-left),end=std::min(width,screenWidth-left);
            if(begin>=end)continue;
            for(int row=std::max(0,-y);row<std::min(height,screenHeight-y);++row){
                auto* dst=sky.data()+(size_t(row+y)*screenWidth+left+begin)*3;
                const auto* src=pixels.data()+size_t(row)*width+begin;
                for(int col=begin;col<end;++col,++src,dst+=3){
                    const unsigned c=*src,a=c&255;
                    if(!a)continue;
                    // Channels are premultiplied when the cloud is cached.
                    dst[0]=static_cast<unsigned char>((c>>24)+(dst[0]*(255-a)+127)/255);
                    dst[1]=static_cast<unsigned char>(((c>>16)&255)+(dst[1]*(255-a)+127)/255);
                    dst[2]=static_cast<unsigned char>(((c>>8)&255)+(dst[2]*(255-a)+127)/255);
                }
            }
        }
    }
};

std::vector<SkyCloud> skyClouds(const Scenery& s,const Palette& pal,int w,int h,unsigned seed) {
    std::vector<SkyCloud> patches;
    if(s.area!=1)return patches;
    const float scale=h/720.f;
    // Keep angular sizes stable across monitor formats; only the field of view grows.
    const int period=w+int(320*scale);
    const int count=std::max(3,int(std::round(period/(330*scale))));
    for(int i=0;i<count;++i){
        const unsigned n=seed+373*i;
        const bool high=i%3==1;
        const bool bank=!high && (i%3==2 || i%6==3);
        const auto& source=high?highlands::cirrus:bank?highlands::stratus:highlands::cloud;
        SkyCloud patch;
        patch.high=high;
        patch.x=int((i+.25f+.28f*noise(n+9))*period/count);
        patch.y=int(scale*((high?30.f:99.f)+(high?60.f:90.f)*noise(n+5)));
        const float maxW=(high?245.f:bank?280.f:198.f)+noise(n+7)*103.f;
        const float maxH=(high?47.f:65.f)+noise(n+21)*18.f;
        const float z=std::min(maxW/source.width,maxH/source.height)*scale;
        patch.width=std::max(1,int(source.width*z+.5f));
        patch.height=std::max(1,int(source.height*z+.5f));
        patch.pixels.resize(size_t(patch.width)*patch.height);
        const unsigned opacity=(s.night?103:132)+(high?0:bank?-15:18);
        const bool flip=i%2!=0;
        const auto vapor=mix(pal.upper,pal.mist,high?.49f:.39f);
        for(int y=0;y<patch.height;++y)for(int x=0;x<patch.width;++x){
            const float u=(x+.5f)/patch.width;
            auto sample=samplePainting(source,flip?1.f-u:u,(y+.5f)/patch.height,1);
            const unsigned a=((sample&255)*opacity+127)/255;
            const auto color=mix(sample>>8,vapor,high?.30f:.22f);
            auto pm=[&](int k){return (((color>>k)&255)*a+127)/255;};
            patch.pixels[size_t(y)*patch.width+x]=pm(16)<<24|pm(8)<<16|pm(0)<<8|a;
        }
        patches.push_back(std::move(patch));
    }
    return patches;
}

struct Landscape {
    // Prerasterized repeating bands let one camera step copy scanlines instead of repainting trees.
    int key=-1,width=0,height=0,lastCamera=std::numeric_limits<int>::min();
    std::array<Layer,5> layers;
    std::vector<unsigned char> skyPixels, composite;
    std::vector<SkyCloud> clouds;
    int lastHigh=0,lastLow=0;
    void reset(const Scenery&s,int w,int h,int k){
        key=-1; // A partially rebuilt set must be retried if painting throws.
        const auto pal=colors(s);unsigned sd=906+chapter(s)*853+s.level*128+s.area*43;
        skyPixels.resize(size_t(w)*h*3);float scale=h/720.f;
        // Integer period (in output pixels) makes wrap and the last crest share a seam.
        int period=int(1728*scale);float span=period/scale;
        auto paintBand=[&](int j){
            auto&l=layers[j];l.reset(period,h);
            Canvas gc{l.pixels,period,h,scale};
            if(j==0){
                if(s.area==1)ridges(gc,pal,span,sd,false);
                else if(s.area==3)sanctum(gc,pal,span,sd);
                else {
                    basin(gc,pal,s.area==0,span,sd);
                    if(s.area==2)grottoCeiling(gc,span,pal,sd);
                }
            }
            else {
                if(j==1 && s.area==1)ridges(gc,pal,span,sd,true);
                grove(gc,s,pal,j-1,span,sd);
            }
            patina(l,pal,j,chapter(s));
            l.prepare();
        };
        // The first visible frame needs all five planes in separate buffers.
        auto paintPair=[&](int j){paintBand(j);paintBand(j+3);};
        std::array<std::future<void>,2> jobs;
        if(std::thread::hardware_concurrency()>=4){
            for(int j=0;j<2;++j){
                try {
                    jobs[j]=std::async(std::launch::async,[&,j]{paintPair(j);});
                } catch(const std::exception&){
                    // Submission failed. Finish every unsubmitted pair on the caller.
                    break;
                }
            }
        }
        Canvas skyCanvas{skyPixels,w,h,scale};
        sky(skyCanvas,s,pal,w/scale,sd);
        clouds=skyClouds(s,pal,w,h,sd);
        paintBand(2);
        for(int j=0;j<2;++j){
            if(jobs[j].valid())jobs[j].get();
            else paintPair(j);
        }
        width=w;height=h;key=k;lastCamera=std::numeric_limits<int>::min();
    }
    void render(std::vector<unsigned char>&rgb,int w,int h,const Scenery&s){
        if(w<=0||h<=0)return;
        int k=chapter(s)*64+(s.level&7)*8+(s.area&3)*2+int(s.night);
        if(k!=key||w!=width||h!=height)reset(s,w,h,k);
        const int period=w+int(320*(h/720.f));
        // Advance by whole output pixels. Take the modulus in 64 bits so long
        // saves resume at their exact phase and the layer never jumps on wrap.
        const int high=clouds.empty()?0:int((s.frame/41)%period)-int(std::lround(s.camera*.013f*h/240.f));
        const int low=clouds.empty()?0:int((s.frame/23)%period)-int(std::lround(s.camera*.031f*h/240.f));
        if(lastCamera!=s.camera || high!=lastHigh || low!=lastLow){
            composite=skyPixels;
            for(const auto& cloud:clouds)cloud.paint(composite,w,h,cloud.high?high:low,period);
            lastHigh=high;lastLow=low;
            constexpr float drift[]={.058f,.118f,.215f,.37f,.56f};
            for(int j=0;j<5;++j)layers[j].overlay(composite,w,int(std::round(s.camera*(h/240.f)*drift[j])));
            lastCamera=s.camera;
        }
        rgb=composite;const auto pal=colors(s);Canvas gc{rgb,w,h,h/720.f};float span=w/gc.zoom;
        float time=float(s.frame%216000)/60.f;
        unsigned sd=chapter(s)*157+s.level*29;bool water=s.area==0,shrine=s.area==3;
        if(water){
            // One distant shoal drifts as a loose group, with a single forked-tail silhouette per fish.
            for(int i=0;i<10;++i){
                float school=std::fmod(160+time*5.5f-s.camera*.30f,span+280);
                if(school<0)school+=span+280;
                float fx=school-124+noise(sd+i*39)*102;
                float fy=256+noise(sd+i*21+17)*37+std::sin(time*.65f+i)*2;
                float body=2.5f+3.8f*noise(sd+i*91+5);
                Color fish=mix(pal.upper,pal.deep,.47f);
                gc.oval(fx,fy,body,body*.32f,fish);
                gc.polygon({{fx-body*.58f,fy},{fx-body*1.44f,fy-body*.48f},{fx-body*1.40f,fy+body*.48f}},fish);
            }
        }
        for(int i=0;i<35;++i){
            unsigned j=sd+i*11;float depth=.2f+.65f*noise(j);
            float x=std::fmod(noise(j+8)*2390+time*(water?2:6)*depth-s.camera*depth,span+70);if(x<0)x+=span+70;x-=30;
            float ypos=290+noise(j+3)*375;
            if(water||shrine) {
                const float advance=std::fmod(time*(12+depth*8),780.f);
                ypos=std::fmod(1560+noise(j+3)*970-advance,780.f);
            }
            float y=ypos+std::sin(time*.65f+i)*8;
            float pulse=.5f+.5f*std::sin(time*1.9f+j);
            Color hue=water?0x9ad9d1:shrine?0xf6aa60:pal.jewel;
            if(water){gc.ring(x,y,1+2.7f*depth,.85f,mix(pal.horizon,hue,.52f));}
            else if(shrine){
                if(y>384 && i%2){float taper=.7f+noise(j+1)*.75f;
                    gc.stroke(x,y,x+taper,y+2.2f,1.f,mix(pal.deep,hue,.35f+.5f*pulse));}
            }else if(i%4==0){gc.glow(x,y,8,hue,.38f*pulse);gc.oval(x,y,.75f+pulse,.75f+pulse,mix(pal.moss,hue,.75f));}
            else {float d=1+noise(j+2)*2;gc.polygon({{x-d,y},{x,y-d*.63f},{x+d,y-1},{x+d*.15f,y+d*.69f}},mix(pal.moss,hue,.32f+pulse*.3f));}
        }
    }
};

bool bulk(unsigned mt) {
    return (mt>=0x51&&mt<=0x5e)||(mt>=0x61&&mt<=0x63)||mt==0x69||mt==0x88||mt==0x89||mt==0xc0||mt==0xc1||mt==0xc4;
}
void leaf(Canvas&g,float x,float y,float dx,float dy,Color c){
    float nx=-dy*.19f,ny=dx*.19f;
    g.polygon({{x,y},{x+dx*.45f+nx,y+dy*.45f+ny},{x+dx,y+dy},{x+dx*.51f-nx,y+dy*.51f-ny}},c);
}
void rosette(Canvas&g,float x,float y,float r,Color c){
    for(int k=0;k<5;++k){float a=k*Tau/5;g.oval(x+r*.52f*std::cos(a),y+r*.52f*std::sin(a),r*.46f,r*.4f,c);}
    g.oval(x,y,r*.32f,r*.32f,0xf9dc90);
}
void moss(Canvas&g,const Palette&p,unsigned seed,bool underwater,bool decor) {
    g.box(0,0,64,2,p.light);
    for(int i=0;i<8;++i){
        float x=i*8.f,drip=2+noise(seed+i)*5;
        g.polygon({{x,2},{x+8,2},{x+8,4},{x+4,drip},{x,4}},p.moss);
        float gx=x+2+noise(i+seed+7)*4,ht=2+noise(seed+12*i)*5;
        g.stroke(gx,1,gx-1.7f,-ht,1,mix(p.moss,p.light,.41f));
    }
    if(!decor)return;
    float x=21+noise(seed+11)*22;
    if(underwater){
        for(int j=-2;j<=2;++j){
            float ht=8+noise(seed+j+7)*9;
            g.stroke(x,1,x+j*3,-ht*.48f,1.5f,p.moss);
            leaf(g,x+j*3,-ht*.48f,j*1.4f,-ht*.52f,mix(p.moss,p.jewel,.22f));
        }
    }else if(seed%5==0){
        g.stroke(x,0,x+1,-12,1.5f,p.moss);leaf(g,x,-2,-6,-5,p.moss);
        rosette(g,x+1,-12,4.3f,seed&2?0xc4b5df:0xe0daba);
    }else if(seed%5==1){
        for(int j=-2;j<=2;++j){
            float ht=7+noise(seed+j+12)*5;
            g.stroke(x,0,x+j*3,-ht,1.2f,p.moss);
            for(int k=1;k<4;++k){float t=k/4.f;leaf(g,x+j*3*t,-ht*t,-3,-1.7f,p.moss);leaf(g,x+j*3*t,-ht*t,3,-2,mix(p.moss,p.light,.23f));}
        }
    }else if(seed%5==2){
        g.stroke(x,0,x+1,-7,2.9f,p.light);
        g.oval(x+1,-7,6.3f,2.5f,mix(p.moss,p.jewel,.35f));
        g.oval(x+1,-8,5.3f,2.4f,mix(p.moss,0xb6ebd3,.37f));
        g.oval(x-1,-9,1.2f,.7f,p.light);
        g.stroke(x+6,0,x+7,-3,1.6f,p.light);g.oval(x+7,-4,3.8f,1.8f,p.moss);
    }else if(seed%9==4){
        g.oval(x,0,6,3.2f,mix(p.bark,p.moss,.47f));g.stroke(x-3,-1.5f,x,-2.4f,1,p.moss);
    }
}
float smoothNoise(float x,float y,unsigned seed,int periodX,int periodY){
    int ix=int(std::floor(x)),iy=int(std::floor(y));float fx=x-ix,fy=y-iy;
    fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    auto sample=[&](int a,int b){return noise(seed+wrap(a,periodX)*173u+wrap(b,periodY)*1897u);};
    float a=sample(ix,iy),b=sample(ix+1,iy),c=sample(ix,iy+1),d=sample(ix+1,iy+1);
    return (a+(b-a)*fx)*(1-fy)+(c+(d-c)*fx)*fy;
}
// Only the small overlap at the borders is cross-faded. This preserves the
// scale and orientation of the stones in the rest of the soil painting.
Color soilSample(float phaseX,float phaseY,int light) {
    constexpr float overlap=.09f, period=1.f-overlap;
    const float u=(phaseX-std::floor(phaseX))*period;
    const float v=(phaseY-std::floor(phaseY))*period;
    auto horizontal=[&](float y) {
        Color c=samplePainting(highlands::subsoil,u,y,light)>>8;
        if(u<overlap) c=mix(samplePainting(highlands::subsoil,u+period,y,light)>>8,c,u/overlap);
        return c;
    };
    const Color c=horizontal(v);
    return v<overlap ? mix(horizontal(v+period),c,v/overlap) : c;
}
void forestGround(Canvas& g,const Palette& palette,const Scenery& scene,
                  unsigned variant,bool exposed) {
    const int col=int(variant%32), row=int(variant/32);
    const int light=scene.night?4:1;
    for(int x=0;x<64;++x) {
        const float wx=(col*64+x+.5f)/2048.f;
        const float relief=std::sin(Tau*(2*wx+.13f));
        const float soilU=5.f*wx+.012f*relief+.11f;
        const float turfPhase=3.f*wx+.024f*relief;
        const float turfU=.015f+.97f*(1.f-std::abs(2.f*(turfPhase-std::floor(turfPhase))-1.f));
        for(int y=exposed?-10:0;y<64;++y) {
            auto* dst=g.rgb.data()+(size_t(g.Y(float(y)))*g.width+g.X(float(x)))*3;
            if(y>=0) {
                const float wy=(row*64+y+.5f)/256.f;
                const Color soil=soilSample(soilU,wy+.019f*relief+.27f,light);
                const Color shade=mix(soil,palette.deep,.17f);
                dst[0]=shade>>16; dst[1]=shade>>8; dst[2]=shade;
            }
            if(!exposed || y>=33)continue;
            const auto skin=samplePainting(highlands::turf,turfU,(y+10.5f)/43.f,light);
            // Fade the bottom of the sod into the sediment below the root mat.
            const unsigned a=unsigned((skin&255)*std::clamp((32.f-y)/16.f,0.f,1.f));
            if(!a || (y<0 && a<96))continue;
            const Color moss=mix(skin>>8,palette.moss,.15f);
            for(int channel=0;channel<3;++channel){
                const unsigned color=(moss>>(16-channel*8))&255;
                dst[channel]=static_cast<unsigned char>(y<0?color:(color*a+dst[channel]*(255-a)+127)/255);
            }
        }
    }
}
void rootstone(Canvas&g,const Palette&p,unsigned seed,bool top,bool ornaments,bool water,unsigned variant){
    // A 512 x 256 deposit is sampled in absolute world coordinates. Faults and
    // strata continue through cell boundaries, including between adjacent rows.
    const int originX=int(variant%8)*64,originY=int(variant/8)*64;
    const unsigned salt=8703u+unsigned(p.bark);
    constexpr int columns=12,rows=6;
    struct Site {float x,y;unsigned seed;};
    std::array<Site,25> sites{};
    int num=0,firstX=int(std::floor(originX*columns/512.f))-1,firstY=int(std::floor(originY*rows/256.f))-1;
    for(int ly=firstY;ly<firstY+5;++ly)for(int lx=firstX;lx<firstX+5;++lx){
        unsigned h=unsigned(wrap(lx,columns))*61+unsigned(wrap(ly,rows))*719+salt;
        sites[num++]={lx+.1f+.78f*noise(h),ly+.06f+.86f*noise(h+11),h};
    }
    for(int j=0;j<64;++j)for(int i=0;i<64;++i){
        int wx=originX+i,wy=originY+j;
        float coarse=smoothNoise(wx/64.f,wy/64.f,salt,8,4);
        float ripple=smoothNoise(wx/16.f,wy/16.f,salt+777,32,16);
        float xx=wx*columns/512.f+(coarse-.5f)*.35f, yy=wy*rows/256.f+.26f*std::sin(wx*Tau/128.f)+(ripple-.5f)*.14f;
        float best=1e6f,next=1e6f;const Site*closest=&sites.front();
        for(const auto& site:sites){float dx=xx-site.x,dy=yy-site.y;
            // Compress vertical facets to produce shelved, eroded slate.
            float d=dx*dx*.87f+dy*dy*1.3f;
            if(d<best){next=best;best=d;closest=&site;}else if(d<next)next=d;
        }
        float edge=std::clamp((next-best)*20.f,0.f,1.f);
        float broad=.35f+.38f*noise(closest->seed+31)+.17f*coarse+.1f*ripple;
        broad+=.08f*std::clamp(closest->y-yy,-1.f,1.f);
        float grain=noise(unsigned(wx)*13u+unsigned(wy)*553u+salt);
        Color c=mix(mix(p.deep,0x182431,.26f),p.bark,broad*.9f);
        float light=.10f*std::max(0.f,.8f-std::abs(yy-closest->y)*1.7f)*ripple;
        c=mix(c,p.moss,light);
        c=mix(mix(p.deep,0x101e2a,.4f),c,.46f+.54f*edge);
        if(top){float fringe=3+9*coarse+4*ripple;
            if(j<fringe){float f=1.f-j/fringe;c=mix(c,p.moss,.20f+.57f*f);}
        }
        float layers=.5f+.5f*std::sin(wy*.59f+coarse*11+ripple*2.2f);
        int grainValue=int((grain-.5f)*5+std::max(0.f,layers-.74f)*7);
        auto*dst=&g.rgb[(size_t(g.Y(float(j)))*g.width+i)*3];
        for(int ch=0;ch<3;++ch)dst[ch]=static_cast<unsigned char>(std::clamp(int((c>>(16-8*ch))&255)+grainValue,0,255));
    }
    if(!top)return;
    if(ornaments || water){
        moss(g,p,seed,water,ornaments);
    }else{
        g.box(0,0,64,3,mix(p.moss,p.light,.28f));
        g.box(0,3,64,2,mix(p.bark,p.moss,.58f));
        float seam=12+noise(seed+3)*41;
        g.stroke(seam,0,seam-2,5,1.5f,p.deep);
    }
    if(ornaments&&variant%4==1){
        float x=9+noise(seed+17)*40,d=12+noise(seed+7)*16;
        g.stroke(x,3,x+3,10,1.75f,mix(p.bark,p.moss,.6f));g.stroke(x+3,10,x-1,d,1.2f,mix(p.bark,p.moss,.45f));
        g.stroke(x+1,16,x+6,20,1,p.moss);leaf(g,x+6,20,4,2,mix(p.bark,p.moss,.5f));
    }
}
void rune(Canvas&g,float x,float y,float r,Color c){
    g.stroke(x-r,y,x,y-r*1.4f,1.4f,c);g.stroke(x,y-r*1.4f,x+r,y,1.4f,c);
    g.stroke(x+r,y,x,y+r*1.4f,1.4f,c);g.stroke(x,y+r*1.4f,x-r,y,1.4f,c);
    g.stroke(x,y-r*.55f,x,y+r*.55f,1.25f,c);
}
void seal(Canvas& g,const Scenery& scene,bool charged){
    stampProp(g,charged ? props::active_seal : props::spent_seal,scene,0,0,64,64);
}
void twig(Canvas&g,const Palette&p,float x,float y,float h,Color tint){
    g.stroke(x,y,x+2,y-h,2.8f,p.bark);for(int i=1;i<5;++i){float sy=y-i*h/5;leaf(g,x+1,sy,float(i%2?6:-7),-5.f,tint);}
}
void collectible(Canvas&g,const Scenery&s){
    constexpr int height=43;
    const int width=std::clamp(height*props::crystal.width/props::crystal.height,16,43);
    stampProp(g,props::crystal,s,(64-width)/2,30-height/2,width,height);
}
void woodDeck(Canvas&g,const Scenery&s,unsigned mt,unsigned variant){
    const bool ledge=mt>=0x16&&mt<=0x1b;
    const bool left=ledge?(mt==0x16||mt==0x19):bool(variant&32);
    const bool right=ledge?(mt==0x18||mt==0x1b):bool(variant&64);
    const auto& im=props::bridge;
    // The middle of the painting runs back and forth in world space. The two
    // cells abutting a joint always sample the same place along the wood grain.
    constexpr int joins[]={22,34,47,60,75,66,50,35,22};
    const unsigned col=variant%8;
    const int a=left?0:im.width*joins[col]/100;
    const int b=right?im.width:im.width*joins[col+1]/100;
    const int height=ledge?43:mt==0x89?28:32;
    stampPropRegion(g,im,s,0,-2,64,height,std::min(a,b),0,std::max(1,std::abs(a-b)),im.height,b<a);
}
void bridgeRailing(Canvas&g,const Palette&p,unsigned variant){
    const int c=int(variant%8);
    const bool left=variant&32,right=variant&64;
    const float x0=left?4.f:0.f,x1=right?60.f:64.f;
    const Color bark=mix(p.bark,0x694b35,.5f),hemp=mix(p.light,0x997447,.53f);
    auto post=[&](float x){
        g.stroke(x-1,64,x,20,3.4f,mix(bark,p.deep,.38f));
        g.stroke(x,64,x+1,20,1.1f,mix(bark,p.light,.5f));
        g.oval(x,20,2.1f,1.3f,hemp);
    };
    const float h0=left||c%2==0?24.f:29.f,h1=right||c%2!=0?24.f:29.f;
    const float mid=(h0+h1)*.5f+1.5f;
    if(left||c%2==0)post(x0+1);
    if(right)post(x1-1);
    g.stroke(x0,h0,32,mid,2.5f,bark);g.stroke(32,mid,x1,h1,2.5f,bark);
    g.stroke(x0,h0-.6f,32,mid-.6f,1.35f,hemp);g.stroke(32,mid-.6f,x1,h1-.6f,1.35f,hemp);
}
void cavernSlate(Canvas& g,const Palette& p,const Scenery& s,unsigned seed,bool exposed) {
    // Cut different faces from one painting, keeping the direction of the light.
    const auto& art=props::slate;
    const int sw=std::max(1,int(art.width*(.49f+.42f*noise(seed+41))));
    const int sh=std::max(1,int(art.height*(.46f+.45f*noise(seed+53))));
    const int sx=int((art.width-sw)*noise(seed+127));
    const int sy=int((art.height-sh)*noise(seed+67));
    const Color joint=mix(p.deep,0x202329,.48f);
    g.box(0,0,64,64,joint);
    stampPropRegion(g,art,s,0,0,64,64,sx,sy,sw,sh);

    // The mortar and top bevel stay inside the playable rectangle even on an
    // isolated or bouncing brick. Different cuts keep long walls readable.
    const float leftCorner=1.f+4.f*noise(seed+18),rightCorner=1.f+4.f*noise(seed+27);
    const float footCorner=1.f+3.f*noise(seed+82);
    const float lip=exposed?2.4f:1.15f;
    const float lightness=.08f*noise(seed+26);
    const Color ambient=noise(seed+23)>.5f?p.mist:p.deep;
    for(int y=0;y<64;++y){
        auto* dst=&g.rgb[(size_t(g.Y(float(y)))*g.width+g.X(0))*3];
        for(int x=0;x<64;++x,dst+=3){
            const float north=std::min(float(y),std::min(x+y-leftCorner,63-x+y-rightCorner)*.7071f);
            const float south=std::min(float(63-y),std::min(x+63-y-footCorner,126-x-y-footCorner)*.7071f);
            const float edge=std::min({north,south,float(x),float(63-x)});
            Color face=(unsigned(dst[0])<<16)|(unsigned(dst[1])<<8)|dst[2];
            face=mix(face,ambient,lightness);
            if(north<lip+1)face=mix(face,p.light,.24f*std::clamp((lip+1-north)/lip,0.f,1.f));
            if(south<2.5f||x>61)face=mix(face,joint,.30f);
            face=mix(joint,face,std::clamp(.48f+edge*.60f,0.f,1.f));
            dst[0]=static_cast<unsigned char>(face>>16);
            dst[1]=static_cast<unsigned char>(face>>8);
            dst[2]=static_cast<unsigned char>(face);
        }
    }
}
void paintCell(Canvas&g,const Palette&p,const Scenery&s,unsigned mt,bool exposed,unsigned seed,int y,unsigned variant){
    // Metatile IDs refer to the complete SMB1 visual dictionary, independent of collision.
    if(mt==0||mt==1||mt==0x5f||mt==0x60||mt==0x44||(mt==0x87&&s.area!=3))return;
    if((mt==0x86||mt==0x87)&&s.area==3){
        const bool surface=mt==0x86;
        for(int yy=0;yy<64;++yy){
            float depth=surface?yy/64.f:1.f;
            g.box(0,float(yy),64,1,mix(0xe89656,0x854a4c,.15f+.85f*depth));
        }
        for(int x=0;x<64;++x){
            const float wx=float(64*(variant%8)+x);
            const float ripple=1.8f*std::sin(wx*Tau/64)+1.2f*std::cos(wx*Tau/128);
            if(surface){g.box(float(x),3+ripple,1,2.5f,0xffdb8b);g.box(float(x),9+ripple,1,1.5f,0xefab6b);}
            for(int r=1;r<=3;++r){
                float fil=std::sin(wx*Tau/(r==1?64.f:128.f)+r*1.7f);
                if(fil>.27f)g.box(float(x),12.f+r*12.f+ripple*1.4f+fil*4,1,1.35f,mix(0xb35e51,0xf7b66b,.22f+.45f*fil));
            }
        }
        return;
    }
    if(mt<=0x0a||mt==0x0d||mt==0x0e||mt==0x0f||(mt>=0x80&&mt<=0x85))return; // scenery supplied by parallax groves
    if(mt==0xc2||mt==0xc3){collectible(g,s);return;}
    if(mt==0xc0||mt==0xc1||mt==0xc4){seal(g,s,mt!=0xc4);return;}
    if(mt==0xc5||mt==0x6d){g.stroke(27,54,38,15,3,p.bark);crystal(g,38,31,26,p.jewel,3);return;}
    if(mt==0x24||mt==0x25||mt==0x26||mt==0x40){
        g.stroke(31,0,34,32,3,p.moss);g.stroke(34,32,31,64,3,p.moss);
        for(int i=0;i<5;++i){float yy=i*13.f+5;leaf(g,32,yy,(i%2?8.f:-7.f),-5,p.moss);}
        if(mt==0x24){crystal(g,32,19,17,p.jewel,1);}
        return;
    }
    if(mt==0x0b){bridgeRailing(g,p,variant);return;}
    if(mt==0x0c||(mt>=0x41&&mt<=0x43)||mt==0x4d){
        g.stroke(0,27,32,30,2.2f,p.moss);g.stroke(32,30,64,27,2.2f,p.moss);
        for(int i=0;i<4;++i){float x=7+i*16.f;leaf(g,x,28,6,-4,p.moss);}
        if(mt==0x42||mt==0x43)g.stroke(32,29,32,64,2.5f,p.moss);
        return;
    }
    if((mt>=0x45&&mt<=0x4b)){
        // Offset courses run across the facades on the actual game map.
        const Color face=mix(p.bark,p.deep,.36f),seam=mix(face,p.deep,.67f);
        g.box(0,0,64,64,seam);
        for(int course=0;course<4;++course){
            int wy=int(variant/8)*4+course,origin=(wy%2?21:0),yy=course*16;
            for(int block=-1;block<4;++block){
                int wx=int(variant%8)*64;
                int lx=(wx+origin)/36*36-origin-wx+block*36;
                unsigned random=(unsigned(wx+lx+origin)/36u)*971u+unsigned(wy)*71u;
                float cut=1+2*noise(random+11);
                Color dressed=mix(face,p.moss,.02f+.10f*noise(random));
                g.polygon({{lx+cut,float(yy+1)},{float(lx+34),float(yy+1)},{float(lx+35),float(yy+3)},
                           {float(lx+35),float(yy+13)},{float(lx+32),float(yy+15)},{float(lx+1),float(yy+15)},
                           {float(lx),float(yy+12)},{float(lx),yy+cut}},dressed);
                g.stroke(lx+3.f,yy+2.f,lx+30.f,yy+2.f,1.1f,mix(dressed,p.moss,.22f));
                if(noise(random+5)>.7f)g.stroke(lx+20.f,yy+4.f,lx+15.f,yy+9.f,1.f,seam);
            }
        }
        if(mt==0x45||mt==0x49){
            g.box(0,0,64,8,mix(face,p.moss,.42f));
            g.stroke(0,2,64,2,1.8f,p.light);g.stroke(0,8,64,8,2.2f,p.deep);
            for(int i=0;i<5;++i){
                float xx=i*13.f+noise(seed+i+9)*4,ht=4+noise(seed+i+14)*9;
                g.stroke(xx,5,xx+2,ht,1.4f,p.moss);leaf(g,xx+1,ht-2,3,2,p.moss);
            }
        }
        if(mt==0x46||mt==0x48){
            lancet(g,32,56,31,45,p.deep,mix(p.moss,p.jewel,.30f));
            g.box(14,55,36,3,mix(face,p.moss,.4f));
        }
        if(mt==0x4a){
            g.oval(32,61,27,41,mix(face,p.moss,.32f));g.oval(32,62,22,36,p.deep);
            g.box(10,55,44,9,p.deep);
            for(int k=1;k<9;++k){
                float t=k*3.14159f/9;g.stroke(32+std::cos(t)*26,61-std::sin(t)*40,
                    32+std::cos(t)*22,61-std::sin(t)*36,1.3f,seam);
            }
            g.oval(32,13,4.5f,4.5f,p.deep);rune(g,32,13,3,p.jewel);
        }
        if(mt==0x4b){
            g.box(10,0,44,64,p.deep);g.box(10,0,4,60,mix(p.deep,0x101722,.62f));
            g.box(52,0,2,60,mix(p.deep,p.moss,.28f));g.box(9,59,46,3,mix(p.bark,p.moss,.23f));
        }
        return;
    }
    if(mt==0x4c||mt==0x4f||mt==0x50)return; // A continuous support sits behind the walkable shelf.
    if(mt==0x4e){twig(g,p,30,68,76,p.moss);return;}
    if(mt==0x22){
        g.stroke(30,61,30,12,4,p.moss);
        for(int i=0;i<5;++i){float yy=i*10.f+12,dx=i%2?13.f:-14.f;g.stroke(30,yy+6,30+dx,yy-2,3,p.moss);g.stroke(30+dx,yy-2,30+dx,yy-8,2,p.light);}
        return;
    }
    if(mt==0x86){for(int x=0;x<64;++x){float y1=3+1.8f*std::sin(x*Tau/32);g.box(float(x),y1,1,1.5f,p.light);g.box(float(x),y1+3,1,2,mix(p.moss,p.deep,.37f));}return;}
    if(mt>=0x64&&mt<=0x66){
        if(mt==0x64){g.box(0,0,64,64,p.bark);g.box(0,3,64,5,p.moss);g.oval(30,30,20,20,p.deep);g.ring(30,30,22,2,p.moss);g.oval(27,28,6,7,p.jewel);}
        else {rootstone(g,p,seed,false,false,false,variant);g.stroke(0,3,64,3,3,p.bark);}
        return;
    }
    if(mt==0x67||mt==0x68){
        if(mt==0x67){g.box(2,6,60,6,p.light);g.oval(32,9,30,8,p.moss);}
        for(int i=0;i<4;++i){float yy=i*14.f+(mt==0x67?12:0);g.stroke(14,yy,50,yy+7,3,p.jewel);g.stroke(50,yy+7,14,yy+14,3,p.moss);}
        return;
    }
    if((mt>=0x16&&mt<=0x1b)||mt==0x63||mt==0x89){woodDeck(g,s,mt,variant);return;}
    if(mt==0x88){
        g.box(0,0,64,13,p.bark);
        g.polygon({{0,9},{64,9},{62,19},{50,24},{38,21},{29,33},{16,25},{0,19}},mix(p.bark,p.deep,.44f));
        g.stroke(12,22,13,36,1.2f,p.moss);leaf(g,13,28,6,3,p.moss);
        moss(g,p,seed,s.area==0,false);return;
    }
    if(mt==0x54||mt==0x69||mt==0x62||((mt==0x51||mt==0x52)&&y>=208)){
        if(s.area==1)forestGround(g,p,s,variant,exposed);
        else rootstone(g,p,seed,exposed,s.area!=3,s.area==0,variant);
        return;
    }
    if(mt==0x61){rootstone(g,p,seed,exposed,false,false,variant);g.stroke(62,5,61,58,2,p.deep);return;}
    // Cave brick and its concealed-reward forms have identical native faces.
    if(s.area==2 && (mt==0x52||(mt>=0x5a&&mt<=0x5e))){
        cavernSlate(g,p,s,seed,exposed);return;
    }
    if(mt>=0x51&&mt<=0x5e){
        if(s.area==1){
            // All concealed-reward and ordinary bricks use the same weathered
            // masonry. Rest coordinates select the face throughout a bump.
            static constexpr const highlands::Image* faces[]{
                &highlands::fieldstone, &highlands::fieldstone_veined, &highlands::fieldstone_layered
            };
            const auto& block=*faces[(variant%24+2*(variant/24)+chapter(s))%3];
            const int dx=int(noise(seed+73)*block.width*.025f);
            const int dy=int(noise(seed+91)*block.height*.025f);
            stampPropRegion(g,block,s,0,0,64,64,dx,dy,block.width-2*dx,block.height-2*dy);
            return;
        }
        // Fractured slate pods carry one visible split, which reads as a breakable surface.
        g.box(0,0,64,64,p.deep);
        g.polygon({{2,5},{7,1},{56,1},{63,6},{62,53},{53,62},{9,62},{1,56}},p.bark);
        g.polygon({{7,2},{56,2},{58,6},{11,8},{3,15},{3,7}},p.moss);
        g.polygon({{51,9},{61,7},{61,52},{52,60},{40,60},{48,39},{44,35}},mix(p.bark,p.deep,.44f));
        const bool flip=(variant&1)!=0;
        const float start=13+noise(seed+9)*28;
        const float joint=start+(flip?-1:1)*(4+noise(seed+13)*7);
        const float branch=joint+(flip?1:-1)*(7+noise(seed+21)*8);
        const float split=16+noise(seed+11)*6;
        const float depth=30+noise(seed+4)*17;
        g.stroke(start,4,joint,split,1.7f,p.deep);
        g.stroke(joint,split,branch,depth*.76f,1.7f,p.deep);
        g.stroke(branch,depth*.76f,branch+(flip?-6:6),depth,1.6f,p.deep);
        g.stroke(joint,split,joint+(flip?-10:10),split+4,1.45f,p.deep);
        if(seed%3==0)leaf(g,13,8,7,6,p.moss);
        return;
    }
    if(mt==0x23)return; // A bumped block is painted by its native moving actor.
    if(bulk(mt))rootstone(g,p,seed,exposed,false,s.area==0,variant);
}

struct Brush {
    // The top gutter contains grass, flowers and the tips of leaves.
    Layer layer;
    void stamp(std::vector<unsigned char>&rgb,int width,int height,int x,int y,const LanternLight& light,
               int clipLeft=0,int clipTop=0,int clipRight=64,int clipBottom=80)const{
        const auto& im=layer;
        const bool lit=light.intersects(x,y,im.width,im.height);
        const int start=std::max({0,-y,clipTop}),end=std::min({im.height,height-y,clipBottom});
        for(int row=start;row<end;++row)for(auto run:im.bands[row]){
            int a=std::max({run.begin,-x,clipLeft}),b=std::min({run.end,width-x,clipRight});
            if(b>a){
                auto* dst=rgb.data()+(size_t(y+row)*width+x+a)*3;
                std::memcpy(dst,im.pixels.data()+(size_t(row)*im.width+a)*3,size_t(b-a)*3);
                if(lit)light.illuminate(dst,x+a,x+b,y+row);
            }
        }
    }
};
enum class Landmark { Post, House, Tower, Lamp };
const Brush& landmarkBrush(Landmark kind, const Scenery& scene) {
    thread_local std::array<Brush, 4> brushes;
    thread_local int cachedLight = -1;
    const int light = scene.area == 1 && scene.night ? 4 : scene.area;
    if (light != cachedLight) { brushes = {}; cachedLight = light; }
    auto& brush = brushes[static_cast<unsigned>(kind)];
    if (brush.layer.width) return brush;
    const landmarks::Image* art;
    int w, h;
    switch (kind) {
    case Landmark::Post: art = &landmarks::waypost; w = 68; h = 704; break;
    case Landmark::House: art = &landmarks::watchhouse; w = 348; h = 320; break;
    case Landmark::Tower: art = &landmarks::gatehouse; w = 344; h = 704; break;
    default: art = &landmarks::signal_lamp; w = 32; h = 64; break;
    }
    brush.layer.reset(w, h);
    Canvas canvas{brush.layer.pixels, w, h};
    if (kind == Landmark::Lamp) {
        // The sliding iron runner attaches the painted hanging loop to the pole.
        canvas.stroke(16, 4, 31, 4, 3, 0xa58350);
        canvas.stroke(16, 5, 31, 5, 1, 0x654d40);
    }
    stampProp(canvas, *art, scene, 0, 0, w, h);
    brush.layer.prepare();
    return brush;
}
const props::Image* habitatPainting(Sprite species) {
    switch(species) {
        case Sprite::Fern: return &props::fern;
        case Sprite::Mushroom: return &props::fungi;
        case Sprite::Seaweed: return &props::kelp;
        case Sprite::Geode: return &props::geode;
        case Sprite::Urn: return &props::urn;
        default: return nullptr;
    }
}
// Colonies only occur on exposed habitat. A meadow contains a few clear stretches
// between plant groups, even when a screen spans many more tiles than the native viewport.
struct Habitat {
    std::array<Brush,12> brushes{};
    int stage=-1;
    const Brush& get(const Scenery& s,unsigned variant){
        const int key=chapter(s)*8+(s.area&3)*2+int(s.night);
        if(stage!=key){brushes={};stage=key;}
        auto& brush=brushes[variant];
        if(brush.layer.width) return brush;
        auto& layer=brush.layer;
        layer.reset(64,54);
        Canvas g{layer.pixels,64,54};
        const Palette p=colors(s);
        static constexpr Sprite specimens[][6]={
            {Sprite::Coral,Sprite::Fossil,Sprite::Sponge,Sprite::Seaweed,Sprite::Stone,Sprite::Moss},
            {Sprite::Fern,Sprite::Bluebell,Sprite::Mushroom,Sprite::Wildflower,Sprite::Moss,Sprite::Stone},
            {Sprite::Geode,Sprite::Crystal,Sprite::Fossil,Sprite::Stone,Sprite::Mushroom,Sprite::Moss},
            {Sprite::Urn,Sprite::Lantern,Sprite::Stone,Sprite::Relic,Sprite::Fossil,Sprite::Moss}
        };
        auto species=specimens[s.area&3];
        unsigned n=variant*183+chapter(s)*71;
        const bool pair=variant<6;
        const float base=52,mainX=pair?27:32,ht=(pair?24.f:21.f)+noise(n+4)*7;
        Color dark=mix(p.deep,p.bark,.47f),light=mix(p.moss,p.light,.18f);
        if(s.area==0)light=mix(light,0x90bca9,.28f);
        if(s.area==2)light=mix(light,0xbca7dc,.30f);
        if(s.area==3)light=mix(p.moss,0xdfad84,.50f);
        auto plant=[&](Sprite id,float at,float tall,float tint,bool mirror,bool main) {
            if(const auto* painting=habitatPainting(id)) {
                // The blade tips and vase handles stay inside this local brush.
                // Place the bottom of each trimmed painting directly on the soil.
                const int maxWidth=std::min(int(at)*2,(g.width-int(at))*2);
                const int targetHeight=int(tall*(main?1.55f:1.20f)+.5f);
                const float scale=std::min(float(targetHeight)/painting->height,
                                           float(maxWidth)/painting->width);
                const int w=std::max(1,int(painting->width*scale+.5f));
                const int h=std::max(1,int(painting->height*scale+.5f));
                stampProp(g,*painting,s,int(at)-w/2,int(base)-h,w,h,mirror);
            } else {
                specimen(g,id,at,base,tall,dark,light,tint,mirror);
            }
        };
        auto main=species[variant%6];
        const bool low=main==Sprite::Moss||main==Sprite::Stone||main==Sprite::Fossil;
        plant(main,mainX,low?ht*.64f:ht,.62f,variant%3==2,true);
        if(pair) {
            auto companion=species[(variant+2)%6];
            if(s.area==3)companion=Sprite::Stone;
            if(s.area==0)companion=main==Sprite::Seaweed?Sprite::Shell:Sprite::Seaweed;
            // A tiny, silt-tinted shell sits at the foot of a kelp blade, never on its own.
            const float sideHeight=companion==Sprite::Shell?8.f:11.f+3*noise(n+9);
            plant(companion,41,sideHeight,companion==Sprite::Shell?.85f:.7f,variant%2==0,false);
        }
        layer.prepare();
        return brush;
    }
};
bool openHabitat(unsigned above,int area){
    // These decorative native cells are painted by the parallax renderer.
    // The deep-water fill also occupies the space immediately above the seabed.
    return above<=0x0a || (above>=0x0d && above<=0x0f) || above==0x44
        || (above>=0x80 && above<=0x85) || (above==0x87 && area==0);
}
void contactShadow(std::vector<unsigned char>&rgb,int width,int height,int center,int surface,int halfWidth) {
    for(int row=0;row<6;++row){
        const int y=surface+row;
        if(y<0||y>=height)continue;
        const float v=(row-2.f)/3.5f,vertical=1-v*v;
        if(vertical<=0)continue;
        for(int x=std::max(0,center-halfWidth);x<std::min(width,center+halfWidth);++x){
            float u=(x-center)/float(halfWidth),weight=vertical-u*u;
            if(weight<=0)continue;
            auto* pixel=&rgb[(size_t(y)*width+x)*3];
            const int shade=int(76*weight);
            for(int k=0;k<3;++k)pixel[k]=static_cast<unsigned char>((pixel[k]*(256-shade))>>8);
        }
    }
}
bool naturalSurface(unsigned mt,int y){
    return mt==0x54||mt==0x69||mt==0x62||mt==0x61||((mt==0x51||mt==0x52)&&y>=208);
}
struct TerrainCache {
    struct Entry {unsigned key;Brush brush;};
    std::vector<Entry> entries;
    int stage=-1;
    const Brush& get(const Scenery&s,unsigned mt,bool top,unsigned variant,int y){
        int k=chapter(s)*64+(s.level&7)*8+(s.area&3)*2+int(s.night);
        if(k!=stage){entries.clear();stage=k;}
        bool deepGround=y>=208;
        unsigned key=(mt<<9)|(variant<<2)|(unsigned(top)<<1)|unsigned(deepGround);
        for(const auto&e:entries)if(e.key==key)return e.brush;
        auto&b=entries.emplace_back(Entry{key,{}}).brush;auto&l=b.layer;
        l.reset(64,80);Canvas g{l.pixels,64,80,1,0,16};
        const auto p=colors(s);paintCell(g,p,s,mt,top,1+variant*157+chapter(s)*59+s.level*23,y,variant);
        l.prepare();return b;
    }
};
const Brush& tileBrush(const Scenery&s,int restX,int restY,unsigned material,bool exposed,unsigned openEnds=0){
    unsigned column=unsigned(std::max(0,restX+s.camera))/16;
    unsigned row=unsigned(std::max(0,restY))/16;
    const bool forestFloor = s.area==1 && (material==0x54 || material==0x69 || material==0x62 ||
                            ((material==0x51 || material==0x52) && restY>=208));
    const bool fieldstone = s.area==1 && material>=0x51 && material<=0x5e;
    unsigned columns=forestFloor?32u:fieldstone?24u:8u;
    unsigned variant=column%columns+columns*(row%4);
    if(material==0x0b || material==0x63 || material==0x89)variant+=(openEnds&3)*32;
    thread_local TerrainCache cache;
    return cache.get(s,material,exposed,variant,restY);
}
enum class WoodShape { Stump, Junction, SideMouth, ShelfPost };
struct WoodObject {
    WoodShape shape;
    int column, top, bottom;
    bool openAbove=false, inverted=false, grounded=false;
};
bool verticalWood(unsigned m){
    return (m>=0x10 && m<=0x15) || m==0x1e || m==0x21;
}
bool woodCap(unsigned m){ return m>=0x10 && m<=0x13; }

// Object coordinates use absolute map columns, including when the mouth or
// the right half of a trunk lies outside the visible viewport.
std::vector<WoodObject> woodObjects(const TerrainWindow& map){
    std::vector<WoodObject> result;
    for(int c=map.first;c<map.first+map.count;++c)for(int r=0;r<TerrainWindow::Rows;++r){
        const unsigned m=map.get(c,r);
        const bool side=m>=0x1c && m<=0x21, water=m==0x6b||m==0x6c;
        if(!side && !water && !verticalWood(m))continue;
        int stem=c,top=r,bottom=r+1;
        auto shape=WoodShape::Stump;
        if(water){
            top=r-int(m==0x6c);bottom=top+2;shape=WoodShape::SideMouth;
        }else{
            if(side){
                stem=c+2-int((m-0x1c)%3);
                top=r-int(m>=0x1f);bottom=top+2;
                shape=WoodShape::Junction;
            }else stem=c-int(m&1);
            auto stemRow=[&](int row){return verticalWood(map.get(stem,row))||verticalWood(map.get(stem+1,row));};
            while(top>0 && stemRow(top-1))--top;
            while(bottom<TerrainWindow::Rows && stemRow(bottom))++bottom;
            for(int row=top;row<bottom;++row){
                if(map.get(stem,row)==0x1e || map.get(stem-1,row)==0x1d){
                    shape=WoodShape::Junction;break;
                }
            }
        }
        const int left=stem-(shape==WoodShape::Junction?2:0);
        if(std::any_of(result.begin(),result.end(),[&](const WoodObject&o){
            return o.column==left && o.shape==shape && o.top==top && o.bottom==bottom;
        }))continue;
        const bool hasCap=woodCap(map.get(stem,top))||woodCap(map.get(stem+1,top));
        const bool inverted=shape==WoodShape::Stump && !hasCap &&
                         (woodCap(map.get(stem,bottom-1))||woodCap(map.get(stem+1,bottom-1)));
        const bool above=top==0&&!hasCap&&!inverted&&!water;
        const bool grounded=bulk(map.get(stem,bottom))||(!water&&bulk(map.get(stem+1,bottom)));
        result.push_back({shape,left,top,bottom,above,inverted,grounded});
    }
    return result;
}
struct WoodBitmap {
    int width,height;
    std::vector<std::uint32_t> pixels;
};
float interpolate(float x,float x0,float x1,float a,float b){
    return a+(b-a)*(x-x0)/(x1-x0);
}
const WoodBitmap& woodBitmap(const WoodObject&o,int width,int height,int light){
    struct Entry {WoodShape shape;bool continuous;WoodBitmap bitmap;};
    thread_local std::vector<Entry> cache;
    thread_local int ambience=-1;
    if(light!=ambience){cache.clear();ambience=light;}
    for(auto&e:cache)if(e.shape==o.shape && e.continuous==o.openAbove &&
                        e.bitmap.width==width && e.bitmap.height==height)return e.bitmap;
    const bool support=o.shape==WoodShape::ShelfPost;
    const auto& art=(o.shape==WoodShape::Stump||support)?elderwood::stump:
                    o.shape==WoodShape::Junction?elderwood::junction:elderwood::branch;
    WoodBitmap image{width,height,std::vector<std::uint32_t>(size_t(width)*height)};
    for(int y=0;y<height;++y){
        const float py=y+.5f;float v=py/height;
        if(o.shape==WoodShape::Junction){
            // The fork in the source painting starts 71% down. Map that point
            // to the exact native ledge two tiles above the bottom of the entry.
            const float split=std::max(0.f,float(height-130));
            if(o.openAbove && split>310.f && py<split){
                // A shaft leaving the ceiling has no cut top. Keep the bark at
                // its painted scale and carry it upward without another fork.
                const float travel=(split-py)*.83f/art.height;
                if(travel<=.51f)v=.71f-travel;
                else {
                    const float turn=std::fmod(travel-.51f,.74f);
                    v=.20f+(turn<=.37f?turn:.74f-turn);
                }
            }else if(py<split)v=interpolate(py,0,split,o.openAbove?.22f:0.f,.71f);
            else v=interpolate(py,split,float(height),.71f,1.f);
        }else if(support){
            // A ledge support continues beneath the deck, without a hollow mouth.
            // Reuse the middle bark in its painted proportions on tall trestles.
            const float rootHeight=std::min(19.f,height*.13f);
            const float barkEnd=o.grounded?height-rootHeight:float(height);
            if(py>=barkEnd)v=interpolate(py,barkEnd,float(height),.83f,1.f);
            else {
                const float travel=(barkEnd-py)*art.width/width/art.height;
                if(travel<=.63f)v=.83f-travel;
                else {
                    const float turn=std::fmod(travel-.63f,.90f);
                    v=.20f+(turn<=.45f?turn:.90f-turn);
                }
            }
        }else if(o.shape==WoodShape::Stump){
            const float lip=std::min(23.f,height*.18f),roots=std::min(36.f,height*.25f);
            if(py<lip)v=interpolate(py,0,lip,o.openAbove?.23f:0.f,o.openAbove?.28f:.095f);
            else if(py<height-roots)v=interpolate(py,lip,height-roots,o.openAbove?.28f:.095f,.84f);
            else v=interpolate(py,height-roots,float(height),.84f,1.f);
        }
        for(int x=0;x<width;++x){
            float u=(x+.5f)/width;
            if(o.shape==WoodShape::Junction){
                // Preserve the breadth of the opening; fit the straight section
                // between it and the load-bearing trunk in the last two columns.
                if(u<.30f)u=interpolate(u,0,.30f,0,.215f);
                else if(u<.50f)u=interpolate(u,.30f,.50f,.215f,.645f);
                else u=interpolate(u,.50f,1.f,.645f,1.f);
            }else if(o.shape==WoodShape::SideMouth){
                // The short underwater passage exposes a knot rather than
                // a sawed-off square. Its far shoulder curves into the bank.
                const float axis=std::abs(2*v-1);
                const float shoulder=1.f-.23f*axis*axis;
                const float coverage=std::clamp((shoulder-u)*width+.5f,0.f,1.f);
                u=u<.75f?interpolate(u,0,.75f,0,.235f):interpolate(u,.75f,1.f,.235f,.365f);
                const auto c=samplePainting(art,u,v,light);
                image.pixels[size_t(y)*width+x]=(c&0xffffff00u)|unsigned((c&255)*coverage+.5f);
                continue;
            }
            auto pixel=samplePainting(art,u,v,light);
            if(support && (pixel&255)){
                const Color shade=light==0?0x305156:light==2?0x343b4e:0x42474c;
                pixel=(mix(pixel>>8,shade,.44f)<<8)|(pixel&255);
            }
            image.pixels[size_t(y)*width+x]=pixel;
        }
    }
    cache.push_back({o.shape,o.openAbove,std::move(image)});
    return cache.back().bitmap;
}
// Fill a scanline mask from the same cached painting used for the trunk.
void addMouth(std::vector<HollowMouth>& mouths,const WoodBitmap& art,int left,int top,
              const WoodObject& wood,bool sideways){
    const bool fork=wood.shape==WoodShape::Junction;
    int rows=sideways?std::min(128,art.height):fork?
        std::clamp(int((art.height-130)*.13f),1,36):std::min(23,art.height);
    const int first=sideways||wood.inverted?art.height-rows:0;
    const float cx=sideways?0.f:(fork?.81f:.5f)*art.width;
    const float rx=(fork?.136f:.292f)*art.width;
    HollowMouth m{sideways?HollowMouth::Axis::Side:HollowMouth::Axis::Vertical,
                  wood.inverted,left,top+first,left+art.width,top+first+rows};
    for(int j=0;j<rows;++j){
        const int row=first+j,srcY=wood.inverted?art.height-1-row:row;
        int x0=0,x1=art.width;
        if(sideways){
            const float v=(j+.5f)/rows;
            const float dy=(v-.592f)/.346f;
            if(std::abs(dy)>=1.f)continue;
            const float fullWidth=fork?83.f:std::min(55.f,art.width*.85f);
            x1=std::min(x1,int(fullWidth*(.48f-.115f*dy+.52f*std::sqrt(1.f-dy*dy))));
        }else{
            // The back rim stays behind the robot; the foreground bark starts
            // at the curved near edge of the dark opening.
            const float y=wood.inverted?rows-j-.5f:j+.5f;
            const float d=std::max(0.f,(y/rows-.33f)/.42f);
            if(d>=1.f)continue;
            const float radius=rx*std::sqrt(1.f-d*d);
            x0=std::max(x0,int(cx-radius));
            x1=std::min(x1,int(cx+radius+1.f));
        }
        const auto* pixels=&art.pixels[size_t(srcY)*art.width];
        while(x0<x1 && (pixels[x0]&255)<220)++x0;
        while(x1>x0 && (pixels[x1-1]&255)<220)--x1;
        m.spans[j]={left+x0,left+x1};
    }
    mouths.push_back(m);
}
void stampWood(std::vector<unsigned char>&rgb,int width,int height,const Scenery&s,const WoodObject&o,int postWidth=0,int covered=0,std::vector<HollowMouth>* mouths=nullptr){
    const bool stump=o.shape==WoodShape::Stump,fork=o.shape==WoodShape::Junction;
    const int left=(o.column*16-s.camera)*4+(postWidth?(64-postWidth)/2:stump?-17:0);
    const int top=o.openAbove?0:(32+o.top*16)*4-covered;
    const int bottom=(32+o.bottom*16)*4;
    const int imgW=postWidth?postWidth:stump?166:fork?262:64,imgH=bottom-top+int(o.grounded)*3;
    if(left>=width||left+imgW<=0||top>=height||imgH<=0)return;
    const int light=(s.area==1&&s.night)?4:s.area;
    const auto& bitmap=woodBitmap(o,imgW,imgH,light);
    if(mouths){
        if((stump||fork)&&!o.openAbove)addMouth(*mouths,bitmap,left,top,o,false);
        if(fork||o.shape==WoodShape::SideMouth)addMouth(*mouths,bitmap,left,top,o,true);
    }
    if(o.grounded&&!postWidth)contactShadow(rgb,width,height,left+(fork?imgW*3/4:imgW/2),bottom,
                                stump?76:fork?76:30);
    const int x0=std::max(0,-left),x1=std::min(imgW,width-left);
    const bool lit=s.lantern.intersects(left,top,imgW,imgH);
    for(int row=std::max(0,-top);row<std::min(imgH,height-top);++row){
        const auto* pixels=&bitmap.pixels[size_t(o.inverted?imgH-1-row:row)*imgW];
        auto* dst=&rgb[(size_t(row+top)*width+left+x0)*3];
        int runStart=-1;
        for(int x=x0;x<x1;++x,dst+=3){
            const unsigned c=pixels[x],a=c&255;
            if(!a){
                if(runStart>=0){s.lantern.illuminate(dst-(x-runStart)*3,left+runStart,left+x,top+row);runStart=-1;}
                continue;
            }
            if(a==255){dst[0]=c>>24;dst[1]=c>>16;dst[2]=c>>8;}
            else for(int k=0;k<3;++k)dst[k]=static_cast<unsigned char>((((c>>(24-8*k))&255)*a+dst[k]*(255-a)+127)/255);
            if(lit&&runStart<0)runStart=x;
        }
        if(runStart>=0)s.lantern.illuminate(dst-(x1-runStart)*3,left+runStart,left+x1,top+row);
    }
}
}
LandmarkCoverage drawFinishLandmarks(std::vector<unsigned char>& rgb, int width, int height,
                                     const Scenery& scene, const TerrainWindow& map) {
    LandmarkCoverage coverage{};
    const auto stamp = [&](Landmark kind, int center, int base) {
        const auto& b = landmarkBrush(kind, scene);
        const int w = b.layer.width, h = b.layer.height;
        b.stamp(rgb, width, height, (center-scene.camera)*4-w/2, base*4-h,
                scene.lantern, 0, 0, w, h);
    };
    for (int c = map.first; c < map.first + map.count; ++c) {
        if (map.get(c,0)==0x24 && map.get(c,1)==0x25 && map.get(c,10)==0x61) {
            stamp(Landmark::Post, c*16+8, 208);
            coverage[c-map.first] = (1u<<11)-1;
        }
        // The two lancet windows flank the central brick in both authored layouts.
        // Keep unrelated masonry and foreground platforms separate from the house.
        for (int top : {0, 6}) {
            if (map.get(c-1,top+1)!=0x46 || map.get(c+1,top+1)!=0x48 ||
                map.get(c,top+3)!=0x4a) continue;
            stamp(top==0 ? Landmark::Tower : Landmark::House, c*16+8, 208);
            for (int col=std::max(c-2,map.first); col<=std::min(c+2,map.first+map.count-1); ++col) {
                for (int row=top; row<=10; ++row) {
                    const auto material=map.get(col,row);
                    const bool doorStop = row==10 && material==0x52 &&
                                          col==c+(top==0?-1:1);
                    if ((material>=0x45 && material<=0x4b) || doorStop)
                        coverage[col-map.first] |= std::uint16_t(1u<<row);
                }
            }
        }
    }
    return coverage;
}
void drawSignalLampCell(std::vector<unsigned char>& rgb, int width, int height,
                        const Scenery& scene, int x, int y, int row) {
    const auto& lamp = landmarkBrush(Landmark::Lamp, scene);
    lamp.stamp(rgb, width, height, x*4, y*4-row*32, scene.lantern, 0, row*32, 32, (row+1)*32);
}
void drawShelfSupports(std::vector<unsigned char>&rgb,int width,int height,const Scenery&s,const TerrainWindow&map){
    if(width<=0||height<=0||rgb.size()<size_t(width)*height*3)return;
    auto post=[](unsigned m){return m==0x4c||m==0x4f||m==0x50;};
    for(int c=map.first;c<map.first+map.count;++c){
        for(int row=0;row<TerrainWindow::Rows;){
            const auto material=map.get(c,row);
            if(!post(material)){++row;continue;}
            const int first=row;
            do{++row;}while(post(map.get(c,row)));
            const auto roof=map.get(c,first-1);
            // The original canopy is 16 pixels tall as a tile, but its visible
            // platform only takes the upper half. Seat the support behind it.
            const int covered=roof>=0x16&&roof<=0x1b?53:0;
            const int postWidth=material!=0x4c?43:c%3==0?35:16;
            const WoodObject o{WoodShape::ShelfPost,c,first,row,false,false,bulk(map.get(c,row))};
            stampWood(rgb,width,height,s,o,postWidth,covered);
        }
    }
}
void drawElderwood(std::vector<unsigned char>&rgb,int width,int height,const Scenery&s,const TerrainWindow&map,std::vector<HollowMouth>* mouths){
    if(width<=0||height<=0||rgb.size()<size_t(width)*height*3)return;
    for(const auto& object:woodObjects(map))stampWood(rgb,width,height,s,object,0,0,mouths);
}
void movingTerrainCell(std::vector<unsigned char>& rgb,int width,int height,const Scenery& s,
                       int x,int y,unsigned material,int partX,int partY,int restY){
    if(partX<0||partX>1||partY<0||partY>1||width<=0||height<=0||rgb.size()<size_t(width)*height*3)return;
    const int originX=x-partX*8,originY=y-partY*8;
    const auto& b=tileBrush(s,originX,restY,material,true);
    const int left=partX*32,top=16+partY*32;
    b.stamp(rgb,width,height,originX*4,originY*4-16,s.lantern,left,top,left+32,top+32);
}
void background(std::vector<unsigned char>&rgb,int width,int height,const Scenery&s){
    thread_local Landscape land;
    land.render(rgb,width,height,s);
}
unsigned visualMetatile(const unsigned char*ram,const unsigned char quad[4],unsigned palette,unsigned collision){
    if(!quad||!ram||palette>3)return collision;
    // A temporarily empty/bumped visual cell should expose what is actually visible this frame.
    if(quad[0]==0x24&&quad[1]==0x24&&quad[2]==0x24&&quad[3]==0x24)return 0;
    const unsigned table=ram[0x8b08+palette] | (unsigned(ram[0x8b0c+palette])<<8);
    if(table<0x8000||table>0xff00)return collision;
    const unsigned offset=palette*64;
    if((collision>>6)==palette&&collision<=255&&std::memcmp(ram+table+4*(collision&63),quad,4)==0)return collision;
    // The four graphics banks end before the unused holes of their ID spaces.
    constexpr unsigned counts[]={0x27,0x2e,0x0b,0x07};
    for(unsigned i=0;i<counts[palette];++i)if(std::memcmp(ram+table+4*i,quad,4)==0)return offset+i;
    return collision;
}
void terrain(std::vector<unsigned char>&rgb,int width,int height,const Scenery&s,int x,int y,unsigned mt,unsigned above,unsigned openEnds,bool landmarkAbove){
    if((mt>=0x10&&mt<=0x15)||(mt>=0x1c&&mt<=0x21)||mt==0x6b||mt==0x6c)return; // Draw each hollow as one object.
    if(mt==0||mt==1||mt>255||x*4>=width||(x+16)*4<=0||y*4>=height+16||rgb.size()<size_t(width)*height*3)return;
    unsigned column=unsigned(std::max(0,x+s.camera))/16,row=unsigned(std::max(0,y))/16;
    const bool isCrystal=mt==0xc2 || mt==0xc3;
    const float cycle=float(s.frame%7200)/60.f+float(column%11);
    const int lift=isCrystal?int(std::lround(2.f*std::sin(cycle*2.f))):0;
    const auto&b=tileBrush(s,x,y,mt,landmarkAbove || !bulk(above),openEnds);
    b.stamp(rgb,width,height,x*4,y*4-16+lift,s.lantern);
    if(openHabitat(above,s.area) && y>=112 && naturalSurface(mt,y)){
        const unsigned group=column/6,pos=column%6;
        const unsigned salt=group*471+chapter(s)*183+s.level*33+row*79;
        const unsigned start=unsigned(noise(salt+6)*3.98f);
        if(pos==start){
            thread_local Habitat habitat;
            // Adjacent gardens draw from disjoint sets. Companion species live
            // inside their own brush and share the primary's patch of soil.
            const unsigned kind=(group%2)*3+unsigned(noise(salt+19)*2.99f);
            const unsigned variant=kind+(noise(salt+3)>.55f?6u:0u);
            contactShadow(rgb,width,height,x*4+31,y*4,variant<6?23:16);
            habitat.get(s,variant).stamp(rgb,width,height,x*4,y*4-52,s.lantern);
        }
    }
    if(mt==0xc2||mt==0xc3||mt==0x24||mt==0xc0||mt==0xc1){
        Canvas g{rgb,width,height,1,float(x*4),float(y*4+lift)};
        const bool chargedSeal=mt==0xc0||mt==0xc1;
        int cy=mt==0x24?10:(chargedSeal?32:30);
        float pulse=.55f+.45f*std::sin(cycle*2.5f);
        auto p=colors(s);
        const Color aura=chargedSeal ? mix(0xffc454,p.jewel,.20f) : isCrystal?mix(0x85edbd,p.jewel,.18f):p.jewel;
        g.glow(32,float(cy),chargedSeal?13.f:19.f,aura,.14f+pulse*.15f);
        if(isCrystal&&pulse>.76f){
            const float radius=(pulse-.76f)*12;
            const auto glint=mix(aura,0xfffee8,pulse);
            g.stroke(30-radius,21,30+radius,21,.9f,glint);
            g.stroke(30,21-radius,30,21+radius,.9f,glint);
        }
    }
}
} // namespace iskra

