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

    /* Move directions. */
    enum class MoveDir {
        Left,
        Right,
        Up,
        Down
    };

    /**
     * Result of collapsing a single 4-cell line.
     *
     * `line` = new state after pack+merge
     * `scoreGain` = how many points it contributed
     * `changed` = whether the line changed
     */
    struct LineMoveResult {
        std::array<int, kBoardSide> line {};
        int scoreGain = 0;
        bool changed = false;
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
    bool boardHasValue(std::array<std::array<int, kBoardSide>, kBoardSide> const& board, int target) {
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
    bool boardCanMove(std::array<std::array<int, kBoardSide>, kBoardSide> const& board) {
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
     * Collapse single line.
     *
     * Example:
     *  [2, 0, 2, 4] -> [4, 4, 0, 0]
     *  [2, 2, 2, 2] -> [4, 4, 0, 0]
     */
    LineMoveResult collapseLine(std::array<int, kBoardSide> const& input) {
        std::vector<int> packed;
        packed.reserve(kBoardSide);

        /* Step 1: strip zeroes. */
        for (int value : input) {
            if (value != 0) {
                packed.push_back(value);
            }
        }

        LineMoveResult result {};

        /* Step 2: merge neighbors. */
        int outIndex = 0;
        for (size_t i = 0; i < packed.size(); ++i) {
            if (i + 1 < packed.size() && packed[i] == packed[i + 1]) {
                int merged = packed[i] * 2;
                result.line[outIndex++] = merged;
                result.scoreGain += merged;
                ++i; /* consume second tile. */
            }
            else {
                result.line[outIndex++] = packed[i];
            }
        }

        /* Step 3: compare against original to see if anything changed. */
        result.changed = (result.line != input);
        return result;
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
     */
    bool m_hasShownWinPopup = false;
    bool m_isGameOver = false;

    /* Random source for spawn placement. */
    std::mt19937 m_rng { std::random_device {}() };

protected:
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
        auto boardBg = makeSolid(boardTrayColor(), kBoardSize, kBoardSize);
        boardBg->setPosition({ kBoardSize / 2.f, kBoardSize / 2.f });
        boardRoot->addChild(boardBg);

        /**
         * Pre-create all tile visuals.
         *
         * Each board slot gets:
         *  - colored layer
         *  - bitmap label centered on that layer
         */
        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                int index = row * kBoardSide + col;

                /* Local board coords for center of tile. */
                float x = kBoardPadding + (kCellSize / 2.f) + col * (kCellSize + kBoardGap);
                float y = kBoardSize - kBoardPadding - (kCellSize / 2.f) - row * (kCellSize + kBoardGap);

                auto slot = makeSolid(slotColor(), kCellSize, kCellSize);
                slot->setPosition({ x, y });
                boardBg->addChild(slot, 0);
                m_slotLayers[index] = slot;

                auto tile = makeSolid(tileColorForValue(2), kCellSize, kCellSize);
                tile->setPosition({ x, y });
                boardBg->addChild(tile, 1);
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
        this->addControlButton(menu, "NEW", {  52.f, 20.f }, menu_selector(Game2048Popup::onNewGame),   0.58f);
        //this->addControlButton(menu, "UP", { size.width / 2.f, 28.f }, menu_selector(Game2048Popup::onMoveUp),    0.50f);
        //this->addControlButton(menu, "LEFT", { size.width / 2.f - 40.f, 8.f }, menu_selector(Game2048Popup::onMoveLeft), 0.48f);
        //this->addControlButton(menu, "DOWN", { size.width / 2.f, 8.f }, menu_selector(Game2048Popup::onMoveDown), 0.48f);
        //this->addControlButton(menu, "RIGHT", { size.width / 2.f + 40.f, 8.f }, menu_selector(Game2048Popup::onMoveRight), 0.48f);

        /* Load best score. */
        //m_bestScore = Mod::get()->getSavedValue<int>("best-score", 0);

        /* Start new run. */
        this->resetGame();
        return true;
    }

    /* Utility for creating a popup control button. */
    CCMenuItemSpriteExtra* addControlButton(
        CCMenu* menu,
        char const* text,
        CCPoint position,
        SEL_MenuHandler callback,
        float scale
    ) {
        auto sprite = ButtonSprite::create(text);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, callback);
        button->setPosition(position);
        button->setScale(scale);
        menu->addChild(button);
        return button;
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

    /**
     * Push current board state into visual tile grid.
     *
     * `pulseIndex` = optional flat tile index to animate as newly spawned tile.
     */
    void refreshBoard(int pulseIndex = -1) {
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
    int spawnRandomTile() {
        std::vector<int> empty;
        empty.reserve(kCellCount);

        /* Collect all empty board slots. */
        for (int row = 0; row < kBoardSide; ++row) {
            for (int col = 0; col < kBoardSide; ++col) {
                if (m_board[row][col] == 0) {
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
        m_board[row][col] = (pickValue(m_rng) == 10) ? 4 : 2;
        return index;
    }

    /* Apply directional move to full board. */
    bool applyMove(MoveDir dir) {
        bool changed = false;
        int totalScoreGain = 0;

        /* Process each row/column depending on move dir. */
        for (int outer = 0; outer < kBoardSide; ++outer) {
            std::array<int, kBoardSide> line {};

            /**
             * Extract one logical line from the board.
             *
             * Left/Right -> rows
             * Up/Down -> cols
             */
            for (int inner = 0; inner < kBoardSide; ++inner) {
                switch (dir) {
                    case MoveDir::Left:
                    case MoveDir::Right:
                        line[inner] = m_board[outer][inner];
                        break;

                    case MoveDir::Up:
                    case MoveDir::Down:
                        line[inner] = m_board[inner][outer];
                        break;
                }
            }

            /* Reuse same logic for Right/Down by reversing first. */
            bool reverse = (dir == MoveDir::Right || dir == MoveDir::Down);
            if (reverse) {
                std::reverse(line.begin(), line.end());
            }

            auto moved = collapseLine(line);

            /* Reverse back so writeback uses normal board orientation. */
            if (reverse) {
                std::reverse(moved.line.begin(), moved.line.end());
            }

            changed = changed || moved.changed;
            totalScoreGain += moved.scoreGain;

            /* Write processed line back to board. */
            for (int inner = 0; inner < kBoardSide; ++inner) {
                switch (dir) {
                    case MoveDir::Left:
                    case MoveDir::Right:
                        m_board[outer][inner] = moved.line[inner];
                        break;

                    case MoveDir::Up:
                    case MoveDir::Down:
                        m_board[inner][outer] = moved.line[inner];
                        break;
                }
            }
        }

        /* If nothing changed, invalid move. */
        if (!changed) {
            return false;
        }

        m_score += totalScoreGain;
        //this->updateBestScore();
        //this->updateScoreLabels();
        return true;
    }

    /* Finalize visual/state consequences of successful move. */
    void finishMove(int pulseIndex) {
        this->refreshBoard(pulseIndex);
        this->updateScoreLabels();

        /* First time reaching 2048. */
        if (!m_hasShownWinPopup && boardHasValue(m_board, 2048)) {
            m_hasShownWinPopup = true;
            this->setStatus("2048 reached. Keep climbing.", ccc3(90, 140, 70));

            createQuickPopup(
                "2048!",
                "Nice. The <cg>2048</c> tile is on the board.",
                "Keep Going",
                "New Game",
                [this](auto, bool btn2) {
                    if (btn2) {
                        this->resetGame();
                    }
                }
            );

            return;
        }

        /* No legal moves. */
        if (!boardCanMove(m_board)) {
            m_isGameOver = true;
            this->setStatus("No moves left.", ccc3(180, 70, 70));

            createQuickPopup(
                "Game Over",
                ("Final score: " + std::to_string(m_score)).c_str(),
                "Close",
                "Retry",
                [this](auto, bool btn2) {
                    if (btn2) {
                        this->resetGame();
                    }
                }
            );

            return;
        }

        /* Post-move status text. */
        if (m_hasShownWinPopup) {
            this->setStatus("Still alive. Bigger tiles are possible.", ccc3(90, 140, 70));
        }
        else {
            this->setStatus("Arrow keys or WASD", ccc3(119, 110, 101));
        }
    }

    /* Public-facing movement entry point. Keyboard/button callbacks use this. */
    void requestMove(MoveDir dir) {
        if (m_isGameOver) {
            return;
        }

        /* Ignore invalid moves. */
        if (!this->applyMove(dir)) {
            return;
        }

        int spawned = this->spawnRandomTile();
        this->finishMove(spawned);
    }

    /* Reset to a new game. */
    void resetGame() {
        for (auto& row : m_board) {
            row.fill(0);
        }

        m_score = 0;
        m_isGameOver = false;
        m_hasShownWinPopup = false;

        /* Two tile opening. */
        int first = this->spawnRandomTile();
        int second = this->spawnRandomTile();
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