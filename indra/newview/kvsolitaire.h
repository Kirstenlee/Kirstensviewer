/**
 * @file kvsolitaire.h
 * @brief KV Solitaire - standalone Klondike floater, self-contained game
 * system fired from the S24 console ("play solitaire" / "solitaire" /
 * "klondike"). Reuses the real card textures built for KV Blackjack
 * (newview/skins/default/textures/cards/) - no new card art. Classic 3-card
 * draw, completion-based scoring, ends on a win or on a provable "no legal
 * move left" state.
 *
 * Copyright (c) 2026 Kirstenlee Cinquetti (Lee Quick)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LL_KVSOLITAIRE_H
#define LL_KVSOLITAIRE_H

#include "llfloater.h"
#include "lltimer.h"
#include <vector>
#include <string>

class LLButton;
class LLTextBox;

// S24: deliberate scope simplification from full Klondike, same "cheap but
// honest" spirit as KV Reversi's non-search AI - only SINGLE cards move at
// a time (the top card of the waste, or a tableau column's exposed top
// card). Real Klondike lets you drag a whole descending/alternating run of
// exposed cards together; implementing that needs sequence validation and
// multi-card relocation, real added complexity for a console timewaster.
// Single-card moves are still a fully playable, classic-feeling game.
class KVFloaterSolitaire : public LLFloater
{
public:
    KVFloaterSolitaire(const LLSD& key);
    ~KVFloaterSolitaire();

    bool postBuild() override;
    void onClose(bool app_quitting) override;

private:
    static const S32 NUM_COLS = 7;
    // S24: 13 is a deliberate, documented cap, not a real rule of the game.
    // A column can in principle hold more (the data model - mTableau[col] -
    // has no cap at all), but reserving on-screen space for the worst case
    // (up to 19 cards) would make the floater impractically tall for a
    // console timewaster. A column this deep is rare in normal play; if it
    // happens, cards beyond the 13th simply have no slot to render into
    // (still tracked correctly, just not visible) rather than compressing
    // every column's layout to fit.
    static const S32 MAX_COL_SLOTS = 13;

    // rank 1=A .. 13=K, suit 0=S,1=H,2=D,3=C (same encoding as KV Blackjack's Card).
    struct Card
    {
        S32 rank = 0;
        S32 suit = 0;
        bool faceUp = false;
        std::string iconName() const;
        bool isRed() const { return suit == 1 || suit == 2; }
    };

    enum class SourceType { None, Waste, Tableau };

    void newGame();
    void buildAndShuffleDeck(std::vector<Card>& deck);

    void onStockClicked();
    void onWasteClicked();
    void onTableauClicked(S32 col, S32 row);
    void onFoundationClicked(S32 suit);
    void onNewGameClicked();

    // S24: LLButton has no dedicated double-click callback - its own
    // handleDoubleClick() just re-fires a single click (confirmed in
    // llbutton.cpp). Detected manually here instead: every click records
    // (kind, col, row) and resets a timer; a second click on the SAME
    // target within kDoubleClickSeconds counts as a double-click.
    bool isDoubleClick(S32 kind, S32 col, S32 row);
    void autoSendToFoundation(const Card& c, SourceType src, S32 srcCol);

    void selectWasteTop();
    // S24: picks up a STACK, not just a single card - every card from `row`
    // to the column's top, inclusive. Always legal to pick up: the face-up
    // portion of any column is guaranteed to already be one valid
    // descending/alternating run, because every card in it was only ever
    // placed there after passing canPlaceOnTableau() in the first place -
    // this file never needs to separately re-validate "is this a stack".
    void selectTableauStack(S32 col, S32 row);
    void clearSelection();
    void tryMoveSelectedToTableau(S32 destCol, S32 clickedRow);
    void moveTableauRun(S32 srcCol, S32 srcRow, S32 destCol); // relocates [srcRow..top] of srcCol onto destCol
    void tryMoveSelectedToFoundation(S32 destSuit);

    bool canPlaceOnTableau(const Card& c, S32 col) const;
    bool canPlaceOnFoundation(const Card& c, S32 suit) const;
    void exposeNewTableauTop(S32 col); // flips the new top card face-up if needed, with a sound
    void onSuccessfulMove();           // marks progress + re-renders + checks for a win
    void checkForStuck();              // true "nothing at all can be done" - proactive, after every state change

    S32 foundationCardCount() const;
    S32 currentScore() const { return foundationCardCount() * 10; }
    bool hasAnyLegalMove() const;

    void endGame(bool won);

    void renderAll();
    void renderStockAndWaste();
    void renderFoundations();
    void renderTableau(S32 col);
    void updateStatus();
    void setStatus(const std::string& text);
    void playSound(const char* settingsName);

    void loadStats();
    void saveStats();

    std::vector<Card> mStock;              // back() = next to draw
    std::vector<Card> mWaste;              // back() = top/playable card
    std::vector<Card> mTableau[NUM_COLS];  // back() = top card of each column
    std::vector<Card> mFoundation[4];      // indexed by suit; back() = top card

    SourceType mSelectedType = SourceType::None;
    S32 mSelectedCol = -1; // tableau column index, only meaningful when mSelectedType == Tableau
    S32 mSelectedRow = -1; // first row of the held stack within mSelectedCol (through to the column's top)

    static const F32 kDoubleClickSeconds; // defined in the .cpp
    LLTimer mClickTimer;
    S32 mLastClickKind = -1; // 0 = waste, 1 = tableau, -1 = none yet
    S32 mLastClickCol = -1;
    S32 mLastClickRow = -1;

    bool mGameOver = true;
    bool mProgressMadeThisPass = false; // reset on each stock recycle; a recycle with no progress since the last one ends the game

    S32 mBestScore = 0;
    S32 mGamesPlayed = 0;
    S32 mGamesWon = 0;

    LLTextBox* mStatusText = nullptr;
    LLTextBox* mStatsText = nullptr;
    LLButton* mNewGameBtn = nullptr;

    LLButton* mStockBtn = nullptr;
    LLButton* mWasteBtn = nullptr;
    LLButton* mFoundationBtn[4] = {};
    LLButton* mTableauSlots[NUM_COLS][MAX_COL_SLOTS] = {};
};

#endif // LL_KVSOLITAIRE_H
