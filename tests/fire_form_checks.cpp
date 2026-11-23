#include "character_art.h"
#include "sheet_art.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void check(const std::string& text,bool result){++checks;if(!result)++failures;std::cout<<(result?"PASS ":"FAIL ")<<text<<'\n';}
std::uint64_t hash(const std::vector<unsigned char>& a){std::uint64_t h=1469598103934665603ull;for(auto p:a){h^=p;h*=1099511628211ull;}return h;}
// Recorded from the delivered original outfit before the fire material repair.
// These fixtures independently lock all 244 reference RGBA frames and geometry.
constexpr std::uint64_t OriginalHashes[244]={
0x6e8b74b0a6f28554ull,0x98c912fa27abb375ull,0x143c6a7b844fe90full,0x8354642f1a1d7391ull,
0xefa77789753d5290ull,0xd13e536577c8bda8ull,0x1d9977d324ad06faull,0xb9f53f4be96596a9ull,
0x021d993a81d465c4ull,0xe3c9c0950b517b3eull,0x7528594fe488ae97ull,0x4dd24630ed40cb9bull,
0xd75575e53927db30ull,0x7abb68fd062d0c14ull,0x04765e2ccbbcedc4ull,0x4ac401870e14cb12ull,
0x2521106d619d1f09ull,0xf878ab3479651381ull,0x283343a31f60bb48ull,0x7145236a189c9c14ull,
0xe6e4d5646d112a4dull,0x40c6867941ad1bb8ull,0x6d5b5348f9da3607ull,0xd4f08b759f4b180dull,
0x2b2ef21a4fa020e6ull,0xaee4552cafae44a1ull,0xff296a0884446013ull,0x6cfdd8ee88b77b0full,
0xbc72cf2b65878702ull,0x3169b8bb72ba93a0ull,0xeaa9952d0623a748ull,0x61b2974e24b84ab1ull,
0xc7cf258bce491ba7ull,0x88865f39850bdac7ull,0x400841e98c5cefb1ull,0x47c8c62da2278b28ull,
0xde343e2741b9c871ull,0xbc26b84c87ebec07ull,0xbf216681f1bed683ull,0xacb641da82125d06ull,
0xd429b734097c5456ull,0x7c0eee02fab8b2b9ull,0xf61003670d459214ull,0xcdc31e03130de667ull,
0x3b179e0a54b77ea3ull,0x83b76083a0e2052dull,0x18d2a79b93f7d4eeull,0x135a008eb5137eabull,
0x1057c0d3f316b3b3ull,0xa0d0035b599913d6ull,0xb6b41375cbf701c2ull,0xdeb6e2ce3d02e928ull,
0x273842789c5a7ae6ull,0x3892f5cb4d7605dcull,0x06a6f14e8004b3ebull,0xc92ea0ad7cb17bdaull,
0xd479a2cda315118cull,0x590d2642798a6f51ull,0x8c00ae6c2a097ee0ull,0x4fd6e81804acd6ceull,
0x22a4c4de36698ea3ull,0xffd5f8a10fafe343ull,0x477c43d5a35c7d7full,0xb536bc2b5ba75cc0ull,
0x59d3c9ece3984addull,0x43a3e44ddc3a4701ull,0x105ef2e36070eef2ull,0xe7e5dfc453b08ce2ull,
0x985394c06c0f0a60ull,0x21bef65d03d98220ull,0x4d4d5d69fa9acab9ull,0x35be300eda53decfull,
0xbecb74aa80910b40ull,0x73b0693b1b1b34caull,0xd2b266dc38b668e8ull,0x63ef1103969e175eull,
0x8716d7596a5fde42ull,0x51230ff99c7632ffull,0xc18411c201dc285cull,0x74d5fa082846e3eeull,
0x2eae7bd083dc7d08ull,0x8abcb0ddf4589802ull,0xc249ea4849aa819cull,0x3caa1967ac5ed1c0ull,
0xb0afbec2278a9ea2ull,0xa2eaed67a3b94eabull,0xaac435767f622512ull,0x0f37937114632c9eull,
0x58f63f51e02e5c05ull,0xfaa3c92d7e4cc963ull,0xa5822c8a8e377119ull,0x268d35669f609218ull,
0x59e4dd711874f83aull,0x47f1f46bf0c2c0f4ull,0x6fe44856e389b37cull,0x7b26819e1a839ad0ull,
0xbced4479019b70caull,0x0582adba9e3375d0ull,0xcd4f37302db7a586ull,0xa19939560e14275eull,
0xc50c89770d6ec325ull,0xca459f4d0c53a98eull,0xd716600efc3329f2ull,0xfc55f0f4d60109c1ull,
0xf3b62c00c097fbc6ull,0x1bf7d8073ece4e05ull,0xfe6619484f62fc81ull,0x93e686bb3eeade6bull,
0x9a9939ad13ca5fe7ull,0x2082a397029dc5a7ull,0x455244a08d9e75d7ull,0x84c80c65e09056eaull,
0xfd678c5b44f0e887ull,0xd90b69357c516bafull,0x5d896313d26bd034ull,0xec1c89991f8b44e5ull,
0xa0da45781ebf48c6ull,0x3a384d99342a99a4ull,0xdb51b703c297d3cdull,0xfd21e50d22ffec16ull,
0xc1ce06fdf292b298ull,0x2246625b99a4eb21ull,0x6e8b74b0a6f28554ull,0x98c912fa27abb375ull,
0x143c6a7b844fe90full,0x8354642f1a1d7391ull,0xefa77789753d5290ull,0xd13e536577c8bda8ull,
0x1d9977d324ad06faull,0xb9f53f4be96596a9ull,0x021d993a81d465c4ull,0xe3c9c0950b517b3eull,
0x7528594fe488ae97ull,0x4dd24630ed40cb9bull,0xd75575e53927db30ull,0x7abb68fd062d0c14ull,
0x04765e2ccbbcedc4ull,0x4ac401870e14cb12ull,0x2521106d619d1f09ull,0xf878ab3479651381ull,
0x283343a31f60bb48ull,0x7145236a189c9c14ull,0xe6e4d5646d112a4dull,0x40c6867941ad1bb8ull,
0x6d5b5348f9da3607ull,0xd4f08b759f4b180dull,0x2b2ef21a4fa020e6ull,0xaee4552cafae44a1ull,
0xff296a0884446013ull,0x6cfdd8ee88b77b0full,0xbc72cf2b65878702ull,0x3169b8bb72ba93a0ull,
0xeaa9952d0623a748ull,0x61b2974e24b84ab1ull,0xc7cf258bce491ba7ull,0x88865f39850bdac7ull,
0x400841e98c5cefb1ull,0x47c8c62da2278b28ull,0xde343e2741b9c871ull,0xbc26b84c87ebec07ull,
0xbf216681f1bed683ull,0xacb641da82125d06ull,0xd429b734097c5456ull,0x7c0eee02fab8b2b9ull,
0xf61003670d459214ull,0xcdc31e03130de667ull,0x3b179e0a54b77ea3ull,0x83b76083a0e2052dull,
0x18d2a79b93f7d4eeull,0x135a008eb5137eabull,0x1057c0d3f316b3b3ull,0xa0d0035b599913d6ull,
0xb6b41375cbf701c2ull,0xdeb6e2ce3d02e928ull,0x273842789c5a7ae6ull,0x3892f5cb4d7605dcull,
0x06a6f14e8004b3ebull,0xc92ea0ad7cb17bdaull,0xd479a2cda315118cull,0x590d2642798a6f51ull,
0x8c00ae6c2a097ee0ull,0x4fd6e81804acd6ceull,0x22a4c4de36698ea3ull,0x672c78b762fdb655ull,
0xc33298e435454c1aull,0xed5b5cd6ace4c7fcull,0xb21247e53d743e44ull,0x2eed650273f49ba0ull,
0xf81f05b6353fd9deull,0xcd68bcd658819889ull,0x95e49836e7fcbebcull,0xcacf87249d98b484ull,
0x4fc39a977bbef252ull,0xf9ca8b1939bd53f7ull,0x646900fb60396f0aull,0x6350d2081ee61ab5ull,
0x1f9c07b4f3096fe6ull,0x2223a642722dd679ull,0x3a80abcf22de36c6ull,0x5d2892162040a115ull,
0x97378beb701d1e2full,0x911231e2f82b6612ull,0xbf46ea6acd739aeeull,0x4bf80491f161fd25ull,
0xf0b4f8c8c8793f68ull,0x0419f4b5b18b6624ull,0xdb0fe60802dc9608ull,0x15600cc61b08a263ull,
0x1925dc56945ba212ull,0x63540e205ddbf187ull,0x8a0176c6d3cb0ae7ull,0xadb23a6cd7d1f4e0ull,
0xb1f1748c2f388cadull,0xdc5f78f83ffb390cull,0xce18a61494961a13ull,0xeedaa8f706d5d37cull,
0x9b99d6e063703dd4ull,0x2a848ae7d3acdccdull,0x5c394888a8e75efbull,0xbb4f2d57d9276f4aull,
0x9d4dda0403e9dc0aull,0xab512d8125e927a3ull,0x14dc24343a00d033ull,0x741a53daad5a1be5ull,
0xbfff29ffbda3ea0eull,0x7b62137d832e86a8ull,0x6d01c5ecee4ac339ull,0xc6afb4f98ac0bea3ull,
0xce225f063a65cb7dull,0x1af87533ccea9147ull,0xd87d5182ca2d2a24ull,0x4e67e8369c9280e2ull,
0x50b2b42beef77046ull,0xe913c264fb02375bull,0xa0c2232ba200c03dull,0xc74e263b5c6532a1ull,
0xd2bc56983dc878feull,0x4edc244f3532ca70ull,0x7b6354e33d6b0edcull,0x0e86b9fa12f837bcull,
0x3e45c9732c480d88ull,0xdc605042cd825fbbull,0xa1cfcc2894367d80ull,0xed74c029becc582full
};
mario::ChibiColors outfit(unsigned form){mario::ChibiColors c;if(form==1)c.cap=c.shirt={40,162,73};if(form>=2){c.cap=c.shirt={246,240,225};c.overalls=form==3?std::array<unsigned char,3>{40,162,73}:std::array<unsigned char,3>{214,48,47};}c.luigi=form==1||form==3;return c;}
void writeSheet(const std::filesystem::path& path,const std::vector<std::vector<unsigned char>>& frames){
    const int columns=7,width=columns*128,height=int((frames.size()+columns-1)/columns)*256;
    std::vector<unsigned char> rgb(std::size_t(width)*height*3,42);
    for(std::size_t frame=0;frame<frames.size();++frame)for(int y=0;y<128;++y)for(int x=0;x<64;++x){
        const auto at=(y*64+x)*4;const unsigned a=frames[frame][at+3];
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const auto dest=(std::size_t((frame/columns)*256+y*2+dy)*width+(frame%columns)*128+x*2+dx)*3;for(unsigned c=0;c<3;++c)rgb[dest+c]=static_cast<unsigned char>((frames[frame][at+c]*a+42*(255-a)+127)/255);}
    }
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<width<<' '<<height<<"\n255\n";out.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());if(!out)throw std::runtime_error("Cannot write contact sheet");
}
void frames(const std::filesystem::path& output){
    const std::array<unsigned,3> sheets{mario::WalkSheet,mario::RunSheet,mario::JumpSheet};
    const std::array<unsigned,3> counts{14,21,26};const char* names[]{"walk","run","jump"};
    unsigned originalIndex=0,totalCloth=0,leftRed=0,totalBlue=0,leftBlue=0;
    unsigned protectedSkin=0,protectedGloves=0,protectedEyes=0,protectedShoes=0;
    unsigned changedSkin=0,changedGloves=0,changedEyes=0,changedShoes=0;
    for(unsigned variant=0;variant<4;++variant)for(unsigned action=0;action<3;++action){
        std::array<std::vector<std::vector<unsigned char>>,4> previews;
        bool original=true,alpha=true,white=true,pants=true,anatomy=true;
        for(unsigned frame=0;frame<counts[action];++frame){
            std::vector<unsigned char> source;mario::renderSheetFrame(variant,{sheets[action],frame},outfit(0),source);
            original=original&&hash(source)==OriginalHashes[originalIndex++];
            int top=128,bottom=0;for(int y=0;y<128;++y)for(int x=0;x<64;++x)if(source[(y*64+x)*4+3]>=150){top=std::min(top,y);bottom=std::max(bottom,y);}
            for(unsigned form=0;form<4;++form){
                std::vector<unsigned char> rendered;mario::renderSheetFrame(variant,{sheets[action],frame},outfit(form),rendered);
                if(variant<2)previews[form].push_back(rendered);
                unsigned cloth=0,unconverted=0,blue=0,unconvertedBlue=0;
                for(int y=0;y<128;++y)for(int x=0;x<64;++x){
                    const std::size_t at=(y*64+x)*4;alpha=alpha&&source[at+3]==rendered[at+3];if(source[at+3]<200)continue;
                    const int r=source[at],g=source[at+1],b=source[at+2],rr=rendered[at],gg=rendered[at+1],bb=rendered[at+2];
                    const bool equal=!std::memcmp(source.data()+at,rendered.data()+at,4);
                    const bool head=y>=top+(bottom-top)*20/100&&y<top+(bottom-top)*42/100;
                    const bool skin=head&&r>20&&g>12&&r>g+7&&g>b+4&&r<g*2&&(g-b)*4>(r-b);
                    const bool gloves=std::min({r,g,b})>100&&std::max({r,g,b})-std::min({r,g,b})<16;
                    // Blue eye probes end above the collar. In jump12/14 the
                    // overall straps occupy the former 42-percent head band.
                    const bool eyes=head&&y<top+(bottom-top)*38/100&&b>g+15&&b>r+20;
                    const bool shoes=y>=bottom-7&&r>20&&r>g+6&&r>b+8&&g>b;
                    if(form){protectedSkin+=skin;protectedGloves+=gloves;protectedEyes+=eyes;protectedShoes+=shoes;changedSkin+=skin&&!equal;changedGloves+=gloves&&!equal;changedEyes+=eyes&&!equal;changedShoes+=shoes&&!equal;anatomy=anatomy&&(!(skin||gloves||eyes||shoes)||equal);}
                    // Independent reference-color probes: strongly red cloth
                    // and lower-body blue denim, without reading material labels.
                    if(form>=2&&r>=65&&r>g*2.2&&r>b*2.2&&std::abs(g-b)<r*.13&&y<bottom-8){++cloth;if(rr>gg+24||rr>bb+30)++unconverted;}
                    auto denimNeighbor=[&](int xx,int yy){if(xx<0||xx>=64||yy<0||yy>=128)return false;const auto n=(yy*64+xx)*4;return source[n+3]>=200&&source[n+2]>source[n]*1.4&&source[n+2]>source[n+1]*1.2;};
                    // Use cloth interiors: antialiased glove boundaries and
                    // gray stitching can borrow blue from adjacent denim and
                    // must not be painted as if they were solid cloth.
                    const bool denimInterior=denimNeighbor(x-1,y)&&denimNeighbor(x+1,y)&&denimNeighbor(x,y-1)&&denimNeighbor(x,y+1);
                    if(form>=2&&denimInterior&&y>=top+(bottom-top)*44/100&&b>r*1.7&&b>g*1.3&&b>40){++blue;const bool target=form==2?rr>bb+8&&rr>gg+8:gg>rr+8&&gg>bb+8;if(!target)++unconvertedBlue;}
                }
                if(form>=2){totalCloth+=cloth;leftRed+=unconverted;totalBlue+=blue;leftBlue+=unconvertedBlue;white=white&&unconverted<=3&&cloth>5;pants=pants&&unconvertedBlue<=3&&blue>10;}
            }
        }
        const auto label=std::string(names[action])+" variant"+std::to_string(variant);
        check(label+" retains every delivered source RGBA frame",original);
        check(label+" keeps silhouette alpha in every outfit",alpha);
        check(label+" has complete white cap and sleeves across all frames",white);
        check(label+" recolors all blue denim to red or green",pants);
        check(label+" protects anatomy gloves eyes and boots",anatomy);
        if(variant<2)for(unsigned form=0;form<4;++form)writeSheet(output/(std::string(names[action])+"-"+(variant?"small":"big")+"-form"+std::to_string(form)+".ppm"),previews[form]);
    }
    check("protected anatomy samples cover all supplied actions",protectedSkin>1000&&protectedGloves>1000&&protectedEyes>30&&protectedShoes>1000);
    std::ofstream report(output/"pixel-metrics.json");report<<"{\n  \"reference_frames\":244,\n  \"rendered_outfits\":976,\n  \"red_cloth_samples\":"<<totalCloth<<",\n  \"remaining_red_samples\":"<<leftRed<<",\n  \"blue_denim_samples\":"<<totalBlue<<",\n  \"remaining_blue_samples\":"<<leftBlue<<",\n  \"protected_skin_samples\":"<<protectedSkin<<",\n  \"changed_skin_samples\":"<<changedSkin<<",\n  \"protected_glove_samples\":"<<protectedGloves<<",\n  \"changed_glove_samples\":"<<changedGloves<<",\n  \"protected_eye_samples\":"<<protectedEyes<<",\n  \"changed_eye_samples\":"<<changedEyes<<",\n  \"protected_shoe_samples\":"<<protectedShoes<<",\n  \"changed_shoe_samples\":"<<changedShoes<<"\n}\n";
    bool ranges=true;for(auto bad:{mario::SheetFrame{99,0},mario::SheetFrame{mario::WalkSheet,14},mario::SheetFrame{mario::RunSheet,21},mario::SheetFrame{mario::JumpSheet,26}}){std::vector<unsigned char> p;bool threw=false;try{mario::renderSheetFrame(0,bad,outfit(0),p);}catch(const std::out_of_range&){threw=true;}ranges=ranges&&threw;}std::vector<unsigned char> p;bool threw=false;try{mario::renderSheetFrame(4,{mario::WalkSheet,0},outfit(0),p);}catch(const std::out_of_range&){threw=true;}check("invalid direct frame requests reject safely",ranges&&threw);
}
std::array<SMB_tile,8> pose(unsigned group,bool left){std::array<SMB_tile,8> a{};for(unsigned i=0;i<8;++i){auto& t=a[i];t.tileidx=left?(i^1u):i;t.x=30+int(i%2)*8;t.y=40+int(i/2)*8;t.paletteidx=int(group);t.flip_horz=left;t.extra_type=TILE_TYPE_SPRITE;t.extra_spriteidx=static_cast<unsigned char>(1+i);}return a;}
void paletteGroups(){
    std::array<unsigned char,32> pal{};pal[16]=0x22;
    constexpr unsigned char colors[4][3]={{0x37,0x27,0x16},{0x30,0x27,0x19},{0x16,0x27,0x18},{0x30,0x16,0x27}};
    for(unsigned group=0;group<4;++group)for(unsigned c=0;c<3;++c)pal[(group+4)*4+c+1]=colors[group][c];
    mario::CharacterRenderer renderer;renderer.setPatternTables(MarioGameData+0x8010);renderer.setPalette(pal.data());
    for(bool luigi:{false,true})for(bool left:{false,true}){
        const mario::PlayerAnimation stable{4,4,0,0,0,false,0,12,0,false};
        auto star=stable;star.starInvincible=true;
        std::vector<unsigned char> first;std::set<std::uint64_t> starColors;bool stableColors=true,cache=true;
        for(unsigned repeat=0;repeat<3;++repeat)for(unsigned group=4;group<8;++group){auto commands=pose(group,left);
            renderer.beginFrame();renderer.preparePlayer(commands,luigi,true,false,true,&stable);for(const auto& t:commands)renderer.drawTile(t,true,luigi,true);if(first.empty())first=renderer.pixels();else stableColors=stableColors&&first==renderer.pixels();
            renderer.beginFrame();renderer.preparePlayer(commands,luigi,true,false,true,&star);for(const auto& t:commands)renderer.drawTile(t,true,luigi,true);starColors.insert(hash(renderer.pixels()));
            renderer.beginFrame();renderer.preparePlayer(commands,luigi,true,false,true,&stable);for(const auto& t:commands)renderer.drawTile(t,true,luigi,true);cache=cache&&first==renderer.pixels();
        }
        const auto label=std::string(luigi?"Luigi":"Mario")+(left?" left":" right");check(label+" flower palette groups 4 through 7 have one complete outfit",stableColors);check(label+" true star still cycles distinct native outfit colors",starColors.size()>=3);check(label+" star and flower atlas caches cannot mix",cache);
    }
}
}
int main(int argc,char** argv){try{const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("fire-form-verification");std::filesystem::create_directories(output);frames(output);paletteGroups();std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
