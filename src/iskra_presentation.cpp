#include "iskra_presentation.h"

namespace iskra {
void Presentation::prepare(const unsigned char* ram, uint64_t clock) {
    scene.world = 1 + ram[0x75f];
    scene.level = 1 + ram[0x75c];
    scene.area = ram[0x74e] & 3;
    scene.frame = clock;
    scene.night = ram[0x744] == 4 || ram[0x744] == 6 || ram[0x744] == 7;
    const int mode = ram[0x770], task = ram[0x772];
    const bool demo = mode == 0 && task == 3 && ram[0x7a2] == 0;
    playing = demo || (mode == 1 && task == 3) || mode == 2;
    actors.prepare(ram, clock);
}
void Presentation::backdrop(SMB_state* state) {
    scene.camera = SMB_viewport_camera_x(state);
    float lampX = 0, lampY = 0;
    scene.lantern.visible = false;
    if (playing && actors.lanternPosition(scene.camera,lampX,lampY))
        scene.lantern.place(lampX,lampY,scene.area,scene.frame);
    background(image, renderWidth(), Height, scene);
    spriteCount = behindCount = 0;
    hasTerrain = false;
    ground.reset(scene.camera);
}
void Presentation::tile(const SMB_tile& tile) {
    if (!playing) return;
    if (tile.extra_type != TILE_TYPE_SPRITE) {
        hasTerrain = true;
        return;
    }
    if (spriteCount == sprites.size()) return;
    sprites[spriteCount++] = tile;
    if (!hasTerrain) behindCount = spriteCount;
    const int top = actors.platformSurfaceY(tile);
    if (top >= 0 && tile.x + 8 > 0 && tile.x < width && tile.y < 240)
        ground.recordPlatform(tile.extra_spriteidx,tile.x,tile.y,top);
}

void Presentation::finishFrame(SMB_state* state) {
    // Tile delivery preserves hardware priority. Inspect all deck positions
    // before projecting shadows, then composite behind-sprites, terrain and
    // front-sprites in their original order.
    for (unsigned i=0; i<behindCount; ++i)
        actors.draw(image,renderWidth(),sprites[i],scene);
    std::vector<HollowMouth> mouths;
    const bool crossing=std::any_of(sprites.begin(),sprites.begin()+behindCount,[&](const SMB_tile& tile){
        return actors.usesHollow(tile,true)||actors.usesHollow(tile,false);
    });
    if (hasTerrain) drawTerrain(state,crossing?&mouths:nullptr);
    // Only pipe travellers may reappear inside the painted hollow. All other
    // terrain, including its front rim, keeps the native sprite priority.
    for (const auto& mouth:mouths) {
        const bool side=mouth.axis==HollowMouth::Axis::Side;
        for(unsigned i=0;i<behindCount;++i)
            if(actors.usesHollow(sprites[i],side))
                actors.draw(image,renderWidth(),sprites[i],scene,nullptr,&mouth);
    }
    for (unsigned i=behindCount; i<spriteCount; ++i)
        actors.draw(image,renderWidth(),sprites[i],scene,ground.maskFor(sprites[i].extra_spriteidx));
}
void Presentation::drawTerrain(SMB_state* state,std::vector<HollowMouth>* mouths) {
    const auto* ppu = SMB_ppuram(state);
    const auto* ram = SMB_ram(state);
    const unsigned first = unsigned(std::max(0,scene.camera)) / 16;
    const unsigned last = unsigned(std::max(0,scene.camera) + width - 1) / 16;
    TerrainWindow window{};
    window.first=std::max(0,int(first)-3);
    window.count=int(last)+4-window.first;
    for (int local=0; local<window.count; ++local) {
        const unsigned column=unsigned(window.first+local);
        const int x=int(column*16)-scene.camera;
        for (unsigned row=0; row<13; ++row) {
            unsigned char quartet[4]{}, palette=0, collision=0;
            const bool known=SMB_viewport_cell(state,column,row,quartet,&palette,&collision);
            if (column>=first && x < 256) {
                // Use the current nametables for broken blocks and collected items.
                const unsigned tx=(column%16)*2, ty=4+row*2;
                const unsigned base=0x2000+((column/16)&1)*0x400;
                for(unsigned t=0;t<4;++t) quartet[t]=ppu[base+(ty+t%2)*32+tx+t/2];
                const auto attr=ppu[base+0x3c0+(ty/4)*8+tx/4];
                palette=(attr >> ((column&1)*2+((ty/2)&1)*4))&3;
            } else if (!known || SMB_viewport_block_bumped(state,column,row)) continue;
            const unsigned material=visualMetatile(ram,quartet,palette,collision);
            window.cells[local][row]=static_cast<unsigned char>(material);
        }
    }
    drawShelfSupports(image,renderWidth(),Height,scene,window);
    const auto landmarks = drawFinishLandmarks(image,renderWidth(),Height,scene,window);
    // Both neighbours must be known to leave a broken bridge with an exposed end.
    for(unsigned column=first;column<=last;++column){
        const int x=int(column*16)-scene.camera;
        unsigned above=0;
        for(int row=0;row<TerrainWindow::Rows;++row){
            const unsigned material=window.get(int(column),row);
            ground.record(column-first,row,material);
            if(material && !(landmarks[column-window.first] & (1u<<row))){
                const unsigned openEnds=unsigned(window.get(int(column)-1,row)!=material)
                                      | (unsigned(window.get(int(column)+1,row)!=material)<<1);
                const bool landmarkAbove=row>0 && (landmarks[column-window.first] & (1u<<(row-1)));
                terrain(image,renderWidth(),Height,scene,x,32+row*16,material,above,openEnds,landmarkAbove);
            }
            above=material;
        }
    }
    drawElderwood(image,renderWidth(),Height,scene,window,mouths);
    std::array<ShadowCaster,7> casters{};
    const int count = actors.shadowCasters(scene.camera, casters);
    for (int i = 0; i < count; ++i) {
        const auto& actor = casters[i];
        float direction = -.55f;
        if (scene.lantern.visible) {
            const float fromLamp = actor.x - scene.lantern.x / float(Scale);
            const float dy = actor.feetY - scene.lantern.y / float(Scale);
            if (std::abs(fromLamp) > 8 && fromLamp * fromLamp + dy * dy < 36 * 36)
                direction = std::clamp(fromLamp / 15.f, -1.f, 1.f);
        }
        ground.project(image, renderWidth(), actor.x, actor.feetY, actor.width,
                       actor.airborne, scene.area == 0, direction);
    }
}
}