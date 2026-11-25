#include "game_hud.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace mario {
namespace {
constexpr ImU32 Ink = IM_COL32(247, 239, 248, 255);
constexpr ImU32 Lilac = IM_COL32(202, 173, 241, 255);
constexpr ImU32 Amber = IM_COL32(255, 215, 138, 255);
constexpr ImU32 Coral = IM_COL32(249, 108, 106, 255);
constexpr ImU32 Night = IM_COL32(18, 20, 39, 255);
struct Chapter { const char* name; std::array<const char*,4> trail; const char* beacon; };
constexpr Chapter chapters[] = {
    {"Шёпот сосен", {"Восемь маяков погасли. Искра поднимает фонарь. На старой сосне проступает первая метка.", "Под корнями сосны светится янтарная жила.", "Искра идёт вдоль жилы. В хвое пахнет дымом.", "За каменным порогом ещё тлеет первый маяк."}, "Первый огонь выхватил из тьмы тропу к садам."},
    {"Забытые сады", {"За соснами лежит заброшенный сад. На ветвях звенит стекло.", "Стеклянное семя скатилось к самой воде.", "На тёмной коре остался золотой след.", "Искра подносит семя к погасшей чаше."}, "В кронах снова зажглись золотые плоды."},
    {"Зеркальные топи", {"Луна дрожит в чёрной воде. Её отражение плывёт против течения.", "Под ряской мерцает ступень. За ней — ещё одна.", "Камыш качнулся. Впереди показался сухой остров.", "Отражение остановилось прямо под чашей маяка."}, "На воде появилась светлая тропа к северу."},
    {"Серебряный бор", {"В серебряном бору под корой стучит крошечная искра.", "Тонкая ветвь звенит в такт шагам.", "Из разлома в старой сосне струится тёплый свет.", "Серебряная хвоя указывает на каменный свод."}, "Лес ответил тихим перезвоном хвои."},
    {"Янтарный перевал", {"Ветер крепчает. Искра прикрывает фонарь ладонью.", "В расселине уцелел кусочек тёплого янтаря.", "До вершины ещё один уступ. Огонёк держится.", "На седловине ждёт чаша, полная снега."}, "С высоты теперь видны огни долины."},
    {"Лиловый сумрак", {"Лиловые споры отступают от тёплого стекла фонаря.", "В тумане одна за другой проступают зарубки.", "Последняя зарубка ведёт к глухому оврагу.", "Сквозь лиловую пыль виден фитиль шестого маяка."}, "Туман расступился перед пламенем маяка."},
    {"Звёздная чаща", {"В ночной чаще звёзды зацепились за еловые лапы.", "С ветки в фонарь соскользнула первая звезда.", "Между кронами тянется нить холодного света.", "Нить обрывается у седьмого маяка."}, "Семь огней указали путь к сердцу леса."},
    {"Перед рассветом", {"Семь огней уже горят. Внизу, под корнями, темно.", "По корням бежит тепло от далёких маяков.", "Искра слышит под землёй медленный стук.", "Остался последний шаг. В корнях ждёт восьмая чаша."}, "Восемь маяков горят. Лес встречает рассвет."}
};
struct Canvas {
    ImDrawList* draw;
    ImVec2 pos;
    float width, sx, sy;
    ImVec2 at(float x, float y) const { return {pos.x + x * sx, pos.y + y * sy}; }
    float unit() const { return std::min(sx, sy); }
    void box(float x, float y, float w, float h, ImU32 color, float radius = 0) const {
        draw->AddRectFilled(at(x,y), at(x+w,y+h), color, radius*unit());
    }
    void label(float x, float y, float size, const std::string& value, ImU32 color, bool center = false) const {
        auto* font = ImGui::GetFont();
        size *= unit();
        auto p = at(x,y);
        if (center) p.x -= font->CalcTextSizeA(size, std::numeric_limits<float>::max(), 0, value.c_str()).x/2;
        draw->AddText(font, size, p, color, value.c_str());
    }
    void spark(float x,float y,float radius,ImU32 color) const {
        const auto left=at(x-radius*.73f,y), right=at(x+radius*.73f,y);
        draw->AddTriangleFilled(left, at(x,y-radius), right, color);
        draw->AddTriangleFilled(left, right, at(x,y+radius), color);
    }
};
std::string digits(int n, int width) {
    char buffer[16];
    std::snprintf(buffer,sizeof(buffer),"%0*d",width,std::max(0,n));
    return buffer;
}
std::string score(std::string s) {
    if (s.size()>6) s.erase(0,s.size()-6);
    for (auto& c : s) if (c<'0'||c>'9') c='0';
    return std::string(6-s.size(),'0')+s;
}
void title(const Canvas& c, const PresentationHud& d, const Chapter& ch, bool illustrated) {
    if (!illustrated) c.box(0,0,c.width,240,Night);
    const ImU32 shade=IM_COL32(16,19,38,242), clear=IM_COL32(16,19,38,0);
    c.draw->AddRectFilledMultiColor(c.at(0,0),c.at(c.width*.68f,240),shade,clear,clear,shade);
    const float left=c.width*.07f;
    c.spark(left+1.8f,46,2.2f,Lilac);
    c.label(left+6,42.8f,6.4f,"ЛЕСНОЕ ПРИКЛЮЧЕНИЕ",Lilac);
    // The original 29 × 7 wordmark from the reference's inline SVG.
    constexpr const char* logo[]={
        "#...#..####.#..##.####...###.",
        "#...#.##....#.##..#...#.##.##",
        "#..##.#.....###...#...#.#...#",
        "#.#.#.#.....##....####..#####",
        "##..#.#.....###...#.....#...#",
        "#...#.##....#.##..#.....#...#",
        "#...#..####.#..##.#.....#...#"
    };
    const float pixel=std::clamp(c.width*.012f,3.15f,5.0f);
    for (int layer=0;layer<2;++layer)
        for (int row=0;row<7;++row)
            for (int col=0;col<29;++col) if (logo[row][col]=='#')
                c.box(left+col*pixel,55+row*pixel+(layer?0:1.4f),pixel+.035f,pixel+.035f,layer?IM_COL32(249,242,232,255):IM_COL32(64,55,82,255));
    const float sub=60+7*pixel;
    c.label(left,sub,12.5f,"Хранитель леса",Ink);
    c.label(left,sub+18,6.7f,"Погасли восемь маяков.",Amber);
    c.label(left,sub+27,6.7f,"Помоги Искре вернуть лесу свет.",Ink);
    if (d.worldSelectAvailable) {
        c.label(left,205,7.0f,"Маяк "+std::to_string(d.world)+" · "+ch.name,Ink);
        c.label(left,215,5.9f,"Сменить маяк — кнопка бега",Lilac);
    }
}
void trailOpening(const Canvas& c,const PresentationHud& d,const Chapter& ch,bool illustrated) {
    if(!illustrated)c.box(0,0,c.width,240,Night);
    const auto shade=IM_COL32(11,16,29,208), middle=IM_COL32(15,17,30,145), clear=IM_COL32(15,17,30,0);
    c.draw->AddRectFilledMultiColor(c.at(0,0),c.at(c.width*.32f,240),shade,middle,middle,shade);
    c.draw->AddRectFilledMultiColor(c.at(c.width*.32f,0),c.at(c.width*.76f,240),middle,clear,clear,middle);
    const float left=c.width*.065f;
    const float column=c.width*.465f;
    const float wrap=column*c.sx;
    auto* font=ImGui::GetFont();
    const float headingSize=std::clamp(c.width*.052f,14.5f,20.f)*c.unit();
    const float bodySize=8.25f*c.unit();
    const char* story=trailStory(d);
    const float headingH=font->CalcTextSizeA(headingSize,std::numeric_limits<float>::max(),wrap,ch.name).y/c.sy;
    const float storyH=font->CalcTextSizeA(bodySize,std::numeric_limits<float>::max(),wrap,story).y/c.sy;
    const float top=124-(14+headingH+8+storyH)*.5f;
    c.label(left,top,6.5f,"ТРОПА "+std::to_string(d.world)+"."+std::to_string(d.level),Amber);
    c.draw->AddText(font,headingSize,c.at(left,top+13),Ink,ch.name,nullptr,wrap);
    c.draw->AddText(font,bodySize,c.at(left,top+13+headingH+8),IM_COL32(235,227,222,255),story,nullptr,wrap);
}
void chapterCard(const Canvas& c, const std::string& eyebrow, const char* name, const std::string& line, const char* hint = nullptr) {
    const float w=std::min(c.width-16.0f,239.0f), center=c.width*.5f;
    const float bottom=hint ? 162.0f : 149.0f;
    c.box(center-w*.5f,86,w,bottom-86,IM_COL32(18,19,35,238),5);
    c.draw->AddRect(c.at(center-w*.5f,86),c.at(center+w*.5f,bottom),IM_COL32(160,130,189,130),5*c.unit(),c.unit());
    c.label(center,93,6.4f,eyebrow,Lilac,true);
    c.label(center,105,15.0f,name,Ink,true);
    c.label(center,130,6.9f,line,Amber,true);
    if (hint) c.label(center,148,6.3f,hint,Lilac,true);
}
void hud(const Canvas& c,const PresentationHud& d,const Chapter& ch) {
    const bool wide=c.width>=340;
    const float left=5, right=c.width-5;
    const SDL_FRect pause = menuButtonRect(d);
    c.box(left,4,pause.x+pause.w+2-left,19,IM_COL32(17,18,33,222),4);
    c.box(right-(wide?81:54),4,wide?81:54,19,IM_COL32(17,18,33,222),4);
    // Life heart and the amber crystal of the reference, painted rather than font-dependent.
    const float x=left+7, y=13;
    c.draw->AddCircleFilled(c.at(x-1.35f,y-1.4f),2.15f*c.unit(),Coral,12);
    c.draw->AddCircleFilled(c.at(x+1.35f,y-1.4f),2.15f*c.unit(),Coral,12);
    c.draw->AddTriangleFilled(c.at(x-3.3f,y-.6f),c.at(x+3.3f,y-.6f),c.at(x,y+3.8f),Coral);
    c.label(x+5,8.8f,9.0f,std::to_string(std::clamp(d.lives,0,255)),Ink);
    c.box(left+28,8,0.4f,11,IM_COL32(138,117,164,160));
    c.spark(left+35,y,4.4f,Amber);
    c.label(left+41,8.8f,9.0f,digits(std::clamp(d.coins,0,99),2),Ink);
    const float cx=c.width*.5f;
    std::string section="ТРОПА "+std::to_string(d.world)+"."+std::to_string(d.level);
    c.label(cx+.35f,4.6f,10.5f,ch.name,Night,true);
    c.label(cx,4.0f,10.5f,ch.name,Ink,true);
    c.label(cx+.3f,16.0f,5.9f,section,Night,true);
    c.label(cx,15.5f,5.9f,section,Lilac,true);
    const float tx=right-(wide?76:50);
    c.label(tx,6.8f,5.8f,d.currentPlayer==1?"ЛУЧ":"ИСКРА",Lilac);
    c.label(tx,14.0f,6.7f,score(d.score),Ink);
    const float xTime=right-(wide?27:22);
    c.label(xTime,6.8f,5.7f,"ВРЕМЯ",Lilac);
    c.label(xTime,13.5f,7.4f,d.noTimer?"ВЫКЛ":digits(std::clamp(d.timer,0,999),3),
            !d.noTimer&&d.timer>0&&d.timer<100?Coral:Ink);
}
}
const char* trailStory(const PresentationHud& data) {
    return chapters[std::clamp(data.world,1,8)-1].trail[std::clamp(data.level,1,4)-1];
}
SDL_FRect menuButtonRect(const PresentationHud& data) {
    if (data.titleVisible()) return {float(std::clamp(data.nativeWidth,256,512))-40,5,35,17};
    return {61,5,16,17};
}
void readWaygateSigns(PresentationHud& hud, int warpZone, const std::uint8_t* ppu, int cameraX) {
    hud.waygates = {};
    if (!ppu || hud.mode != 1 || hud.task != 3 || warpZone < 4 || warpZone > 6) return;
    // The three sign cells are written by the native warp-room message.
    // The second nametable begins halfway around the 512-pixel scrolling ring.
    for (int i = 0; i < 3; ++i) {
        const int column = 5 + i * 8;
        const int world = ppu[0x2400 + 17 * 32 + column];
        const int x = (256 + column * 8 + 4 - cameraX) & 511;
        if (world >= 1 && world <= 8 && x < hud.nativeWidth) hud.waygates[i] = {x, world};
    }
}
void drawPresentationHud(const PresentationHud& data,const SDL_FRect& dest,bool illustrated) {
    if (!data.valid||!std::isfinite(dest.x)||!std::isfinite(dest.y)||!std::isfinite(dest.w)||
        !std::isfinite(dest.h)||dest.w<=0||dest.h<=0) return;
    const auto& io=ImGui::GetIO();
    const float fx=io.DisplayFramebufferScale.x>0?io.DisplayFramebufferScale.x:1;
    const float fy=io.DisplayFramebufferScale.y>0?io.DisplayFramebufferScale.y:1;
    const float width=float(std::clamp(data.nativeWidth,256,512));
    Canvas c{ImGui::GetBackgroundDrawList(),{dest.x/fx,dest.y/fy},width,dest.w/(width*fx),dest.h/(240*fy)};
    const auto& ch=chapters[std::clamp(data.world,1,8)-1];
    c.draw->PushClipRect(c.at(0,0),c.at(width,240),true);
    if (data.titleVisible()) title(c,data,ch,illustrated);
    else if(data.chapterVisible())trailOpening(c,data,ch,illustrated);
    else {
        c.draw->PushClipRect(c.at(0,0),c.at(width,32),true);
        hud(c,data,ch);
        c.draw->PopClipRect();
        for (const auto& sign : data.waygates) {
            if (!sign.world) continue;
            c.box(float(sign.x)-6,135,12,12,IM_COL32(34,27,45,240),2);
            c.label(float(sign.x),136,9.0f,std::to_string(sign.world),Amber,true);
        }
        if (data.mode==1&&data.task==1&&data.screenTask==5)
            chapterCard(c,"ВРЕМЯ ИСТЕКЛО","Фонарь погас","Новый огонь ждёт у начала тропы.");
        else if (data.mode==2&&data.task>=3)
            chapterCard(c,"МАЯК "+std::to_string(data.world)+" ИЗ 8",data.world==8?"Рассвет вернулся":"Маяк зажжён",ch.beacon,
                        data.world==8 && data.task==4 ? "Удерживай кнопку бега, чтобы вернуться в меню." : nullptr);
        else if (data.mode==3)
            chapterCard(c,"КОНЕЦ ПУТИ","Последняя искра","Огонёк уснул. Лес запомнит твой путь.");
    }
    c.draw->PopClipRect();
}
} // namespace mario
