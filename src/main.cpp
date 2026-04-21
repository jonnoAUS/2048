/**
 * 2048 game implementation for Geometry Dash.
 *
 * HUGE CREDIT to: https://rosettacode.org/wiki/2048
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <array>
#include <vector>
#include <random>
#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace {
    /* Board constants. */
    constexpr int kBoardSide = 4;
    constexpr int kCellCount = kBoardSide * kBoardSide;

    /* Popup sizing. */
    constexpr float kPopupWidth = 300.f;
    constexpr float kPopupHeight = 290.f;

    /* Board layout constants. */
    constexpr float kBoardSize = 236.f;
    constexpr float kBoardPadding = 6.f;
    constexpr float kBoardGap = 6.f;
    constexpr float kCellSize = (kBoardSize - (kBoardPadding * 2.f) - (kBoardGap * 3.f)) / 4.f;

    /* Animation timing. */
    constexpr float kTileMoveAnimDuration = 0.085f;
    constexpr float kTileMergePopDuration = 0.065f;

    /* Geode setting key. */
    constexpr char const* kTileAnimationSettingKey = "tile-animations";

    /* Move directions. */
    enum class MoveDir {
        Left,
        Right,
        Up,
        Down
    };

    /* Shared board type. */
    using Board = std::array<std::array<int, kBoardSide>, kBoardSide>;

    /**
     * Simple motion desc for one visual tile during a move.
     *
     * `fromIndex` = original flat cell index
     * `toIndex` = dest flat cell index
     * `value` = tile val before merge
     * `merged` = whether tile merged
     */
    struct TileMotion {
        int fromIndex = -1;
        int toIndex = -1;
        int value = 0;
        bool merged = false;
    };

    /**
     * Simulated move result.
     *
     * `board` = board state after move
     * `scoreGain` = score delta from merges
     * `changed` = whether anything moved/merged
     * `mergedIndices` = dest cells that should get merge effect
     * `motions` = raw tile travel data for anim layer
     */
    struct MoveSimulation {
        Board board {};
        int scoreGain = 0;
        bool changed = false;
        std::vector<int> mergedIndices;
        std::vector<TileMotion> motions;
    };

    /* Helper for creating a solid colored layer. */
    CCLayerColor* makeSolid(ccColor4B color, float width, float height) {
        auto layer = CCLayerColor::create(color, width, height);
        layer->ignoreAnchorPointForPosition(false);
        layer->setAnchorPoint({ 0.5f, 0.5f });
        return layer;
    }

    /* Update existing layer's fill color / opac. */
    void applyColor(CCLayerColor* layer, ccColor4B color) {
        layer->setColor(ccc3(color.r, color.g, color.b));
        layer->setOpacity(color.a);
    }

    /**
     * 2048 tile palette.
     * Credit to: https://2048-game.fandom.com/wiki/2048
     */
    ccColor4B slotColor() {
        return ccc4(205, 193, 180, 255);
    }
    ccColor4B boardTrayColor() {
        return ccc4(187, 173, 160, 255);
    }
    ccColor4B cardColor() {
        return ccc4(250, 248, 239, 255);
    }

    ccColor4B tileColorForValue(int value) {
        switch (value) {
            case 2:    return ccc4(238, 228, 218, 255);
            case 4:    return ccc4(237, 224, 200, 255);
            case 8:    return ccc4(242, 177, 121, 255);
            case 16:   return ccc4(245, 149, 99, 255);
            case 32:   return ccc4(246, 124, 95, 255);
            case 64:   return ccc4(246, 94, 59, 255);
            case 128:  return ccc4(237, 207, 114, 255);
            case 256:  return ccc4(237, 204, 97, 255);
            case 512:  return ccc4(237, 200, 80, 255);
            case 1024: return ccc4(237, 197, 63, 255);
            case 2048: return ccc4(237, 194, 46, 255);
            default:   return ccc4(60, 58, 50, 255);
        }
    }

    /**
     * Text color per tile.
     *
     * Low-value light tiles use darker text.
     * Everything else uses usual bright white.
     */
    ccColor3B tileTextColorForValue(int value) {
        if (value == 2 || value == 4) {
            return ccc3(119, 110, 101);
        }
        return ccc3(255, 255, 255);
    }

    /* Label scale based on num digits. */
    float tileLabelScaleForValue(int value) {
        if (value < 10)    return 1.15f;
        if (value < 100)   return 1.00f;
        if (value < 1000)  return 0.82f;
        if (value < 10000) return 0.68f;
        return 0.56f;
    }

    /* Returns `true` if board contains at least one `tile >= target`. */
    bool boardHasValue(Board const& board, int target) {
        for (auto const& row : board) {
            for (int value : row) {
                if (value >= target) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Returns `true` if board has any legal moves.
     *
     * A move is possible if:
     *  - any cell is empty
     *  - any horizontal/vertical neighbor can merge
     */
    bool boardCanMove(Board const& board) {
        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                if (board[row][col] == 0) {
                    return true;
                }

                if (col + 1 < kBoardSide && board[row][col] == board[row][col + 1]) {
                    return true;
                }
                if (row + 1 < kBoardSide && board[row][col] == board[row + 1][col]) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Converts move line pos into a flat board index.
     *
     * Left / Right:
     *      `outer` is row, `inner` is col
     *
     * Up / Down:
     *      `outer` is col, `inner` is row
     */
    int boardIndexFor(MoveDir dir, int outer, int inner) {
        switch (dir) {
            case MoveDir::Left:
            case MoveDir::Right:
                return outer * kBoardSide + inner;

            case MoveDir::Up:
            case MoveDir::Down:
                return inner * kBoardSide + outer;
        }

        return 0;
    }

    /**
     * Simulate one whole move.
     *
     * This does two jobs:
     *      1. Builds resulting board
     *      2. Records where each visible tile should slide
     *
     * That lets the animation move first, then commit the new board after.
     */
    MoveSimulation simulateMove(Board const& board, MoveDir dir) {
        MoveSimulation sim {};
        sim.board = board;

        /* Process each row/col depending on dir. */
        for (int outer = 0; outer < kBoardSide; ++outer) {
            std::array<int, kBoardSide> line {};
            std::array<int, kBoardSide> indices {};

            /**
             * Extract one logical line from the board.
             *
             * `line` = tile vals in move order
             * `indices` = flat board indices
             */
            for (int inner = 0; inner < kBoardSide; ++inner) {
                int flat = boardIndexFor(dir, outer, inner);
                indices[inner] = flat;
                line[inner] = board[flat / kBoardSide][flat % kBoardSide];
            }

            /**
             * Right / Down are handled by reversing logical view first.
             *
             * This means only one pack+merge needed.
             */
            bool reverse = (dir == MoveDir::Right || dir == MoveDir::Down);
            if (reverse) {
                std::reverse(line.begin(), line.end());
                std::reverse(indices.begin(), indices.end());
            }

            /**
             * `packed` stores only non-zero cells, while preserving original
             * indices so anim layer knows where tiles came from.
             */
            std::vector<std::pair<int, int>> packed;
            packed.reserve(kBoardSide);

            for (int i = 0; i < kBoardSide; ++i) {
                if (line[i] != 0) {
                    packed.emplace_back(line[i], indices[i]);
                }
            }

            std::array<int, kBoardSide> movedLine {};
            int outIndex = 0;

            /* Standard 2048 pack+merge pass. */
            for (size_t i = 0; i < packed.size(); ++i) {
                int destIndex = indices[outIndex];

                if (i + 1 < packed.size() && packed[i].first == packed[i + 1].first) {
                    int mergedValue = packed[i].first * 2;

                    movedLine[outIndex] = mergedValue;
                    sim.scoreGain += mergedValue;
                    sim.mergedIndices.push_back(destIndex);

                    /* Both source tiles travel to same dest. */
                    sim.motions.push_back({
                        packed[i].second,
                        destIndex,
                        packed[i].first,
                        true
                    });

                    sim.motions.push_back({
                        packed[i + 1].second,
                        destIndex,
                        packed[i + 1].first,
                        true
                    });

                    ++outIndex;
                    ++i; /* consume second tile. */
                }
                else {
                    /* No merge. */
                    movedLine[outIndex] = packed[i].first;

                    sim.motions.push_back({
                        packed[i].second,
                        destIndex,
                        packed[i].first,
                        false
                    });

                    ++outIndex;
                }
            }

            /* If line differs, move is valid. */
            if (movedLine != line) {
                sim.changed = true;
            }

            /* Reverse back for writeback if this was a Right / Down move. */
            auto writeLine = movedLine;
            if (reverse) {
                std::reverse(writeLine.begin(), writeLine.end());
            }

            /* Store processed line back into simulated board. */
            for (int inner = 0; inner < kBoardSide; ++inner) {
                int flat = boardIndexFor(dir, outer, inner);
                sim.board[flat / kBoardSide][flat % kBoardSide] = writeLine[inner];
            }
        }

        return sim;
    }
}

/* Main 2048 popup. */
class Game2048Popup : public Popup {
protected:
    /* Board contents. */
    std::array<std::array<int, kBoardSide>, kBoardSide> m_board {};

    /**
     * Visual tile node storage.
     *
     * `m_tileLayers` = background recs
     * `m_tileLabels` = bitmap font labels inside each tile
     * `m_slotLayers` = separate tile color
     */
    std::array<CCLayerColor*, kCellCount> m_tileLayers {};
    std::array<CCLabelBMFont*, kCellCount> m_tileLabels {};
    std::array<CCLayerColor*, kCellCount> m_slotLayers {};

    /* Board rendering helpers. */
    CCLayerColor* m_boardBg = nullptr;
    CCNode* m_animLayer = nullptr;
    std::array<CCPoint, kCellCount> m_cellCenters {};

    /* HUD labels. */
    CCLabelBMFont* m_scoreValueLabel = nullptr;
    CCLabelBMFont* m_bestValueLabel = nullptr;
    CCLabelBMFont* m_statusLabel = nullptr;

    /* Score state. */
    int m_score = 0;
    int m_bestScore = 0;

    /**
     * Run-state flags.
     *
     * `m_hasShownWinPopup`:
     *      Stops 2048 popup from spamming multiple times once 2048 has been reached.
     * `m_isGameOver`:
     *      Block further movements until reset.
     *
     * `m_isAnimating`:
            Blocks input while temp slide anim is in progress.
     */
    bool m_hasShownWinPopup = false;
    bool m_isGameOver = false;
    bool m_isAnimating = false;

    /* Pending post-anim state. */
    Board m_pendingBoard {};
    std::vector<int> m_pendingMergedIndices;
    int m_pendingSpawnIndex = -1;

    /* Random source for spawn placement. */
    std::mt19937 m_rng { std::random_device {}() };

protected:
    /* Utility for creating a popup control button. */
    /* UPDATE v1.1.1: Rewrite to avoid default buttons as this had an issue with chunky proportions. */
    CCMenuItemSpriteExtra* addControlButton(
        CCMenu* menu,
        char const* text,
        CCPoint position,
        SEL_MenuHandler callback,
        float buttonScale,
        float textScale
    ) {
        auto bg = CCScale9Sprite::create("GJ_button_05.png");
        bg->setContentSize({ 34.f, 34.f });
        bg->setScale(buttonScale);

        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setPosition({ 17.f, 17.f });
        label->setScale(textScale);
        bg->addChild(label);

        auto button = CCMenuItemSpriteExtra::create(bg, this, callback);
        button->setPosition(position);
        menu->addChild(button);
        return button;
    }

    /* UPDATE v1.1.3: Save progress when exiting a game. */
    void saveProgress() {
        /* Only persist if run is still alive. */
        if (m_isGameOver) {
            return;
        }

        auto mod = Mod::get();

        /* Flatten 4x4 into one array for storage. */
        std::vector<int> flat;
        flat.reserve(kCellCount);

        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                flat.push_back(m_board[row][col]);
            }
        }

        mod->setSavedValue("resume-exists", true);
        mod->setSavedValue("resume-board", flat);
        mod->setSavedValue("resume-score", m_score);
        mod->setSavedValue("resume-best-score", m_bestScore);
        mod->setSavedValue("resume-win-shown", m_hasShownWinPopup);
    }

    /* Clear progress. */
    void clearSavedProgress() {
        auto mod = Mod::get();

        mod->setSavedValue("resume-exists", false);
        mod->setSavedValue("resume-board", std::vector<int>{});
        mod->setSavedValue("resume-score", 0);
        mod->setSavedValue("resume-best-score", 0);
        mod->setSavedValue("resume-win-shown", false);
    }

    /* Load progress. */
    bool loadSavedProgress() {
        auto mod = Mod::get();

        if (!mod->getSavedValue<bool>("resume-exists", false)) {
            return false;
        }

        auto flat = mod->getSavedValue<std::vector<int>>("resume-board", {});
        if (flat.size() != kCellCount) {
            return false;
        }

        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                m_board[row][col] = flat[row * kBoardSide + col];
            }
        }

        m_score = mod->getSavedValue<int>("resume-score", 0);
        m_bestScore = mod->getSavedValue<int>("resume-best-score", 0);
        m_hasShownWinPopup = mod->getSavedValue<bool>("resume-win-shown", false);
        m_isGameOver = false;
        m_isAnimating = false;
        m_pendingMergedIndices.clear();
        m_pendingSpawnIndex = -1;

        //this->updateScoreLabels();
        this->refreshBoard();
        this->setStatus("Resumed previous game.", ccc3(119, 110, 101));
        return true;
    }

    void onClose(CCObject* sender) override {
        /* Save only when player is leaving still-active run. */
        if (!m_isGameOver) {
            this->saveProgress();
        }
        Popup::onClose(sender);
    }

    /* Popup init. */
    bool init() override {
        if (!Popup::init(kPopupWidth, kPopupHeight)) {
            return false;
        }

        this->setTitle("2048");

        auto const size = m_mainLayer->getContentSize();

        /* Main inner card. */
        auto card = makeSolid(cardColor(), 276.f, 246.f);
        card->setPosition({ size.width / 2.f, size.height / 2.f - 2.f });
        m_mainLayer->addChild(card, -1);

        /* SCORE panel. */
        //auto scorePanel = makeSolid(ccc4(187, 173, 160, 255), 62.f, 42.f);
        //scorePanel->setPosition({ 68.f, size.height - 34.f });
        //m_mainLayer->addChild(scorePanel);

        //auto scoreTitle = CCLabelBMFont::create("SCORE", "goldFont.fnt");
        //scoreTitle->setScale(0.34f);
        //scoreTitle->setColor(ccc3(245, 240, 232));
        //scoreTitle->setPosition({ 31.f, 30.f });
        //scorePanel->addChild(scoreTitle);

        //m_scoreValueLabel = CCLabelBMFont::create("0", "bigFont.fnt");
        //m_scoreValueLabel->setScale(0.40f);
        //m_scoreValueLabel->setColor(ccc3(255, 255, 255));
        //m_scoreValueLabel->setPosition({ 31.f, 13.f });
        //scorePanel->addChild(m_scoreValueLabel);

        /* BEST panel. */
        //auto bestPanel = makeSolid(ccc4(187, 173, 160, 255), 62.f, 42.f);
        //bestPanel->setPosition({ size.width - 68.f, size.height - 34.f });
        //m_mainLayer->addChild(bestPanel);

        //auto bestTitle = CCLabelBMFont::create("BEST", "goldFont.fnt");
        //bestTitle->setScale(0.34f);
        //bestTitle->setColor(ccc3(245, 240, 232));
        //bestTitle->setPosition({ 31.f, 30.f });
        //bestPanel->addChild(bestTitle);

        //m_bestValueLabel = CCLabelBMFont::create("0", "bigFont.fnt");
        //m_bestValueLabel->setScale(0.40f);
        //m_bestValueLabel->setColor(ccc3(255, 255, 255));
        //m_bestValueLabel->setPosition({ 31.f, 13.f });
        //bestPanel->addChild(m_bestValueLabel);

        /* Board root node. */
        auto boardRoot = CCNode::create();
        boardRoot->setContentSize({ kBoardSize, kBoardSize });
        boardRoot->ignoreAnchorPointForPosition(false);
        boardRoot->setAnchorPoint({ 0.5f, 0.5f });
        boardRoot->setPosition({ size.width / 2.f, 150.f });
        m_mainLayer->addChild(boardRoot);

        /* Board background. */
        m_boardBg = makeSolid(boardTrayColor(), kBoardSize, kBoardSize);
        m_boardBg->setPosition({ kBoardSize / 2.f, kBoardSize / 2.f });
        boardRoot->addChild(m_boardBg);

        /* Dedicated anim layer. */
        m_animLayer = CCNode::create();
        m_animLayer->setPosition(CCPointZero);
        m_boardBg->addChild(m_animLayer, 20);

        /**
         * Pre-create all tile visuals.
         *
         * Each board slot gets:
         *  - colored slot bg
         *  - colored layer
         *  - bitmap label centered on that layer
         */
        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                int index = row * kBoardSide + col;

                /* Local board coords for center of tile. */
                float x = kBoardPadding + (kCellSize / 2.f) + col * (kCellSize + kBoardGap);
                float y = kBoardSize - kBoardPadding - (kCellSize / 2.f) - row * (kCellSize + kBoardGap);

                m_cellCenters[index] = CCPoint{ x, y };

                auto slot = makeSolid(slotColor(), kCellSize, kCellSize);
                slot->setPosition({ x, y });
                m_boardBg->addChild(slot, 0);
                m_slotLayers[index] = slot;

                auto tile = makeSolid(tileColorForValue(2), kCellSize, kCellSize);
                tile->setPosition({ x, y });
                m_boardBg->addChild(tile, 1);
                m_tileLayers[index] = tile;

                auto label = CCLabelBMFont::create("", "chatFont.fnt");
                label->setPosition({ kCellSize / 2.f, kCellSize / 2.f - 1.f });
                label->setVisible(false);
                tile->addChild(label);

                m_tileLabels[index] = label;
            }
        }

        /* Status / hint label. */
        m_statusLabel = CCLabelBMFont::create("Arrow keys, WASD, or buttons", "goldFont.fnt");
        m_statusLabel->setScale(0.34f);
        m_statusLabel->setPosition({ size.width / 2.f, 42.f });
        m_statusLabel->setColor(ccc3(119, 110, 101));
        m_mainLayer->addChild(m_statusLabel);

        /* Mouse controls menu. */
        auto menu = CCMenu::create();
        menu->setPosition(CCPointZero);
        m_mainLayer->addChild(menu, 10);

        /* Button control row. */
        this->addControlButton(menu, "NEW", {  42.f, 20.f }, menu_selector(Game2048Popup::onNewGame), 0.58f, 0.65f);

        bool showButtons = Mod::get()->getSettingValue<bool>("button-support");
        if (showButtons) {
            this->addControlButton(menu, "^", { size.width / 2.f, 28.f }, menu_selector(Game2048Popup::onMoveUp), 0.72f, 0.65f);
            this->addControlButton(menu, "<", { size.width / 2.f - 38.f, 8.f }, menu_selector(Game2048Popup::onMoveLeft), 0.72f, 0.65f);
            this->addControlButton(menu, "v", { size.width / 2.f, 8.f }, menu_selector(Game2048Popup::onMoveDown), 0.72f, 0.65f);
            this->addControlButton(menu, ">", { size.width / 2.f + 38.f, 8.f }, menu_selector(Game2048Popup::onMoveRight), 0.72f, 0.65f);
        }

        /* Load best score. */
        //m_bestScore = Mod::get()->getSavedValue<int>("best-score", 0);

        /* Resume existing game if present. */
        if (!this->loadSavedProgress()) {
            this->resetGame();
        }
        return true;
    }

    /* Update bottom status label text+color. */
    void setStatus(std::string const& text, ccColor3B color = ccc3(119, 110, 101)) {
        if (!m_statusLabel) return;
        m_statusLabel->setString(text.c_str());
        m_statusLabel->setColor(color);
    }

    /* Refresh score and best-score labels. */
    void updateScoreLabels() {
        if (m_scoreValueLabel) {
            //m_scoreValueLabel->setString(std::to_string(m_score).c_str());
            return;
        }

        if (m_bestValueLabel) {
            //m_bestValueLabel->setString(std::to_string(m_bestScore).c_str());
            return;
        }
    }

    /* Returns if anims are enabled. */
    bool tileAnimationsEnabled() const {
        auto mod = Mod::get();
        if (!mod->hasSetting(kTileAnimationSettingKey)) {
            return true;
        }
        return mod->getSettingValue<bool>(kTileAnimationSettingKey);
    }

    /* Create temp visual tile for anim layer. */
    CCLayerColor* createAnimatedTile(int value) {
        auto tile = makeSolid(tileColorForValue(value), kCellSize, kCellSize);

        auto label = CCLabelBMFont::create(std::to_string(value).c_str(), "chatFont.fnt");
        label->setPosition({ kCellSize / 2.f, kCellSize / 2.f - 1.f });
        label->setScale(tileLabelScaleForValue(value));
        label->setColor(tileTextColorForValue(value));
        tile->addChild(label);

        return tile;
    }

    /* Clear all transient sliding tiles. */
    void clearAnimationLayer() {
        if (m_animLayer) {
            m_animLayer->removeAllChildrenWithCleanup(true);
        }
    }

    /* Hide all board tiles temporarily. */
    void hideStaticTiles() {
        for (int i = 0; i < kCellCount; ++i) {
            if (m_tileLayers[i]) {
                m_tileLayers[i]->setVisible(false);
            }
            if (m_tileLabels[i]) {
                m_tileLabels[i]->setVisible(false);
            }
        }
    }

    /**
     * Push current board state into visual tile grid.
     *
     * `pulseIndex` = optional flat tile index to animate as newly spawned tile.
     * `mergedIndices` = cells that should get merge "pop" anim.
     */
    void refreshBoard(int pulseIndex = -1, std::vector<int> const& mergedIndices = {}) {
        bool animate = this->tileAnimationsEnabled();

        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                int index = row * kBoardSide + col;
                int value = m_board[row][col];

                auto tile = m_tileLayers[index];
                auto label = m_tileLabels[index];

                if (!tile || !label) {
                    continue;
                }

                /* Clear any prior anims. */
                tile->stopAllActions();
                tile->setScale(1.f);

                /* Empty cell: hide label. */
                if (value == 0) {
                    tile->setVisible(false);
                    label->setVisible(false);
                    label->setString("");
                    continue;
                }

                tile->setVisible(true);
                applyColor(tile, tileColorForValue(value));

                /* Occupied cell: update text+styling. */
                label->setVisible(true);
                label->setString(std::to_string(value).c_str());
                label->setScale(tileLabelScaleForValue(value));
                label->setColor(tileTextColorForValue(value));

                /* Spawn pulse. */
                if (index == pulseIndex) {
                    tile->setScale(0.18f);
                    tile->runAction(
                        CCEaseBackOut::create(
                            CCScaleTo::create(0.20f, 1.f)
                        )
                    );
                }

                /* Merge destination gets quick pop effect. */
                else if (std::find(mergedIndices.begin(), mergedIndices.end(), index) != mergedIndices.end()) {
                    tile->setScale(1.f);
                    tile->runAction(
                        CCSequence::create(
                            CCEaseSineOut::create(CCScaleTo::create(kTileMergePopDuration, 1.14f)),
                            CCEaseSineIn::create(CCScaleTo::create(kTileMergePopDuration, 1.f)),
                            nullptr
                        )
                    );
                }
            }
        }
    }

    /* Update best-score state if current score exceeded it. */
    void updateBestScore() {
        if (m_score > m_bestScore) {
            //m_bestScore = m_score;
            //Mod::get()->setSavedValue<int>("best-score", m_bestScore);
            return;
        }
    }

    /**
     * Spawn one random tile into an empty cell.
     *
     * Spawn rules:
     *  - 90% chance for 2
     *  - 10% chance for 4
     */
    int spawnRandomTile(Board& board) {
        std::vector<int> empty;
        empty.reserve(kCellCount);

        /* Collect all empty board slots. */
        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                if (board[row][col] == 0) {
                    empty.push_back(row * kBoardSide + col);
                }
            }
        }

        /* No empty space left. */
        if (empty.empty()) {
            return -1;
        }

        std::uniform_int_distribution<size_t> pickCell(0, empty.size() - 1);
        std::uniform_int_distribution<int> pickValue(1, 10);

        int index = empty[pickCell(m_rng)];
        int row = index / kBoardSide;
        int col = index % kBoardSide;

        /* Spawn odds. */
        board[row][col] = (pickValue(m_rng) == 10) ? 4 : 2;
        return index;
    }

    /* Build move sim from current board. */
    MoveSimulation computeMove(MoveDir dir) const {
        return simulateMove(m_board, dir);
    }

    /* Finalize visual/state consequences of successful move. */
    /* UPDATE v1.1.2: Defer "Game Over" / "2048!" by one frame. */
    /* UPDATE v1.1.3: Clear save progress if game over. */
    void finishMove(int pulseIndex, std::vector<int> const& mergedIndices = {}) {
        this->refreshBoard(pulseIndex, mergedIndices);
        this->updateScoreLabels();

        /* First time reaching 2048. */
        if (!m_hasShownWinPopup && boardHasValue(m_board, 2048)) {
            m_hasShownWinPopup = true;
            this->setStatus("2048 reached. Keep climbing.", ccc3(90, 140, 70));

            this->runAction(CCSequence::create(
                CCDelayTime::create(0.f),
                CCCallFunc::create(this, callfunc_selector(Game2048Popup::showWinPopup)),
                nullptr
            ));

            return;
        }

        /* No legal moves. */
        if (!boardCanMove(m_board)) {
            m_isGameOver = true;
            /* Clear saved progress. */
            this->clearSavedProgress();
            
            this->setStatus("No moves left.", ccc3(180, 70, 70));

            this->runAction(CCSequence::create(
                CCDelayTime::create(0.f),
                CCCallFunc::create(this, callfunc_selector(Game2048Popup::showGameOverPopup)),
                nullptr
            ));

            return;
        }

        /* Post-move status text. */
        if (m_hasShownWinPopup) {
            this->setStatus("Still alive. Bigger tiles are possible.", ccc3(90, 140, 70));
        }
        else {
            this->setStatus("Arrow keys or WASD.", ccc3(119, 110, 101));
        }
    }

    /* Start move anim. */
    void runMoveAnimation(MoveSimulation const& move, Board const& finalBoard, int spawnIndex) {
        if (!m_animLayer) {
            m_board = finalBoard;
            this->finishMove(spawnIndex, move.mergedIndices);
            return;
        }

        m_isAnimating = true;
        m_pendingBoard = finalBoard;
        m_pendingMergedIndices = move.mergedIndices;
        m_pendingSpawnIndex = spawnIndex;
        
        this->clearAnimationLayer();
        this->hideStaticTiles();

        for (auto const& motion : move.motions) {
            if (motion.value == 0 || motion.fromIndex < 0 || motion.toIndex < 0) {
                continue;
            }

            auto animatedTile = this->createAnimatedTile(motion.value);
            animatedTile->setPosition(m_cellCenters[motion.fromIndex]);
            m_animLayer->addChild(animatedTile);

            /**
             * Even if a tile technically stays in place, keep tiny delay so
             * move resolves on one timing path.
             */
            CCFiniteTimeAction* moveAction = nullptr;

            if (motion.fromIndex == motion.toIndex) {
                moveAction = CCDelayTime::create(kTileMoveAnimDuration);
            }
            else {
                moveAction = CCEaseSineOut::create(
                    CCMoveTo::create(kTileMoveAnimDuration, m_cellCenters[motion.toIndex])
                );
            }

            animatedTile->runAction(moveAction);
        }

        /* Once travel is done, commit to real board. */
        this->runAction(
            CCSequence::create(
                CCDelayTime::create(kTileMoveAnimDuration),
                CCCallFunc::create(this, callfunc_selector(Game2048Popup::onMoveAnimationFinished)),
                nullptr
            )
        );
    }

    /* End-of-anim callback. */
    void onMoveAnimationFinished() {
        m_isAnimating = false;

        this->clearAnimationLayer();

        m_board = m_pendingBoard;
        this->finishMove(m_pendingSpawnIndex, m_pendingMergedIndices);

        m_pendingMergedIndices.clear();
        m_pendingSpawnIndex = -1;
    }

    /* Show `Win` popup. */
    void showWinPopup() {
        createQuickPopup(
            "2048!",
            "Nice. The <cg>2048</c> tile is on the board!",
            "Keep Going",
            "New Game",
            [this](auto, bool btn2) {
                if (btn2) {
                    this->resetGame();
                }
            }
        );
    }

    /* Show `Game Over` popup. */
    void showGameOverPopup() {
        createQuickPopup(
            "Game Over",
            ("<cl>Final Score:</c> <cy>" + std::to_string(m_score) + "</c>").c_str(),
            "Close",
            "Retry",
            [this](auto, bool btn2) {
                if (btn2) {
                    this->resetGame();
                }
            }
        );
    }

    /* Public-facing movement entry point. Keyboard/button callbacks use this. */
    /* UPDATE v1.1.2: Block input while animation. `showWinPopup()`, `showGameOverPopup()` */
    void requestMove(MoveDir dir) {
        if (m_isGameOver || m_isAnimating) {
            return;
        }

        /* Simulate first to know where tiles should slide. */
        auto move = this->computeMove(dir);

        /* Ignore invalid moves. */
        if (!move.changed) {
            return;
        }

        /* Spawn happens after successful move, not inside sim. */
        Board nextBoard = move.board;
        int spawned = this->spawnRandomTile(nextBoard);

        m_score += move.scoreGain;
        this->updateBestScore();
        this->updateScoreLabels();

        /* Either animate move or apply it instantly depending on settings. */
        if (this->tileAnimationsEnabled()) {
            this->runMoveAnimation(move, nextBoard, spawned);
        }
        else {
            m_board = nextBoard;
            this->finishMove(spawned, move.mergedIndices);
        }
    }

    /* Reset to a new game. */
    void resetGame() {
        /* Clear saved progress. */
        this->clearSavedProgress();

        this->stopAllActions();
        this->clearAnimationLayer();

        m_isAnimating = false;
        m_pendingMergedIndices.clear();
        m_pendingSpawnIndex = -1;

        for (auto& row : m_board) {
            row.fill(0);
        }

        m_score = 0;
        m_isGameOver = false;
        m_hasShownWinPopup = false;

        /* Two tile opening. */
        int first = this->spawnRandomTile(m_board);
        int second = this->spawnRandomTile(m_board);
        (void)first; /* only pulse most recent tile. */

        //this->updateScoreLabels();
        this->refreshBoard(second);
        this->setStatus("Arrow keys or WASD.", ccc3(119, 110, 101));
    }

public:
    static Game2048Popup* create() {
        auto ret = new Game2048Popup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    /* Keyboard input handler. */
    void keyDown(enumKeyCodes key, double timestamp) override {
        switch (key) {
            case KEY_Left:
            case KEY_ArrowLeft:
            case KEY_A:
                this->requestMove(MoveDir::Left);
                return;

            case KEY_Right:
            case KEY_ArrowRight:
            case KEY_D:
                this->requestMove(MoveDir::Right);
                return;

            case KEY_Up:
            case KEY_ArrowUp:
            case KEY_W:
                this->requestMove(MoveDir::Up);
                return;

            case KEY_Down:
            case KEY_ArrowDown:
            case KEY_S:
                this->requestMove(MoveDir::Down);
                return;

            case KEY_R:
                this->resetGame();
                return;

            default:
                Popup::keyDown(key, timestamp);
                return;
        }
    }

    /* Button callbacks. */
    void onNewGame(CCObject*) {
        this->resetGame();
    }

    void onMoveUp(CCObject*) {
        this->requestMove(MoveDir::Up);
    }

    void onMoveLeft(CCObject*) {
        this->requestMove(MoveDir::Left);
    }

    void onMoveDown(CCObject*) {
        this->requestMove(MoveDir::Down);
    }

    void onMoveRight(CCObject*) {
        this->requestMove(MoveDir::Right);
    }
};

/* Add 2048 button to main menu. */
class $modify(MenuLayer2048, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        /* Preferred attachment point. */
        auto menu = typeinfo_cast<CCMenu*>(this->getChildByID("bottom-menu"));

        auto sprite = CCSprite::create("2048-menu-icon.png"_spr);
        sprite->setScale(0.5f);
        if (!sprite) {
            log::error("Failed to load 2048-menu-icon.png");
            return true;
        }

        auto button = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            menu_selector(MenuLayer2048::onOpen2048)
        );

        button->setID("open-2048-button"_spr);
        button->setScale(0.60f);

        if (menu) {
            menu->addChild(button);
            menu->updateLayout();
        }
        else {
            auto fallbackMenu = CCMenu::create();
            fallbackMenu->setPosition(CCPointZero);
            fallbackMenu->addChild(button);

            auto winSize = CCDirector::sharedDirector()->getWinSize();
            button->setPosition({
                winSize.width - 52.f,
                42.f
            });

            fallbackMenu->setID("2048-fallback-menu"_spr);
            this->addChild(fallbackMenu);
        }

        return true;
    }

    /* Open 2048 popup. */
    void onOpen2048(CCObject*) {
        if (auto popup = Game2048Popup::create()) {
            popup->show();
        }
    }
};