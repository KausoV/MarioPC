#include "character_art.h"
#include "chibi_art.h"
#include "sheet_art.h"
#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/palette_builtin.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
constexpr int Scale = mario::CharacterRenderer::Scale;
constexpr int Width = mario::CharacterRenderer::Width;
constexpr int Height = mario::CharacterRenderer::Height;
constexpr int AtlasWidth = 64, AtlasHeight = 128;
int checks = 0, failures = 0;
void check(const std::string& name, bool passed) {
    ++checks; if (!passed) ++failures;
    std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
}
std::array<unsigned char,32> canonicalPalette() {
    std::array<unsigned char,32> indices{};
    for (unsigned i = 0; i < indices.size(); ++i) indices[i] = static_cast<unsigned char>((i * 7) & 63);
    indices[16] = 0x22; indices[17] = 0x16; indices[18] = 0x27; indices[19] = 0x18;
    return indices;
}
unsigned sample(int tile, int x, int y) {
    const auto* chr = MarioGameData + 0x8010 + tile * 16;
    return ((chr[y] >> (7-x)) & 1) | (((chr[y+8] >> (7-x)) & 1) << 1);
}
SMB_tile makeTile(int index, int x=30, int y=40, int palette=4, bool horizontal=false, bool vertical=false) {
    SMB_tile tile{}; tile.tileidx=index; tile.x=x; tile.y=y; tile.paletteidx=palette;
    tile.flip_horz=horizontal; tile.flip_vert=vertical; tile.extra_type=TILE_TYPE_SPRITE;
    return tile;
}
struct NativeRaster {
    SMBraster* raster = static_cast<SMBraster*>(std::malloc(SMBraster_size()));
    std::vector<unsigned char> pixels = std::vector<unsigned char>(256 * 240 * 3);
    NativeRaster() {
        if (!raster || !SMBraster_init(raster)) throw std::runtime_error("raster allocation failed");
        SMBraster_provide_palette_lookup(raster, &PALETTE_BUILTIN[0][0]);
        SMBraster_set_buffer(raster,pixels.data(),256);
        SMBraster_update_pattern_tables(raster,MarioGameData+0x8010);
    }
    ~NativeRaster() { SMBraster_fini(raster); std::free(raster); }
};
std::vector<unsigned char> scaled(const std::vector<unsigned char>& native) {
    mario::CharacterRenderer renderer;
    renderer.seedFromNative256(native.data()); return renderer.pixels();
}
std::size_t countColors(const std::vector<unsigned char>& pixels) {
    std::set<unsigned> colors;
    for (std::size_t offset=0; offset<pixels.size(); offset+=3)
        colors.insert((unsigned(pixels[offset])<<16) | (unsigned(pixels[offset+1])<<8) | pixels[offset+2]);
    return colors.size();
}
std::uint64_t hash(const std::vector<unsigned char>& pixels) {
    std::uint64_t value=14695981039346656037ull;
    for(unsigned char byte:pixels){value^=byte;value*=1099511628211ull;}return value;
}
// Independent native pose fixtures copied from the core's documented animation
// table. They test recognition rather than selecting renderer poses directly.
static constexpr unsigned char Poses[26][8] = {
 {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07},
 {0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f},
 {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17},
 {0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f},
 {0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27},
 {0x08,0x09,0x28,0x29,0x2a,0x2b,0x2c,0x2d},
 {0x08,0x09,0x0a,0x0b,0x0c,0x30,0x2c,0x2d},
 {0x08,0x09,0x0a,0x0b,0x2e,0x2f,0x2c,0x2d},
 {0x08,0x09,0x28,0x29,0x2a,0x2b,0x5c,0x5d},
 {0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x5e,0x5f},
 {0xfc,0xfc,0x08,0x09,0x58,0x59,0x5a,0x5a},
 {0x08,0x09,0x28,0x29,0x2a,0x2b,0x0e,0x0f},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x34,0x35},
 {0xfc,0xfc,0xfc,0xfc,0x36,0x37,0x38,0x39},
 {0xfc,0xfc,0xfc,0xfc,0x3a,0x37,0x3b,0x3c},
 {0xfc,0xfc,0xfc,0xfc,0x3d,0x3e,0x3f,0x40},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x41,0x42,0x43},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x44,0x45},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x44,0x47},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x48,0x49},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x90,0x91},
 {0xfc,0xfc,0xfc,0xfc,0x3a,0x37,0x92,0x93},
 {0xfc,0xfc,0xfc,0xfc,0x9e,0x9e,0x9f,0x9f},
 {0xfc,0xfc,0xfc,0xfc,0x3a,0x37,0x4f,0x4f},
 {0xfc,0xfc,0x00,0x01,0x4c,0x4d,0x4e,0x4e},
 {0x00,0x01,0x4c,0x4d,0x4a,0x4a,0x4b,0x4b}
};
bool swimmingPose(unsigned pose) { return (pose>=5&&pose<=7)||(pose>=17&&pose<=19); }
std::array<SMB_tile,8> makePose(unsigned pose,int x=30,int y=40,bool left=false,unsigned start=4,int palette=4) {
    std::array<SMB_tile,8> commands{};
    for(unsigned slot=0;slot<8;++slot) {
        const auto index=left?(slot^1u):slot;
        auto command=makeTile(Poses[pose][index],x+int(slot%2)*8,y+int(slot/2)*8,palette,left);
        command.extra_spriteidx=static_cast<unsigned char>((start+slot)&63);
        const bool symmetric=(pose==25&&slot>=4)||((pose==10||pose==23||pose==24)&&slot>=6)||(pose==22&&slot>=4);
        if(symmetric)command.flip_horz=(slot&1)!=0;
        commands[slot]=command;
    }
    return commands;
}
void drawPrepared(mario::CharacterRenderer& art,const std::array<SMB_tile,8>& commands,
                  unsigned pose,bool luigi=false,bool fire=false,bool descending=true) {
    art.preparePlayer(commands,luigi,fire,swimmingPose(pose));
    for(unsigned i=0;i<8;++i)art.drawTile(commands[descending?7-i:i],true,luigi,fire);
}
mario::ChibiColors marioColors() {
    mario::ChibiColors colors{};
    colors.cap={218,41,58};colors.shirt=colors.cap;colors.overalls={36,94,177};colors.luigi=false;
    return colors;
}
void compositeAtlas(std::vector<unsigned char>& frame,const std::vector<unsigned char>& rgba,int x,int y,bool left=false) {
    for(int ay=0;ay<AtlasHeight;++ay)for(int ax=0;ax<AtlasWidth;++ax) {
        const int targetX=x*Scale+ax,targetY=y*Scale+ay;
        if(targetX<0||targetX>=Width||targetY<0||targetY>=Height)continue;
        const auto input=(std::size_t(ay)*AtlasWidth+(left?AtlasWidth-1-ax:ax))*4;
        const auto output=(std::size_t(targetY)*Width+targetX)*3;
        const unsigned alpha=rgba[input+3];
        for(unsigned channel=0;channel<3;++channel)
            frame[output+channel]=static_cast<unsigned char>((unsigned(rgba[input+channel])*alpha+unsigned(frame[output+channel])*(255-alpha)+127)/255);
    }
}
// Changes may occupy the complete old player cells, because the new silhouette
// replaces CHR alpha. Later opaque native draws must still erase permission.
void spriteMask(std::vector<unsigned char>& mask, const SMB_tile& tile, bool player) {
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        if(!player&&!sample(tile.tileidx,x,y))continue;
        const int px=tile.x+(tile.flip_horz?7-x:x),py=tile.y+(tile.flip_vert?7-y:y);
        if(px<0||px>=256||py<0||py>=240)continue;
        mask[py*256+px]=static_cast<unsigned char>(player);
    }
}
bool insideCellsOnly(const std::vector<unsigned char>& frame,const std::vector<unsigned char>& blank,
                     const std::array<SMB_tile,8>& commands) {
    std::vector<unsigned char> mask(256*240);
    for(const auto& command:commands)spriteMask(mask,command,true);
    for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {
        const auto p=(std::size_t(y)*Width+x)*3;
        if(!mask[(y/Scale)*256+x/Scale]&&std::memcmp(frame.data()+p,blank.data()+p,3))return false;
    }
    return true;
}
std::uint64_t alphaDistance(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    std::uint64_t distance=0;
    for(std::size_t i=3;i<a.size();i+=4)distance+=unsigned(std::abs(int(a[i])-int(b[i])));
    return distance;
}
std::uint64_t lowerBodyHash(const std::vector<unsigned char>& rgba) {
    std::vector<unsigned char> lower(rgba.begin()+96*AtlasWidth*4,rgba.end());
    return hash(lower);
}
void historicProceduralMotionTests(const std::filesystem::path& output) {
    const auto colors=marioColors();
    bool everyPoseValid=true,smallBounds=true,deterministic=true;
    for(int pose:{0,12,5,17,8,20}) {
        std::array<std::vector<unsigned char>,24> frames;
        std::set<std::uint64_t> distinct,lower;
        for(unsigned phase=0;phase<24;++phase) {
            mario::ChibiMotion motion{};motion.nativePose=pose;motion.phase=float(phase)/24.0f;
            mario::renderChibiMotion(motion,colors,frames[phase]);
            if(frames[phase].size()!=std::size_t(AtlasWidth)*AtlasHeight*4) {
                everyPoseValid=false;continue;
            }
            distinct.insert(hash(frames[phase]));lower.insert(lowerBodyHash(frames[phase]));
            if(pose>=12)
                for(std::size_t at=3;at<std::size_t(64)*AtlasWidth*4;at+=4)
                    smallBounds=smallBounds&&frames[phase][at]==0;
            std::vector<unsigned char> repeated;
            mario::renderChibiMotion(motion,colors,repeated);
            deterministic=deterministic&&frames[phase]==repeated;
        }
        check("historic procedural: motion pose"+std::to_string(pose)+" has at least12 distinct artwork frames in24 samples",distinct.size()>=12);
        check("historic procedural: motion pose"+std::to_string(pose)+" visibly animates lower limbs",lower.size()>=6);
        mario::ChibiMotion end{};end.nativePose=pose;end.phase=1;
        std::vector<unsigned char> endpoint;mario::renderChibiMotion(end,colors,endpoint);
        check("historic procedural: motion pose"+std::to_string(pose)+" cyclic endpoint exactly returns to phase0",endpoint==frames[0]);
        std::uint64_t totalDistance=0,maxDistance=0;
        if(everyPoseValid)
            for(unsigned phase=0;phase<24;++phase) {
                const auto distance=alphaDistance(frames[phase],frames[(phase+1)%24]);
                totalDistance+=distance;maxDistance=std::max(maxDistance,distance);
            }
        // An isolated jump at the cycle seam would dwarf ordinary in-between
        // changes. This measures output coverage rather than repeating rig math.
        check("historic procedural: motion pose"+std::to_string(pose)+" has no abrupt coverage jump including last-to-first phase",
              totalDistance>0&&maxDistance*24<=totalDistance*3+24);
    }
    check("historic procedural: all motion frames keep valid64x128 RGBA output",everyPoseValid);
    check("historic procedural: all small walk swim climb motion frames retain transparent upper64 rows",smallBounds);
    check("historic procedural: motion rendering is deterministic without retained animator history",deterministic);
    bool vertical=true;
    for(int pose:{4,16,22}) {
        std::set<std::uint64_t> variants;
        for(int speed:{-4,0,4}) {
            mario::ChibiMotion motion{};motion.nativePose=pose;motion.verticalSpeed=float(speed);
            std::vector<unsigned char> rgba;mario::renderChibiMotion(motion,colors,rgba);
            variants.insert(hash(rgba));
        }
        vertical=vertical&&variants.size()==3;
    }
    check("historic procedural: jump and death artwork distinguishes ascent apex and fall from native vertical speed",vertical);

    // A labelled filename/contact-sheet sequence supports visual acceptance of
    // gait and hand anatomy; numerical continuity alone cannot judge anatomy.
    constexpr int sheetWidth=AtlasWidth*12,sheetHeight=AtlasHeight*4;
    std::vector<unsigned char> sheet(std::size_t(sheetWidth)*sheetHeight*3);
    for(int y=0;y<sheetHeight;++y)for(int x=0;x<sheetWidth;++x) {
        const auto at=(std::size_t(y)*sheetWidth+x)*3;
        const unsigned char tone=((x/8+y/8)&1)?43:34;
        sheet[at]=tone;sheet[at+1]=tone+7;sheet[at+2]=tone+20;
    }
    for(unsigned small=0;small<2;++small)for(unsigned phase=0;phase<24;++phase) {
        mario::ChibiMotion motion{};motion.nativePose=small?12:0;motion.phase=float(phase)/24.0f;
        std::vector<unsigned char> rgba;mario::renderChibiMotion(motion,colors,rgba);
        for(int y=0;y<AtlasHeight;++y)for(int x=0;x<AtlasWidth;++x) {
            const auto source=(std::size_t(y)*AtlasWidth+x)*4;
            const int targetX=int(phase%12)*AtlasWidth+x,targetY=int(small*2+phase/12)*AtlasHeight+y;
            const auto target=(std::size_t(targetY)*sheetWidth+targetX)*3;
            const unsigned alpha=rgba[source+3];
            for(unsigned channel=0;channel<3;++channel)
                sheet[target+channel]=static_cast<unsigned char>((unsigned(rgba[source+channel])*alpha+unsigned(sheet[target+channel])*(255-alpha)+127)/255);
        }
    }
    std::ofstream preview(output/"historic-procedural-walking-motion24.ppm",std::ios::binary);
    preview<<"P6\n"<<sheetWidth<<" "<<sheetHeight<<"\n255\n";
    preview.write(reinterpret_cast<const char*>(sheet.data()),static_cast<std::streamsize>(sheet.size()));
    check("historic procedural: 24-frame large and small walking contact sheet generated",bool(preview));
}

// Check the actual imported artwork independently of native pose recognition.
// Missing actions intentionally alias the user's supplied whole frames.
void suppliedSheetTests(const std::filesystem::path& output) {
    const auto colors=marioColors();
    bool sizes=true,smallBounds=true,shortBounds=true,alpha=true,deterministic=true,indices=true;
    for(int pose=0;pose<26;++pose)for(unsigned phase=0;phase<24;++phase) {
        mario::ChibiMotion motion{};motion.nativePose=pose;motion.phase=float(phase)/24;
        const auto selected=mario::selectSheetFrame(motion);
        indices=indices&&selected.sheet<3&&selected.frame<(selected.sheet==0?mario::WalkSheetFrames:selected.sheet==1?mario::JumpSheetFrames:mario::RunSheetFrames);
        std::vector<unsigned char> rgba,again;
        mario::renderSheetMotion(motion,colors,rgba);
        mario::renderSheetMotion(motion,colors,again);
        deterministic=deterministic&&rgba==again;
        sizes=sizes&&rgba.size()==std::size_t(AtlasWidth)*AtlasHeight*4;
        if(!sizes)continue;
        unsigned covered=0,partial=0;
        for(std::size_t at=3;at<rgba.size();at+=4) {
            covered+=rgba[at]!=0;partial+=rgba[at]>0&&rgba[at]<255;
            const auto row=at/(AtlasWidth*4);
            if(pose>=12&&pose<=23&&row<64)smallBounds=smallBounds&&rgba[at]==0;
            if((pose==10||pose==24)&&row<32)shortBounds=shortBounds&&rgba[at]==0;
        }
        alpha=alpha&&covered>300&&partial>10;
    }
    check("production sheets return fixed64x128 RGBA for every native pose and phase",sizes);
    check("production sheet indices stay inside all imported walk jump and run frames",indices);
    check("production small aliases keep their upper64 rows transparent",smallBounds);
    check("production crouch and growth aliases keep their upper32 rows transparent",shortBounds);
    check("production sheets preserve visible silhouettes and partial-alpha contour pixels",alpha);
    check("production sheet rendering is deterministic without persistent animator state",deterministic);
    for(int pose:{0,12}) {
        std::set<std::uint64_t> frames,feet;
        std::vector<unsigned char> first;
        for(unsigned phase=0;phase<24;++phase) {
            mario::ChibiMotion motion{};motion.nativePose=pose;motion.phase=float(phase)/24;
            std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
            frames.insert(hash(rgba));feet.insert(lowerBodyHash(rgba));
            if(!phase)first=rgba;
        }
        mario::ChibiMotion endpoint{};endpoint.nativePose=pose;endpoint.phase=1;
        std::vector<unsigned char> last;mario::renderSheetMotion(endpoint,colors,last);
        check("production walk pose"+std::to_string(pose)+" animates at least12 supplied frames",frames.size()>=12);
        check("production walk pose"+std::to_string(pose)+" visibly changes feet",feet.size()>=6);
        check("production walk pose"+std::to_string(pose)+" phase1 wraps exactly to phase0",first==last);
    }
    for(int pose:{4,16}) {
        std::set<std::uint64_t> stages;
        for(int speed:{-5,0,5}) {
            mario::ChibiMotion motion{};motion.nativePose=pose;motion.verticalSpeed=float(speed);
            std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
            stages.insert(hash(rgba));
        }
        check("production jump pose"+std::to_string(pose)+" distinguishes rising apex falling supplied frames",stages.size()==3);
    }
    // The production contact sheet is separate from the historical vector
    // export so visual inspection cannot accidentally accept the wrong art.
    constexpr int sheetWidth=AtlasWidth*12,sheetHeight=AtlasHeight*4;
    std::vector<unsigned char> sheet(std::size_t(sheetWidth)*sheetHeight*3);
    for(int y=0;y<sheetHeight;++y)for(int x=0;x<sheetWidth;++x) {
        const auto at=(std::size_t(y)*sheetWidth+x)*3;
        const unsigned char tone=((x/8+y/8)&1)?43:34;
        sheet[at]=tone;sheet[at+1]=tone+7;sheet[at+2]=tone+20;
    }
    for(unsigned small=0;small<2;++small)for(unsigned phase=0;phase<24;++phase) {
        mario::ChibiMotion motion{};motion.nativePose=small?12:0;motion.phase=float(phase)/24;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
        for(int y=0;y<AtlasHeight;++y)for(int x=0;x<AtlasWidth;++x) {
            const auto from=(std::size_t(y)*AtlasWidth+x)*4;
            const int targetX=int(phase%12)*AtlasWidth+x,targetY=int(small*2+phase/12)*AtlasHeight+y;
            const auto to=(std::size_t(targetY)*sheetWidth+targetX)*3;
            const unsigned opacity=rgba[from+3];
            for(unsigned channel=0;channel<3;++channel)
                sheet[to+channel]=static_cast<unsigned char>((unsigned(rgba[from+channel])*opacity+unsigned(sheet[to+channel])*(255-opacity)+127)/255);
        }
    }
    std::ofstream preview(output/"walking-motion24.ppm",std::ios::binary);
    preview<<"P6\n"<<sheetWidth<<" "<<sheetHeight<<"\n255\n";
    preview.write(reinterpret_cast<const char*>(sheet.data()),static_cast<std::streamsize>(sheet.size()));
    check("production supplied-frame walking contact sheet generated",bool(preview));
}


// Outfit palettes must leave reference face, eyes, hair and shoes intact.
// Compare rendered pixels, rather than duplicating the material-tint rules.
void suppliedPaletteTests() {
    const auto originalColors=marioColors();
    for(int pose:{0,25,4,12,23,16}) {
        mario::ChibiMotion motion{};motion.nativePose=pose;
        if(pose==4||pose==16)motion.verticalSpeed=0;
        std::vector<unsigned char> original;
        mario::renderSheetMotion(motion,originalColors,original);
        int top=AtlasHeight,bottom=-1;
        for(int y=0;y<AtlasHeight;++y)for(int x=0;x<AtlasWidth;++x)
            if(original[(y*AtlasWidth+x)*4+3]) {top=std::min(top,y);bottom=std::max(bottom,y);}
        // Reference anatomy occupies a shorter upper band in the realistic
        // footage. Pixel centers and protected materials avoid measuring
        // a red shirt or blue collar as part of the face.
        const int faceStart=top+((bottom-top)*20+49)/100;
        const int faceEnd=top+((bottom-top)*42-50)/100;
        auto protectedFacePixel=[&](std::size_t at) {
            const int r=original[at],g=original[at+1],b=original[at+2];
            if(!original[at+3])return false;
            const bool neutral=std::max({r,g,b})-std::min({r,g,b})<16;
            const bool brownOrSkin=r>20&&r>=g+6&&g>=b+2&&r<g*2;
            const bool iris=b>g+15&&b>r+20;
            return neutral||brownOrSkin||iris;
        };
        auto brownShoePixel=[&](std::size_t at) {
            return original[at+3]!=0&&original[at]>20&&
                   original[at]>=original[at+1]+6&&original[at]>=original[at+2]+8;
        };
        bool face=true,shoes=true,alpha=true,outfit=true;
        unsigned faceCoverage=0,shoeCoverage=0;
        for(int y=faceStart;y<=faceEnd;++y)for(int x=0;x<AtlasWidth;++x)
            faceCoverage+=protectedFacePixel((std::size_t(y)*AtlasWidth+x)*4);
        for(int y=std::max(top,bottom-7);y<=bottom;++y)for(int x=0;x<AtlasWidth;++x)
            shoeCoverage+=brownShoePixel((std::size_t(y)*AtlasWidth+x)*4);
        for(unsigned variant=0;variant<3;++variant) {
            auto colors=originalColors;
            if(variant==0) {colors.luigi=true;colors.cap=colors.shirt={40,162,73};}
            else {
                colors.luigi=variant==2;colors.cap=colors.shirt={246,240,225};
                colors.overalls=variant==2?std::array<unsigned char,3>{40,162,73}:
                                           std::array<unsigned char,3>{214,48,47};
            }
            std::vector<unsigned char> recolored;mario::renderSheetMotion(motion,colors,recolored);
            unsigned changed=0;
            for(std::size_t at=0;at<original.size();at+=4) {
                alpha=alpha&&original[at+3]==recolored[at+3];
                changed+=std::memcmp(original.data()+at,recolored.data()+at,3)!=0;
            }
            outfit=outfit&&changed>20;
            for(int y=faceStart;y<=faceEnd;++y)for(int x=0;x<AtlasWidth;++x) {
                const auto at=(std::size_t(y)*AtlasWidth+x)*4;
                if(protectedFacePixel(at)) {
                    const bool same=!std::memcmp(original.data()+at,recolored.data()+at,4);
                    if(face&&!same)std::cout<<"FACE_DIAGNOSTIC pose "<<pose<<" form "<<variant<<" xy "<<x<<","<<y<<" source "<<unsigned(original[at])<<","<<unsigned(original[at+1])<<","<<unsigned(original[at+2])<<" rendered "<<unsigned(recolored[at])<<","<<unsigned(recolored[at+1])<<","<<unsigned(recolored[at+2])<<"\n";
                    face=face&&same;
                }
            }
            for(int y=std::max(top,bottom-7);y<=bottom;++y)for(int x=0;x<AtlasWidth;++x) {
                const auto at=(std::size_t(y)*AtlasWidth+x)*4;
                // A lifted leg can place blue trousers in these same rows;
                // only the reference brown footwear material is protected.
                if(brownShoePixel(at))shoes=shoes&&!std::memcmp(original.data()+at,recolored.data()+at,4);
            }
        }
        const auto label="production outfit pose"+std::to_string(pose);
        check(label+" preserves reference face eye hair pixels for Luigi and both fire forms",face&&faceCoverage>20);
        check(label+" preserves reference bottom shoe pixels for Luigi and both fire forms",shoes&&shoeCoverage>20);
        check(label+" still recolors outfit without altering silhouette alpha",alpha&&outfit);
    }
}

void rendererMotionTests() {
    auto palette=canonicalPalette();
    const auto colors=marioColors();
    mario::CharacterRenderer art;art.setPatternTables(MarioGameData+0x8010);art.setPalette(palette.data());
    struct Fixture {unsigned pose,duration,timer,control;float phase;};
    // Fixed native timing cases exercise both ordinary countdown and the NMI
    // control advance that still displays the previous OAM pose.
    const std::array<Fixture,8> fixtures{{
        {0,4,3,0,0},{0,4,2,0,2.0f/24},{0,4,0,0,6.0f/24},
        {1,4,3,1,8.0f/24},{2,4,0,2,22.0f/24},
        {0,4,3,1,6.0f/24},{1,4,3,2,14.0f/24},{2,4,3,0,22.0f/24}
    }};
    bool exact=true,protectedCells=true;
    for(const auto& fixture:fixtures)for(bool left:{false,true}) {
        const auto commands=makePose(fixture.pose,30,40,left);
        const mario::PlayerAnimation animation{fixture.duration,fixture.timer,fixture.control,0,0};
        mario::ChibiMotion motion{};motion.nativePose=0;motion.phase=fixture.phase;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
        art.beginFrame();const auto blank=art.pixels();auto expected=blank;
        compositeAtlas(expected,rgba,30,40,left);
        art.preparePlayer(commands,false,false,false,true,&animation);
        for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
        exact=exact&&art.pixels()==expected;
        protectedCells=protectedCells&&insideCellsOnly(art.pixels(),blank,commands);
    }
    check("captured native animation timers select correct whole motion atlas in both directions",exact);
    check("dynamic OAM motion slices preserve original player cells",protectedCells);
    bool nativeRates=true;
    for(unsigned duration:{7u,4u,2u}) {
        std::set<std::uint64_t> frames;
        for(unsigned pose=0;pose<3;++pose)for(unsigned timer=0;timer<duration;++timer) {
            const mario::PlayerAnimation animation{duration,timer,pose,0,0};
            const auto commands=makePose(pose);
            art.beginFrame();art.preparePlayer(commands,false,false,false,true,&animation);
            for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
            frames.insert(hash(art.pixels()));
        }
        nativeRates=nativeRates&&frames.size()>=std::min(12u,duration*3);
    }
    check("native walk run and fast-run timers animate available supplied frames",nativeRates);
    auto commands=makePose(11);
    const mario::PlayerAnimation throwing{4,2,1,0,0};
    mario::ChibiMotion joined{};joined.nativePose=11;joined.phase=23.0f/24;joined.lowerPose=0;joined.lowerPhase=10.0f/24;
    std::vector<unsigned char> rgba;mario::renderSheetMotion(joined,colors,rgba);
    art.beginFrame();auto expected=art.pixels();compositeAtlas(expected,rgba,30,40);
    art.preparePlayer(commands,false,false,false,true,&throwing);
    for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
    check("moving fire throw uses one compatible whole supplied frame before OAM slicing",art.pixels()==expected);
    commands=makePose(0);
    const mario::PlayerAnimation falling{4,3,0,2,4};
    mario::ChibiMotion landing{};landing.nativePose=4;landing.verticalSpeed=4;
    mario::renderSheetMotion(landing,colors,rgba);
    art.beginFrame();expected=art.pixels();compositeAtlas(expected,rgba,30,40);
    art.preparePlayer(commands,false,false,false,true,&falling);
    for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
    check("native falling state selects landing artwork without changing recognized OAM pose",
          art.playerPose()==0&&art.pixels()==expected);
    bool swimReload=true,swimNext=true;
    for(unsigned base:{5u,17u})for(bool left:{false,true}) {
        auto swim=makePose(base,30,40,left);
        swim[left?7:6].tileidx=base==5?0x31:0x46;
        const mario::PlayerAnimation strokeEnd{4,0,0,1,0};
        art.beginFrame();art.preparePlayer(swim,false,false,true,false,&strokeEnd);
        for(unsigned i=0;i<8;++i)art.drawTile(swim[7-i],true);
        const auto countdownEnd=art.pixels();
        const mario::PlayerAnimation reloaded{4,3,1,1,0};
        art.beginFrame();art.preparePlayer(swim,false,false,true,false,&reloaded);
        for(unsigned i=0;i<8;++i)art.drawTile(swim[7-i],true);
        swimReload=swimReload&&art.pixels()==countdownEnd;
        auto next=makePose(base+1,30,40,left);next[left?7:6].tileidx=base==5?0x31:0x46;
        const mario::PlayerAnimation nextStroke{4,3,1,1,0};
        mario::ChibiMotion nextMotion{};nextMotion.nativePose=int(base);nextMotion.phase=8.0f/24;
        nextMotion.lowerPose=int(base);nextMotion.lowerPhase=16.0f/24;
        mario::renderSheetMotion(nextMotion,colors,rgba);
        art.beginFrame();expected=art.pixels();compositeAtlas(expected,rgba,30,40,left);
        art.preparePlayer(next,false,false,true,false,&nextStroke);
        for(unsigned i=0;i<8;++i)art.drawTile(next[7-i],true);
        swimNext=swimNext&&art.pixels()==expected;
    }
    check("swim timer reload keeps patched feet at the end of the currently displayed OAM stroke",swimReload);
    check("next swim OAM stroke advances the copied whole frame in both forms and directions",swimNext);
    bool sizeChange=true;
    for(unsigned original:{12u,17u,23u,5u,6u,7u,24u}) {
        commands=makePose(original);
        const mario::PlayerAnimation resizing{7,6,0,0,0,true};
        const unsigned visual=original>=12&&original<=23?23:original>=5&&original<=7?25:original;
        mario::ChibiMotion resized{};resized.nativePose=int(visual);
        mario::renderSheetMotion(resized,colors,rgba);
        art.beginFrame();expected=art.pixels();compositeAtlas(expected,rgba,30,40);
        art.preparePlayer(commands,false,false,false,true,&resizing);
        for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
        sizeChange=sizeChange&&art.playerPose()==int(original)&&art.pixels()==expected;
    }
    check("native size-change swim tiles and leftover walk timers retain growth or idle artwork",sizeChange);
    bool motionColors=true;
    for(unsigned variant=0;variant<4;++variant) {
        palette=canonicalPalette();auto outfit=marioColors();
        const bool luigi=(variant&1)!=0,fire=variant>=2;
        outfit.luigi=luigi;
        if(fire) {
            palette[17]=0x37;palette[19]=0x16;
            outfit.cap=outfit.shirt={246,240,225};
            outfit.overalls=luigi?std::array<unsigned char,3>{40,162,73}:std::array<unsigned char,3>{214,48,47};
        } else if(luigi) {
            palette[17]=0x30;palette[19]=0x19;outfit.cap=outfit.shirt={40,162,73};
        }
        art.setPalette(palette.data());mario::renderSheetMotion(joined,outfit,rgba);
        art.beginFrame();expected=art.pixels();compositeAtlas(expected,rgba,30,40);
        commands=makePose(11);art.preparePlayer(commands,luigi,fire,false,true,&throwing);
        for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true,luigi,fire);
        motionColors=motionColors&&art.pixels()==expected;
    }
    palette=canonicalPalette();art.setPalette(palette.data());
    auto invincibleThrow = throwing;
    invincibleThrow.starInvincible = true;
    for(unsigned group=5;group<8;++group) {
        mario::ChibiColors outfit{};outfit.luigi=true;
        std::copy_n(PALETTE_BUILTIN[palette[group*4+1]&63],3,outfit.cap.begin());
        std::copy_n(PALETTE_BUILTIN[palette[group*4+2]&63],3,outfit.shirt.begin());
        std::copy_n(PALETTE_BUILTIN[palette[group*4+3]&63],3,outfit.overalls.begin());
        mario::renderSheetMotion(joined,outfit,rgba);
        art.beginFrame();expected=art.pixels();compositeAtlas(expected,rgba,30,40);
        commands=makePose(11,30,40,false,4,int(group));art.preparePlayer(commands,true,true,false,true,&invincibleThrow);
        for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true,true,true);
        motionColors=motionColors&&art.pixels()==expected;
    }
    check("supplied motion preserves Mario Luigi fire and actual star palettes without cache mixing",motionColors);
    palette=canonicalPalette();art.setPalette(palette.data());commands=makePose(11);
    std::set<std::uint64_t> throwStages;
    std::vector<unsigned char> previous;
    bool throwTail=true;
    for(unsigned timer:{6u,5u,4u,3u,2u,1u,0u}) {
        const mario::PlayerAnimation recoil{7,3,1,0,0,false,timer};
        art.beginFrame();art.preparePlayer(commands,false,true,false,true,&recoil);
        for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
        throwStages.insert(hash(art.pixels()));
        if(timer==0)throwTail=art.pixels()==previous;
        previous=art.pixels();
    }
    check("saved fire-throw timer borrows one stable compatible whole frame",throwStages.size()==1);
    check("last throw OAM at timerzero keeps the compatible recovered frame",throwTail);
}
// Behavioural acceptance for the three new actions. These tests measure actual
// silhouettes and the native-to-art interface, independently of packed data.
void realisticActionTests(const std::filesystem::path& output) {
    const auto colors=marioColors();
    bool valid=true,borders=true,transparent=true;
    for(int pose:{0,12,4,16,10,24,25})for(bool running:{false,true})for(unsigned phase=0;phase<24;++phase) {
        mario::ChibiMotion motion{};motion.nativePose=pose;motion.running=running;
        motion.phase=float(phase)/24;motion.verticalSpeed=int(phase%13)-6;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
        valid=valid&&rgba.size()==std::size_t(AtlasWidth)*AtlasHeight*4;
        if(!valid)continue;
        for(int y=0;y<AtlasHeight;++y)
            borders=borders&&rgba[(y*AtlasWidth)*4+3]==0&&rgba[(y*AtlasWidth+AtlasWidth-1)*4+3]==0;
        for(int x=0;x<AtlasWidth;++x)borders=borders&&rgba[x*4+3]==0;
        unsigned clear=0;for(std::size_t at=3;at<rgba.size();at+=4)clear+=rgba[at]==0;
        transparent=transparent&&clear>rgba.size()/8;
    }
    check("realistic walk run and jump retain fixed64x128 artwork geometry",valid);
    check("all sampled realistic heads and hands retain transparent top and side padding",borders);
    check("realistic action silhouettes leave the keyed background transparent",transparent);

    for(int pose:{0,12}) {
        std::set<std::uint64_t> walkFrames,runFrames,walkFeet,runFeet;
        unsigned different=0;
        std::vector<unsigned char> runFirst;
        for(unsigned phase=0;phase<24;++phase) {
            mario::ChibiMotion motion{};motion.nativePose=pose;motion.phase=float(phase)/24;
            std::vector<unsigned char> walk,run;mario::renderSheetMotion(motion,colors,walk);
            motion.running=true;mario::renderSheetMotion(motion,colors,run);
            walkFrames.insert(hash(walk));runFrames.insert(hash(run));
            walkFeet.insert(lowerBodyHash(walk));runFeet.insert(lowerBodyHash(run));
            different+=walk!=run;
            if(!phase)runFirst=run;
        }
        check("realistic walk pose"+std::to_string(pose)+" uses all14 supplied cycle frames",walkFrames.size()==14);
        check("realistic run pose"+std::to_string(pose)+" uses all21 supplied cycle frames",runFrames.size()==21);
        check("walk and run independently animate feet pose"+std::to_string(pose),walkFeet.size()>=8&&runFeet.size()>=12);
        check("running switches whole artwork rather than only playback rate pose"+std::to_string(pose),different==24);
        mario::ChibiMotion endpoint{};endpoint.nativePose=pose;endpoint.phase=1;endpoint.running=true;
        std::vector<unsigned char> last;mario::renderSheetMotion(endpoint,colors,last);
        check("realistic run cycle wraps exactly to its first frame pose"+std::to_string(pose),last==runFirst);
    }

    // Native ground state must keep a standing frame even if an unrelated
    // counter contains an old upward velocity. Visual preparation cannot delay
    // or anticipate the real jump input.
    auto pal=canonicalPalette();mario::CharacterRenderer art;
    art.setPatternTables(MarioGameData+0x8010);art.setPalette(pal.data());
    auto idle=makePose(25);
    const mario::PlayerAnimation grounded{4,2,1,0,-5,false,0,0};
    art.beginFrame();art.preparePlayer(idle,false,false,false,false,&grounded);
    for(const auto&t:idle)art.drawTile(t,true);
    const auto groundedPixels=art.pixels();
    art.beginFrame();drawPrepared(art,idle,25);
    check("grounded native idle cannot begin visual jump preparation before native takeoff",art.pixels()==groundedPixels);

    bool independentGaits=true,cacheIndependent=true;
    auto moving=makePose(0);
    for(bool left:{false,true})for(int speed:{24,28,40,-24,-28,-40}) {
        moving=makePose(0,30,40,left);
        const mario::PlayerAnimation native{4,2,0,0,0,false,0,speed};
        mario::ChibiMotion expected{};expected.nativePose=0;expected.phase=2.0f/24;
        expected.running=std::abs(speed)>=28;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(expected,colors,rgba);
        art.beginFrame();auto image=art.pixels();compositeAtlas(image,rgba,30,40,left);
        art.preparePlayer(moving,false,false,false,true,&native);
        for(const auto&t:moving)art.drawTile(t,true);
        independentGaits=independentGaits&&art.pixels()==image;
    }
    check("native horizontal speed selects separate walk or run frames in both directions",independentGaits);
    std::vector<unsigned char> reference;
    for(int speed:{24,40,24}) {
        const mario::PlayerAnimation native{4,2,0,0,0,false,0,speed};
        moving=makePose(0);art.beginFrame();art.preparePlayer(moving,false,false,false,true,&native);
        for(const auto&t:moving)art.drawTile(t,true);
        if(reference.empty())reference=art.pixels();
        else if(speed==24)cacheIndependent=cacheIndependent&&art.pixels()==reference;
        else cacheIndependent=cacheIndependent&&art.pixels()!=reference;
    }
    check("warming the running cache never replaces a walking frame at the same phase",cacheIndependent);

    // The running clip contains21 source poses. A native three-key timer would
    // repeatedly skip poses at full speed; the saved scene clock must expose
    // the complete ordered loop while retaining the old native controls.
    auto cadence=[&](bool running,unsigned period,unsigned count) {
        bool exact=true;std::set<std::uint64_t> supplied;
        for(unsigned frame=0;frame<period;++frame) {
            mario::WorldScene scene{};scene.frame=frame;scene.enabled=true;
            art.prepareScene(scene);art.beginFrame();auto expected=art.pixels();
            const unsigned source=frame*count/period;
            mario::ChibiMotion motion{};motion.nativePose=0;motion.running=running;
            motion.phase=(float(source)+0.5f)/count;
            std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
            supplied.insert(hash(rgba));compositeAtlas(expected,rgba,30,40);
            const unsigned pose=frame%3;
            auto commands=makePose(pose);
            const mario::PlayerAnimation native{4,(frame*3)%4,pose,0,0,false,0,running?40:24};
            art.preparePlayer(commands,false,false,false,true,&native);
            for(const auto&t:commands)art.drawTile(t,true);
            exact=exact&&art.pixels()==expected;
        }
        return exact&&supplied.size()==count;
    };
    check("active scene renders every21 supplied run poses in21 ordered native ticks",cadence(true,21,21));
    check("active scene renders every14 supplied walk poses held for two of28 native ticks",cadence(false,28,14));

    mario::WorldScene frozenScene{};frozenScene.frame=17;frozenScene.enabled=true;
    art.prepareScene(frozenScene);art.beginFrame();auto frozenExpected=art.pixels();
    mario::ChibiMotion frozenMotion{};frozenMotion.nativePose=0;frozenMotion.running=true;
    frozenMotion.phase=(17.0f+0.5f)/21;
    std::vector<unsigned char> frozenRGBA;mario::renderSheetMotion(frozenMotion,colors,frozenRGBA);
    compositeAtlas(frozenExpected,frozenRGBA,30,40);
    bool frozen=true;
    for(unsigned pose=0;pose<3;++pose) {
        auto commands=makePose(pose);
        const mario::PlayerAnimation native{7,pose,pose,0,0,false,0,40};
        art.prepareScene(frozenScene);art.beginFrame();
        art.preparePlayer(commands,false,false,false,true,&native);
        for(const auto&t:commands)art.drawTile(t,true);
        frozen=frozen&&art.pixels()==frozenExpected;
    }
    check("same saved scene clock keeps exact run frame despite native OAM timer changes",frozen);

    bool sceneIdle=true,sceneActionCache=true;
    for(unsigned frame:{0u,1u,17u,42u}) {
        mario::WorldScene scene{};scene.frame=frame;scene.enabled=true;
        auto commands=makePose(25);
        const mario::PlayerAnimation native{4,frame%4,frame%3,0,0,false,0,0};
        mario::ChibiMotion motion{};motion.nativePose=25;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
        art.prepareScene(scene);art.beginFrame();auto expected=art.pixels();
        compositeAtlas(expected,rgba,30,40);
        art.preparePlayer(commands,false,false,false,false,&native);
        for(const auto&t:commands)art.drawTile(t,true);
        sceneIdle=sceneIdle&&art.pixels()==expected;
    }
    check("advancing scenery and leftover counters never animate a grounded idle player",sceneIdle);
    for(bool running:{false,true,false}) {
        mario::WorldScene scene{};scene.frame=7;scene.enabled=true;
        auto commands=makePose(0);
        const mario::PlayerAnimation native{4,2,0,0,0,false,0,running?40:24};
        mario::ChibiMotion motion{};motion.nativePose=0;motion.running=running;
        motion.phase=running?(7.0f+0.5f)/21:(3.0f+0.5f)/14;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
        art.prepareScene(scene);art.beginFrame();auto expected=art.pixels();
        compositeAtlas(expected,rgba,30,40);
        art.preparePlayer(commands,false,false,false,true,&native);
        for(const auto&t:commands)art.drawTile(t,true);
        sceneActionCache=sceneActionCache&&art.pixels()==expected;
    }
    check("saved clock walk run walk transition keeps action caches independent",sceneActionCache);
    art.prepareScene(mario::WorldScene{0,0,1,false,false});
    bool landingContact=true,airPhases=true,airMirrors=true;
    for(unsigned pose:{4u,16u})for(bool left:{false,true}) {
        auto commands=makePose(pose,30,40,left);
        std::set<std::uint64_t> stages;
        for(int speed:{-5,-4,-2,0,1,2,4}) {
            const mario::PlayerAnimation native{4,2,1,speed<=0?1u:2u,speed,false,0,0};
            mario::ChibiMotion expected{};expected.nativePose=int(pose);expected.verticalSpeed=float(speed);
            std::vector<unsigned char> rgba;mario::renderSheetMotion(expected,colors,rgba);
            art.beginFrame();auto image=art.pixels();compositeAtlas(image,rgba,30,40,left);
            art.preparePlayer(commands,false,false,false,false,&native);
            for(const auto&t:commands)art.drawTile(t,true);
            stages.insert(hash(art.pixels()));airMirrors=airMirrors&&art.pixels()==image;
        }
        airPhases=airPhases&&stages.size()==7;
        const mario::PlayerAnimation landed{4,2,1,0,0,false,0,0};
        mario::ChibiMotion contact{};contact.nativePose=int(pose);contact.landing=true;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(contact,colors,rgba);
        art.beginFrame();auto image=art.pixels();compositeAtlas(image,rgba,30,40,left);
        art.preparePlayer(commands,false,false,false,false,&landed);
        for(const auto&t:commands)art.drawTile(t,true);
        landingContact=landingContact&&art.pixels()==image;
    }
    check("native ascent apex and fall each expose seven distinct jump poses",airPhases);
    check("jump phases mirror as complete artwork in both native forms",airMirrors);
    check("first native ground contact replaces lagged jumping OAM with supplied contact pose",landingContact);
    bool gloves=true,skin=true,eyes=true;unsigned gloveCoverage=0,skinCoverage=0,eyeCoverage=0;
    for(int pose:{0,12,4,16,25})for(bool running:{false,true}) {
        mario::ChibiMotion motion{};motion.nativePose=pose;motion.running=running;motion.phase=0.5f;
        std::vector<unsigned char> original;mario::renderSheetMotion(motion,colors,original);
        int top=AtlasHeight,bottom=-1;
        for(int y=0;y<AtlasHeight;++y)for(int x=0;x<AtlasWidth;++x)
            if(original[(y*AtlasWidth+x)*4+3]) {top=std::min(top,y);bottom=std::max(bottom,y);}
        for(unsigned variant=0;variant<3;++variant) {
            auto outfit=colors;
            outfit.luigi=variant!=1;
            outfit.cap=outfit.shirt=variant==0?std::array<unsigned char,3>{40,162,73}:std::array<unsigned char,3>{246,240,225};
            outfit.overalls=variant==1?std::array<unsigned char,3>{214,48,47}:std::array<unsigned char,3>{40,162,73};
            std::vector<unsigned char> tinted;mario::renderSheetMotion(motion,outfit,tinted);
            for(int y=top;y<=bottom;++y)for(int x=0;x<AtlasWidth;++x) {
                const auto at=(std::size_t(y)*AtlasWidth+x)*4;
                if(original[at+3]<150)continue;
                const int r=original[at],g=original[at+1],b=original[at+2];
                const bool identical=!std::memcmp(original.data()+at,tinted.data()+at,4);
                // White gloves and cap emblem remain neutral in all forms.
                if(std::min({r,g,b})>100&&std::max({r,g,b})-std::min({r,g,b})<16) {gloves=gloves&&identical;++gloveCoverage;}
                // Warm light skin avoids red fabric and brown dark boots.
                if(y>=top+(bottom-top)*20/100&&y<top+(bottom-top)*42/100&&r>125&&g>65&&b>35&&r>g+8&&g>b+8&&r<g*2) {skin=skin&&identical;++skinCoverage;}
                if(y<top+(bottom-top)*42/100&&b>g+15&&b>r+20) {eyes=eyes&&identical;++eyeCoverage;}
            }
        }
    }
    check("Luigi and fire forms preserve neutral glove and emblem pixels",gloves&&gloveCoverage>100);
    check("Luigi and fire forms preserve warm face skin across actions",skin&&skinCoverage>100);
    check("Luigi and fire forms preserve upper-body blue iris pixels",eyes&&eyeCoverage>10);

    constexpr int sheetWidth=AtlasWidth*21,sheetHeight=AtlasHeight*4;
    std::vector<unsigned char> sheet(std::size_t(sheetWidth)*sheetHeight*3,35);
    for(unsigned form=0;form<2;++form)for(unsigned running=0;running<2;++running)for(unsigned index=0;index<21;++index) {
        mario::ChibiMotion motion{};motion.nativePose=form?12:0;motion.running=running!=0;motion.phase=float(index)/21;
        std::vector<unsigned char> rgba;mario::renderSheetMotion(motion,colors,rgba);
        for(int y=0;y<AtlasHeight;++y)for(int x=0;x<AtlasWidth;++x) {
            const auto src=(std::size_t(y)*AtlasWidth+x)*4;
            const auto dst=(std::size_t((form*2+running)*AtlasHeight+y)*sheetWidth+int(index)*AtlasWidth+x)*3;
            const unsigned alpha=rgba[src+3];
            for(unsigned c=0;c<3;++c)sheet[dst+c]=static_cast<unsigned char>((unsigned(rgba[src+c])*alpha+unsigned(sheet[dst+c])*(255-alpha)+127)/255);
        }
    }
    std::ofstream preview(output/"realistic-ground-actions.ppm",std::ios::binary);
    preview<<"P6\n"<<sheetWidth<<" "<<sheetHeight<<"\n255\n";
    preview.write(reinterpret_cast<const char*>(sheet.data()),static_cast<std::streamsize>(sheet.size()));
    check("actual production realistic walk and run contact sheet generated",bool(preview));
}
void tests(const std::filesystem::path& output) {
    auto palette=canonicalPalette();
    mario::CharacterRenderer art;
    art.setPatternTables(MarioGameData+0x8010);art.setPalette(palette.data());
    NativeRaster native;SMBraster_update_palette(native.raster,palette.data());
    bool ordinary=true;
    for(int tile=0;tile<512;++tile)for(unsigned flips=0;flips<4;++flips) {
        const int x=tile%3==0?-3:tile%3==1?253:30;
        const int y=tile%3==0?-2:tile%3==1?237:40;
        auto command=makeTile(tile,x,y,tile%8,(flips&1)!=0,(flips&2)!=0);
        if(tile>=256)command.extra_type=TILE_TYPE_BG;
        SMBraster_clear(native.raster);SMBraster_draw_tile(native.raster,command);
        art.beginFrame();art.drawTile(command,false);
        if(art.pixels()!=scaled(native.pixels))ordinary=false;
    }
    check("all512 native tiles preserve exact4x colors flips clipping",ordinary);

    bool recognized=true,seamless=true,mirrors=true,bounds=true,smooth=true,finite=true,order=true;

    std::vector<unsigned char> rgba;
    const auto colors=marioColors();
    for(unsigned pose=0;pose<26;++pose) {
        mario::ChibiMotion staticMotion{};staticMotion.nativePose=int(pose);
        mario::renderSheetMotion(staticMotion,colors,rgba);
        finite=finite&&rgba.size()==std::size_t(AtlasWidth)*AtlasHeight*4;
        if(!finite)break;
        unsigned covered=0,softEdges=0;
        for(std::size_t p=3;p<rgba.size();p+=4){covered+=rgba[p]!=0;softEdges+=rgba[p]!=0&&rgba[p]!=255;}
        smooth=smooth&&covered>300&&softEdges>10;
        art.beginFrame();const auto blank=art.pixels();
        auto expected=blank;compositeAtlas(expected,rgba,30,40);
        const auto commands=makePose(pose);
        drawPrepared(art,commands,pose);
        recognized=recognized&&art.playerPose()==int(pose)&&!art.playerMirrored();
        seamless=seamless&&art.pixels()==expected;
        bounds=bounds&&insideCellsOnly(art.pixels(),blank,commands);
        const auto descending=art.pixels();
        art.beginFrame();drawPrepared(art,commands,pose,false,false,false);
        order=order&&art.pixels()==descending;
        art.beginFrame();expected=art.pixels();compositeAtlas(expected,rgba,30,40,true);
        const auto left=makePose(pose,30,40,true);
        drawPrepared(art,left,pose);
        recognized=recognized&&art.playerPose()==int(pose)&&art.playerMirrored();
        mirrors=mirrors&&art.pixels()==expected;
    }
    check("all26 large small swim climb crouch throw growth death poses recognized both directions",recognized);
    check("all26 poses match complete RGBA atlas without8x8 seams",finite&&seamless);
    check("native symmetry attributes do not independently mirror character limbs",mirrors);
    check("independent native cell draw order preserves same assembled artwork",order);
    check("all26 supplied or aliased silhouettes include antialiased translucent edges",smooth);
    check("all26 replacement silhouettes remain within original16x32 OAM cells",bounds);
    check("realistic artwork adds distinguishable face cloth glove and boot shades",countColors(art.pixels())>40);

    art.beginFrame();auto commands=makePose(25);drawPrepared(art,commands,25);const auto mario=art.pixels();
    palette[17]=0x30;palette[19]=0x19;art.setPalette(palette.data());
    art.beginFrame();drawPrepared(art,commands,25,true);const auto luigi=art.pixels();
    check("Luigi retains distinct green outfit",mario!=luigi);
    palette[17]=0x37;palette[19]=0x16;art.setPalette(palette.data());
    art.beginFrame();drawPrepared(art,commands,25,false,true);const auto fire=art.pixels();
    check("fire Mario has distinct white cap and red overalls",fire!=mario&&fire!=luigi);
    art.beginFrame();drawPrepared(art,commands,25,true,true);
    check("fire Luigi has distinct green overalls",art.pixels()!=fire&&art.pixels()!=luigi);
    bool star=true;std::vector<unsigned char> previous;
    for(int group=5;group<8;++group) {
        commands=makePose(25,30,40,false,4,group);
        art.beginFrame();drawPrepared(art,commands,25,true,true);
        if(!previous.empty()&&previous==art.pixels())star=false;
        previous=art.pixels();
    }
    check("star invincibility preserves actual changing sprite palettes",star);
    palette=canonicalPalette();art.setPalette(palette.data());SMBraster_update_palette(native.raster,palette.data());

    bool patches=true;
    for(unsigned pose:{5u,6u,7u,17u,18u,19u})for(bool left:{false,true}) {
        commands=makePose(pose,30,40,left);
        if(pose!=19)commands[left?7:6].tileidx=pose<12?0x31:0x46;
        art.beginFrame();drawPrepared(art,commands,pose);
        patches=patches&&art.playerPose()==int(pose);
    }
    check("native alternate swim feet keep correct large and small animation poses",patches);
    commands=makePose(11);commands[6].tileidx=0x16;commands[7].tileidx=0x17;
    art.beginFrame();drawPrepared(art,commands,11);
    check("moving fire throw recognized when native running legs remain",art.playerPose()==11);
    commands=makePose(5);art.beginFrame();art.preparePlayer(commands,false,false,false);
    check("exact swim signature stays swimming before mixed throw fallback",art.playerPose()==5);
    art.beginFrame();art.preparePlayer(commands,false,false,true);
    check("same shared signature remains swimming when native swimming context active",art.playerPose()==5);
    bool throwLegs=true;
    for(unsigned walking=0;walking<3;++walking)for(bool left:{false,true}) {
        commands=makePose(11,30,40,left);
        commands[6].tileidx=Poses[walking][left?7:6];
        commands[7].tileidx=Poses[walking][left?6:7];
        std::vector<unsigned char> throwAtlas;
        mario::ChibiMotion hybrid{};hybrid.nativePose=11;hybrid.lowerPose=int(walking);
        mario::renderSheetMotion(hybrid,colors,throwAtlas);
        art.beginFrame();auto expected=art.pixels();compositeAtlas(expected,throwAtlas,30,40,left);
        art.preparePlayer(commands,false,false,false,true);
        for(unsigned i=0;i<8;++i)art.drawTile(commands[7-i],true);
        throwLegs=throwLegs&&art.playerPose()==11&&art.pixels()==expected;
    }
    check("moving fire throws preserve compatible whole frames with native walking aliases both directions",throwLegs);
    bool animatedSwimFeet=true;
    for(unsigned pose:{5u,6u,7u,17u,18u}) {
        commands=makePose(pose);art.beginFrame();drawPrepared(art,commands,pose);
        const auto unpatched=art.pixels();commands[6].tileidx=pose<12?0x31:0x46;
        art.beginFrame();drawPrepared(art,commands,pose);
        animatedSwimFeet=animatedSwimFeet&&art.playerPose()==int(pose)&&art.pixels()==unpatched;
    }
    check("native swimming foot patches retain the compatible whole copied frame",animatedSwimFeet);

    commands=makePose(25,30,40,false,60);
    art.beginFrame();const auto blank=art.pixels();drawPrepared(art,commands,25);const auto wrappedSlots=art.pixels();
    art.beginFrame();drawPrepared(art,makePose(25),25);
    check("eight player sprite indices correctly wrap from slot63 to slot0",art.pixels()==wrappedSlots);
    commands=makePose(25,248,40);
    for(auto&command:commands)command.x&=255;
    art.beginFrame();drawPrepared(art,commands,25);
    check("screen-edge horizontal OAM wrapping draws only its original cells",insideCellsOnly(art.pixels(),blank,commands)&&art.pixels()!=blank);
    bool clips=true;
    for(const auto position:std::array<std::array<int,2>,4>{{{-3,-2},{253,237},{30,-5},{-4,60}}}) {
        commands=makePose(25,position[0],position[1]);
        art.beginFrame();drawPrepared(art,commands,25);
        clips=clips&&insideCellsOnly(art.pixels(),blank,commands);
    }
    check("negative and right bottom screen clipping keeps image within OAM cells",clips);
    commands=makePose(25);for(auto&command:commands)command.y=249;
    art.beginFrame();drawPrepared(art,commands,25);
    check("native hidden OAM rows keep injury blink frame fully invisible",art.pixels()==blank);
    commands=makePose(25);for(unsigned i=0;i<4;++i)commands[i].y=249;
    art.beginFrame();drawPrepared(art,commands,25);
    bool partial=true;
    for(int y=40*Scale;y<56*Scale;++y)for(int x=30*Scale;x<46*Scale;++x) {
        const auto p=(std::size_t(y)*Width+x)*3;
        if(std::memcmp(art.pixels().data()+p,blank.data()+p,3))partial=false;
    }
    check("native per-row hiding preserves partial pipe and offscreen occlusion",partial&&art.pixels()!=blank);
    art.beginFrame();drawPrepared(art,makePose(12),12);
    bool small=true;
    for(int y=40*Scale;y<56*Scale;++y)for(int x=30*Scale;x<46*Scale;++x) {
        const auto p=(std::size_t(y)*Width+x)*3;if(std::memcmp(art.pixels().data()+p,blank.data()+p,3))small=false;
    }
    check("small character uses only lower16 native pixels",small&&art.pixels()!=blank);
    art.beginFrame();drawPrepared(art,makePose(10),10);
    bool crouch=true;
    for(int y=40*Scale;y<48*Scale;++y)for(int x=30*Scale;x<46*Scale;++x) {
        const auto p=(std::size_t(y)*Width+x)*3;if(std::memcmp(art.pixels().data()+p,blank.data()+p,3))crouch=false;
    }
    check("crouching character leaves upper native row transparent",crouch&&art.pixels()!=blank);

    art.beginFrame();drawPrepared(art,makePose(25),25);
    auto covering=makeTile(0x120,30,40,1);covering.extra_type=TILE_TYPE_BG;
    art.drawTile(covering,false);SMBraster_clear(native.raster);SMBraster_draw_tile(native.raster,covering);
    const auto coveredNative=scaled(native.pixels);
    bool occlusion=true;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(sample(covering.tileidx,x,y))for(int sy=0;sy<Scale;++sy)for(int sx=0;sx<Scale;++sx) {
        const auto p=(std::size_t((40+y)*Scale+sy)*Width+(30+x)*Scale+sx)*3;
        if(std::memcmp(art.pixels().data()+p,coveredNative.data()+p,3))occlusion=false;
    }
    check("later opaque background pixels fully occlude alpha character artwork",occlusion);
    auto before=art.pixels();
    check("invalid presentation restore keeps previous frame",!art.restorePixels(before.data(),before.size()-1)&&art.pixels()==before);
    art.beginFrame();check("presentation save frame restores exactly without native tick",art.restorePixels(before.data(),before.size())&&art.pixels()==before);
    art.drawTile(makeTile(512),true);art.drawTile(makeTile(-1),true);art.drawTile(makeTile(0,0,0,8),true);
    check("invalid tile indices leave valid presentation frame untouched",art.pixels()==before);
    art.beginFrame();art.drawTile(makeTile(0x3a),true);
    SMBraster_clear(native.raster);SMBraster_draw_tile(native.raster,makeTile(0x3a));
    check("unprepared player draw safely falls back to original native pixels",art.pixels()==scaled(native.pixels));
    commands=makePose(25);for(auto&command:commands)command.tileidx=0x70;
    art.beginFrame();art.preparePlayer(commands);const auto unknown=art.playerPose();
    for(const auto&command:commands)art.drawTile(command,true);
    SMBraster_clear(native.raster);for(const auto&command:commands)SMBraster_draw_tile(native.raster,command);
    check("unknown player tile signatures safely retain original native art",unknown<0&&art.pixels()==scaled(native.pixels));

    std::vector<unsigned char> legacy(512u*480u*3u);
    for(std::size_t i=0;i<legacy.size();++i)legacy[i]=static_cast<unsigned char>((i*31u+i/17u)&255u);
    check("legacy512 save presentation restores without allocating",art.restoreScaledPixels(legacy.data(),512,480));
    bool legacyExact=true;
    for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {
        const auto input=(std::size_t(y/2)*512+x/2)*3,outputPixel=(std::size_t(y)*Width+x)*3;
        if(std::memcmp(legacy.data()+input,art.pixels().data()+outputPixel,3))legacyExact=false;
    }
    check("legacy512 presentation remains exact nearest scaled image",legacyExact);
    before=art.pixels();
    check("invalid scaled restore rejects without changing frame",!art.restoreScaledPixels(legacy.data(),0,480)&&!art.restoreScaledPixels(legacy.data(),511,480)&&!art.restoreScaledPixels(nullptr,512,480)&&art.pixels()==before);

    // All contact-sheet poses are actual animation-table entries. Small, crouch
    // and growth poses retain their original row occupancy and foot baseline.
    art.beginFrame();
    for(unsigned pose=0;pose<26;++pose)for(unsigned player=0;player<2;++player) {
        palette=canonicalPalette();if(player){palette[17]=0x30;palette[19]=0x19;}art.setPalette(palette.data());
        commands=makePose(pose,int(pose%5)*50+int(player)*20+3,int(pose/5)*39+3);
        drawPrepared(art,commands,pose,player!=0);
    }
    std::ofstream preview(output/"character-poses.ppm",std::ios::binary);
    preview<<"P6\n"<<Width<<" "<<Height<<"\n255\n";preview.write(reinterpret_cast<const char*>(art.pixels().data()),static_cast<std::streamsize>(art.pixels().size()));
    check("26 pose Mario and Luigi realistic contact sheet generated",bool(preview));
    palette=canonicalPalette();art.setPalette(palette.data());art.beginFrame();
    drawPrepared(art,makePose(25,24,16),25);
    constexpr int cropWidth=24*Scale,cropHeight=40*Scale;
    std::ofstream hero(output/"character-hero.ppm",std::ios::binary);
    hero<<"P6\n"<<cropWidth<<" "<<cropHeight<<"\n255\n";
    for(int row=0;row<cropHeight;++row)hero.write(reinterpret_cast<const char*>(art.pixels().data()+((12*Scale+row)*Width+20*Scale)*3),cropWidth*3);
    check("realistic character hero crop generated",bool(hero));
}
struct Game {
    SMB_state* state=static_cast<SMB_state*>(std::malloc(SMB_state_size()));
    NativeRaster native;
    mario::CharacterRenderer art;
    SMB_buttons buttons{};
    std::vector<unsigned char> allowed=std::vector<unsigned char>(256*240);
    unsigned offset=4;bool luigi=false,fire=false,visual=true,valid=false;
    std::size_t position=0;
    static bool read(void*p,u8*out,std::size_t size){auto&g=*static_cast<Game*>(p);if(g.position>MarioGameDataSize||size>MarioGameDataSize-g.position)return false;std::memcpy(out,MarioGameData+g.position,size);g.position+=size;return true;}
    static bool seek(void*p,std::size_t at){auto&g=*static_cast<Game*>(p);if(at>MarioGameDataSize)return false;g.position=at;return true;}
    static void input(void*p,SMB_buttons*b){*b=static_cast<Game*>(p)->buttons;}
    static void patterns(void*p,const u8*chr){auto&g=*static_cast<Game*>(p);SMBraster_update_pattern_tables(g.native.raster,chr);g.art.setPatternTables(chr);}
    static void palette(void*p,const u8*indices){auto&g=*static_cast<Game*>(p);SMBraster_update_palette(g.native.raster,indices);g.art.setPalette(indices);}
    static void tile(void*p,SMB_tile t){auto&g=*static_cast<Game*>(p);SMBraster_draw_tile(g.native.raster,t);if(!g.visual)return;const bool player=t.extra_type==TILE_TYPE_SPRITE&&((unsigned(t.extra_spriteidx)+64-(g.offset/4))&63)<8;g.art.drawTile(t,player,g.luigi,g.fire);spriteMask(g.allowed,t,player);}
    Game(bool enhanced=true):visual(enhanced){SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=input;cb.update_pattern_tables=patterns;cb.update_palette=palette;cb.draw_tile=tile;valid=state&&SMB_state_init(state,&cb);}
    ~Game(){std::free(state);}
    void tick(SMB_buttons b={}) {
        buttons=b;auto*ram=SMB_ram(state);offset=ram[0x6e4];if(!offset)offset=4;
        luigi=ram[0x753]!=0;fire=ram[0x756]==2;
        std::array<SMB_tile,8> commands{};
        for(unsigned i=0;i<8;++i) {
            const unsigned at=0x200+((offset+i*4)&255),attr=ram[at+2];
            commands[i]=makeTile(ram[at+1],ram[at+3],int(ram[at])+1,4+int(attr&3),(attr&64)!=0,(attr&128)!=0);
            commands[i].extra_spriteidx=static_cast<unsigned char>(((offset/4)+i)&63);
        }
        std::fill(allowed.begin(),allowed.end(),0);SMBraster_clear(native.raster);art.beginFrame();
        const mario::PlayerAnimation animation{ram[0x70C],ram[0x781],ram[0x70D],ram[0x1D],
                                                int(static_cast<std::int8_t>(ram[0x9F])),ram[0x70B]!=0,ram[0x711],
                                                int(static_cast<std::int8_t>(ram[0x57])),ram[0x433],ram[0x79F]!=0};
        if(visual)art.preparePlayer(commands,luigi,fire,ram[0x704]!=0,ram[0x57]!=0,&animation);
        SMB_tick(state);
    }
    bool start(){if(!valid)return false;for(unsigned i=0;i<35;++i)tick();SMB_buttons b{};b.start=true;tick(b);tick(b);tick();for(unsigned i=0;i<1200;++i){if(SMB_ram(state)[0x770]==1&&SMB_ram(state)[0xe]==8)return true;tick();}return false;}
};
void nativeIntegration() {
    Game upgraded,original(false),resumed;
    const bool ready=upgraded.start()&&original.start();check("production native game starts in dual render fixture",ready);if(!ready)return;
    bool exact=true,limited=true,phaseSaved=false,phaseContinued=true;unsigned changed=0,changedFrames=0;
    std::vector<unsigned char> snapshot(SMB_snapshot_size());
    for(unsigned frame=0;frame<140;++frame) {
        SMB_buttons b{};b.r=true;b.b=true;b.a=frame%70>=18&&frame%70<31;
        upgraded.tick(b);original.tick(b);
        if(frame==22) {
            phaseSaved=SMB_snapshot_export(upgraded.state,snapshot.data(),snapshot.size())&&
                       SMB_snapshot_import(resumed.state,snapshot.data(),snapshot.size())&&
                       resumed.art.restorePixels(upgraded.art.pixels().data(),upgraded.art.pixels().size());
            std::copy(upgraded.native.pixels.begin(),upgraded.native.pixels.end(),resumed.native.pixels.begin());
        } else if(frame>22&&frame<=31&&phaseSaved) {
            resumed.tick(b);
            phaseContinued=phaseContinued&&
                !std::memcmp(SMB_ram(upgraded.state),SMB_ram(resumed.state),0x10000)&&
                !std::memcmp(SMB_ppuram(upgraded.state),SMB_ppuram(resumed.state),0x4000)&&
                upgraded.native.pixels==resumed.native.pixels&&upgraded.art.pixels()==resumed.art.pixels();
        }
        if(std::memcmp(SMB_ram(upgraded.state),SMB_ram(original.state),0x10000)||std::memcmp(SMB_ppuram(upgraded.state),SMB_ppuram(original.state),0x4000)||upgraded.native.pixels!=original.native.pixels)exact=false;
        const auto before=scaled(upgraded.native.pixels);unsigned frameChanged=0;
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {
            const auto p=(std::size_t(y)*Width+x)*3;
            if(std::memcmp(upgraded.art.pixels().data()+p,before.data()+p,3)) {
                ++changed;++frameChanged;if(!upgraded.allowed[(y/Scale)*256+x/Scale])limited=false;
            }
        }
        if(frameChanged)++changedFrames;
    }
    check("140 visual gameplay frames leave all native RAM PPU framebuffer exact",exact);
    check("actual realistic changes stay only inside unoccluded player OAM cells",limited&&changed>1000&&changedFrames>70);
    check("native snapshot restores active animation counters and visible presentation",phaseSaved);
    check("first9 resumed native ticks preserve exact motion RAM PPU and presentation frames",phaseSaved&&phaseContinued);
}
} // namespace
int main(int argc,char**argv){try{const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("character-verification");std::filesystem::create_directories(output);tests(output);suppliedSheetTests(output);suppliedPaletteTests();historicProceduralMotionTests(output);rendererMotionTests();realisticActionTests(output);nativeIntegration();std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}}
