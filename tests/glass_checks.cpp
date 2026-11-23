#include <SDL3/SDL.h>
#include "glass_panel.h"
#include "imgui.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
void require(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
double variance(const std::vector<unsigned char>& pixels, int stride) {
    double sum = 0, squared = 0; const size_t count = pixels.size() / stride;
    for (size_t i = 0; i < pixels.size(); i += stride) { sum += pixels[i]; squared += pixels[i] * pixels[i]; }
    return squared / count - std::pow(sum / count, 2);
}
std::vector<Uint32> read(SDL_Renderer* renderer) {
    SDL_Surface* raw = SDL_RenderReadPixels(renderer,nullptr); require(raw != nullptr,"Read actual glass framebuffer");
    SDL_Surface* rgba = SDL_ConvertSurface(raw,SDL_PIXELFORMAT_RGBA32); SDL_DestroySurface(raw);
    require(rgba != nullptr,"Convert actual glass framebuffer");
    std::vector<Uint32> result(static_cast<size_t>(rgba->w)*rgba->h);
    for (int y=0;y<rgba->h;++y) std::memcpy(result.data()+static_cast<size_t>(y)*rgba->w,static_cast<unsigned char*>(rgba->pixels)+y*rgba->pitch,rgba->w*4);
    SDL_DestroySurface(rgba); return result;
}
}
int main(int argc,char** argv) {
    try {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy"); require(SDL_Init(SDL_INIT_VIDEO),"Initialize actual SDL glass renderer");
        const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("glass-checks-output"); std::filesystem::create_directories(output);
        SDL_Window* window=SDL_CreateWindow("Mario liquid glass checks",768,720,SDL_WINDOW_HIDDEN); require(window!=nullptr,"Create glass test window");
        SDL_Renderer* renderer=SDL_CreateRenderer(window,nullptr); require(renderer!=nullptr,"Create glass test renderer");
        {
            mario::GlassBackdrop glass(renderer); const SDL_FRect dest{0,0,768,720};
            constexpr int width=1024,height=960; std::vector<unsigned char> source(static_cast<size_t>(width)*height*3);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)for(int c=0;c<3;++c)
                source[(static_cast<size_t>(y)*width+x)*3+c]=((x/16+y/16)&1)?240:15;
            const auto original=source;
            require(glass.update(source.data(),width,height,dest,1,false),"Build real CPU-blurred SDL glass texture");
            require(glass.ready() && glass.width()==256 && glass.height()==240,"High-resolution source is reduced before blur/upload");
            require(source==original,"Backdrop never mutates original game presentation pixels");
            require(variance(glass.pixels(),4)<variance(source,3)*0.20,"Real separable blur removes high-frequency checker contrast");
            const auto before=glass.updates(); const double buildTime=glass.lastBuildMilliseconds();
            const auto cachedStarted=std::chrono::steady_clock::now();
            bool cacheSuccess=true; for(int i=0;i<10000;++i) cacheSuccess &= glass.update(source.data(),width,height,dest,1,false);
            require(cacheSuccess,"10000 paused frames reuse the cached glass texture");
            const double cachedUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-cachedStarted).count()/10000;
            require(glass.updates()==before && glass.lastBuildMilliseconds()==buildTime,"Cache hits perform no extra blur or texture upload");
            require(glass.update(source.data(),width,height,SDL_FRect{96,0,768,720},1,false) && glass.updates()==before,"Window movement/letterbox changes reuse image pixels");
            require(glass.update(source.data(),width,height,dest,2,true) && glass.updates()==before+1,"New presentation revision refreshes blur and title erasure");
            // A bright impulse must spread into adjacent dark pixels, independently of downsampling.
            std::fill(source.begin(),source.end(),0);
            for(int y=472;y<488;++y)for(int x=504;x<520;++x)for(int c=0;c<3;++c)source[(static_cast<size_t>(y)*width+x)*3+c]=255;
            require(glass.update(source.data(),width,height,dest,3,false),"Build impulse blur frame");
            const auto& impulse=glass.pixels();
            require(impulse[(120*256+128)*4]>0 && impulse[(120*256+128)*4]<255,"Blur reduces the sharp impulse peak");
            require(impulse[(120*256+136)*4]>0,"Blur spreads light into an initially black neighbor");
            // Uniform scene colors survive both blur and old-lettering erasure.
            for(size_t i=0;i<source.size();i+=3){source[i]=70;source[i+1]=110;source[i+2]=160;}
            require(glass.update(source.data(),width,height,dest,4,true),"Build uniform title scene");
            bool uniform=true; for(size_t i=0;i<glass.pixels().size();i+=4)
                uniform &= glass.pixels()[i]==70 && glass.pixels()[i+1]==110 && glass.pixels()[i+2]==160 && glass.pixels()[i+3]==255;
            require(uniform,"Blur and private text erasure preserve actual adjacent scene color");
            // Render real glass over a colorful scene and enforce the exact panel scissor.
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){const size_t at=(static_cast<size_t>(y)*width+x)*3;
                source[at]=static_cast<unsigned char>(30+x*170/width); source[at+1]=static_cast<unsigned char>(45+y*130/height); source[at+2]=static_cast<unsigned char>(150+((x/48+y/48)&1)*65);}
            require(glass.update(source.data(),width,height,dest,5,false),"Build colorful reusable glass material");
            SDL_Texture* scene=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB24,SDL_TEXTUREACCESS_STATIC,width,height); require(scene!=nullptr,"Create actual backdrop scene texture");
            require(SDL_UpdateTexture(scene,nullptr,source.data(),width*3),"Upload actual scene colors");
            ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.DisplaySize=ImVec2(768,720); io.DisplayFramebufferScale=ImVec2(1,1); io.DeltaTime=1.0f/60;
            io.IniFilename=nullptr; io.LogFilename=nullptr; require(ImGui_ImplSDLRenderer3_Init(renderer),"Initialize actual glass ImGui renderer");
            SDL_SetRenderDrawColor(renderer,0,0,0,255); SDL_RenderClear(renderer); SDL_RenderTexture(renderer,scene,nullptr,&dest); const auto baseline=read(renderer);
            ImGui_ImplSDLRenderer3_NewFrame(); ImGui::NewFrame();
            mario::drawGlassPanel(ImGui::GetBackgroundDrawList(),&glass,ImVec2(64,80),ImVec2(704,640),28); ImGui::Render();
            bool sampledTexture=false; const auto* draw=ImGui::GetDrawData();
            for(const auto* list:draw->CmdLists)for(const auto& command:list->CmdBuffer)if(command.ElemCount){
                require(command.ClipRect.x>=64 && command.ClipRect.y>=80 && command.ClipRect.z<=704 && command.ClipRect.w<=640,"Glass rim/refraction/highlights stay inside panel bounds");
                if(command.TexRef._TexData==nullptr && command.GetTexID()!=0) sampledTexture=true;
            }
            require(sampledTexture,"Glass draw commands sample a real SDL blur texture");
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer); const auto actual=read(renderer); int changed=0;
            for(int y=0;y<720;++y)for(int x=0;x<768;++x)if(actual[static_cast<size_t>(y)*768+x]!=baseline[static_cast<size_t>(y)*768+x]){
                if(x<64||x>=704||y<80||y>=640)throw std::runtime_error("Glass changes pixels outside its bounded panel"); ++changed;
            }
            require(changed>10000,"Liquid glass visibly refracts/tints the actual game backdrop");
            SDL_Surface* screenshot=SDL_RenderReadPixels(renderer,nullptr); require(screenshot!=nullptr,"Read liquid glass preview");
            require(SDL_SavePNG(screenshot,(output/"liquid-glass-preview.png").string().c_str()),"Save real liquid glass preview"); SDL_DestroySurface(screenshot);
            ImGui_ImplSDLRenderer3_Shutdown(); ImGui::DestroyContext(); SDL_DestroyTexture(scene);
            double totalMs=0; for(int sample=0;sample<20;++sample){require(glass.update(source.data(),width,height,dest,6+sample,false),"Measure refreshed high-resolution glass frame");totalMs+=glass.lastBuildMilliseconds();}
            std::ofstream report(output/"glass-checks-result.json");
            report << "{\n\"status\":\"passed\",\"assertions\":" << checks << ",\"source_width\":1024,\"source_height\":960,"
                   << "\"blur_width\":256,\"blur_height\":240,\"mean_refresh_ms\":" << totalMs/20 << ",\"cache_hit_us\":" << cachedUs
                   << ",\"source_pixels_unchanged\":true,\"real_sdl_texture_sampled\":true}\n";
        }
        SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
        std::cout << "Mario liquid glass checks passed: " << checks << '\n'; return 0;
    } catch(const std::exception& error){std::cerr << "Mario liquid glass checks FAILED: " << error.what() << '\n';SDL_Quit();return 1;}
}
