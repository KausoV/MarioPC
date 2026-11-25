#pragma once
#include "smbcore/mario.h"
#include "chibi_art.h"
#include "world_background.h"
#include "camel_enemy_art.h"
#include <array>
#include <cstddef>
#include <vector>
#include <map>
#include <cstdint>

namespace mario {
// Read-only native animation snapshot copied with the pre-tick OAM.
// It is part of existing game RAM, so motion requires no new saved state.
struct PlayerAnimation {
    unsigned duration = 0, timer = 0, control = 0, playerState = 0;
    int verticalSpeed = 0;
    bool changingSize = false;
    unsigned throwTimer = 0;
    int horizontalSpeed = 0;
    unsigned verticalFraction = 0;
    bool starInvincible = false;
};
// A presentation-only raster. Optional scenery replaces non-colliding decorations;
// player atlas slices follow the original OAM cells and compositing order.
class CharacterRenderer {
public:
    static constexpr int Scale = 4;
    static constexpr int Width = 256 * Scale;
    static constexpr int Height = 240 * Scale;
    static constexpr std::size_t FrameBytes = std::size_t(Width) * Height * 3;
    CharacterRenderer();
    // Wider displays reveal more native pixels at the same fixed 4x scale.
    void resizeViewport(int nativeWidth);
    int nativeWidth() const { return nativeWidth_; }
    int renderWidth() const { return nativeWidth_ * Scale; }
    std::size_t frameBytes() const { return std::size_t(renderWidth()) * Height * 3; }
    void setPatternTables(const unsigned char* chr);
    void setPalette(const unsigned char* indices);
    void prepareScene(const WorldScene& scene);
    void setSceneNametables(const unsigned char* ppu, int scrollPixels);
    // A complete, verified native 16x16 world cell in TL,BL,TR,BR order.
    // column is an absolute metatile column; row 0 starts at native y=32.
    void setWorldCell(unsigned column, unsigned row, const unsigned char tiles[4], unsigned palette);
    // Main frontend defers backdrop until native palette/terrain are latched.
    void beginFrame(bool deferBackdrop = false);
    void setGroundSupport(const std::array<unsigned char,512>& support);
    void refreshBackground();
    // Validate complete native jumping-coin owners and OAM pairs read-only.
    void prepareCoins(const unsigned char* ram);
    // Native powerup owner and OAM quartet are validated without changing RAM.
    void preparePowerups(const unsigned char* ram);
    // Only the complete native Goomba owner receives the supplied creature.
    void prepareCamelEnemy(const unsigned char* ram);
    void preparePlayer(const std::array<SMB_tile, 8>& sprites, bool luigi = false,
                       bool fire = false, bool swimming = false, bool moving = false,
                       const PlayerAnimation* animation = nullptr);
    int playerPose() const { return playerPose_; }
    bool playerMirrored() const { return playerMirrored_; }
    void drawTile(const SMB_tile& tile, bool is_player = false, bool luigi = false, bool fire = false);
    void seedFromNative256(const unsigned char* rgb);
    const std::vector<unsigned char>& pixels() const { return pixels_; }
    // Save/load presentation frames without affecting native gameplay.
    bool restorePixels(const unsigned char* rgb, std::size_t size);
    // Keep the existing overlap exact when only the viewport dimensions change.
    void restoreViewportPrefix(const unsigned char* rgb, int sourceWidth) noexcept;
    bool restoreScaledPixels(const unsigned char* rgb, int width, int height) noexcept;
private:
    std::array<unsigned char, 512 * 8 * 8> tiles_{};
    std::array<unsigned char, 32> palette_{};
    int nativeWidth_ = 256;
    std::vector<unsigned char> pixels_, backdropPixels_;
    WorldBackground worldBackground_;
    WorldScene scene_{0,0,1,false,false};
    std::array<std::uint16_t, 2 * 30 * 32> decorationSignatures_{};
    std::array<std::uint16_t, 2 * 30 * 32> coinSignatures_{};
    int visualScroll_ = 0;
    struct WorldColumnProof {
        unsigned column = ~0u;
        std::array<std::uint16_t, 13 * 4> signatures{};
        std::array<std::uint16_t, 13 * 4> coinSignatures{};
    };
    // A 512-pixel view intersects at most 33 metatile columns. Owners prevent
    // modulo aliases from treating a different world cell as a decoration.
    std::array<WorldColumnProof, 34> worldDecorationProofs_{};
    bool decorativeTile(const SMB_tile& tile) const;
    struct CoinSpriteProof {
        int tile = -1, y = -1;
        unsigned x = 0, part = 0;
        bool flipVertical = false;
    };
    std::array<CoinSpriteProof, 64> coinSprites_{};
    int coinTilePart(const SMB_tile& tile, bool& sprite) const;
    bool drawCoinTile(const SMB_tile& tile);
    struct MushroomSpriteProof {
        int tile = -1, y = -1;
        unsigned x = 0, part = 0, palette = 0;
        bool life = false;
    };
    std::array<MushroomSpriteProof, 64> mushroomSprites_{};
    bool drawMushroomTile(const SMB_tile& tile);
    struct FlowerSpriteProof {
        int tile = -1, y = -1;
        unsigned x = 0, part = 0, palette = 0;
        bool flipHorizontal = false;
    };
    std::array<FlowerSpriteProof, 64> flowerSprites_{};
    void prepareFlowerPowerup(const unsigned char* ram);
    bool drawFlowerTile(const SMB_tile& tile);
    CamelEnemyPresentation camelEnemy_;
    bool drawCamelEnemyTile(const SMB_tile& tile);
    std::array<SMB_tile, 8> playerSprites_{};
    int playerPose_ = -1, playerLegPose_ = -1;
    bool playerMirrored_ = false, playerLuigi_ = false, playerFire_ = false;
    ChibiMotion motion_{};
    struct AtlasEntry { std::vector<unsigned char> rgba; std::uint64_t lastUse = 0; };
    std::map<std::uint64_t, AtlasEntry> atlasCache_;
    // With no native animation snapshot, retain historic palette-only samples.
    bool playerStar_ = true;
    std::uint64_t atlasClock_ = 0;
    void prepareMotion(const PlayerAnimation* animation, bool moving, bool swimming);
    const std::vector<unsigned char>& playerAtlas(int paletteGroup);
};
} // namespace mario