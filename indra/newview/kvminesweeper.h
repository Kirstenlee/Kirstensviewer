/**
 * @file kvminesweeper.h
 * @brief KV Minesweeper - standalone Minesweeper floater, self-contained game
 * system fired from the S24 console ("minesweeper" / "mines"). Small/Medium/
 * Large board sizes x Easy/Medium/Hard mine density, first-click-safe mine
 * placement, flood-fill reveal, flag toggling, best-time-per-combination
 * persisted across sessions. Unlike the card games in this series, the cell
 * grid is built entirely at runtime (not pre-declared in XUI) since the
 * board shape changes with the chosen size - see rebuildGrid().
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

#ifndef LL_KVMINESWEEPER_H
#define LL_KVMINESWEEPER_H

#include "llfloater.h"
#include "v4color.h"
#include <vector>
#include <map>
#include <string>

class LLButton;
class LLTextBox;
class LLPanel;

class KVFloaterMinesweeper : public LLFloater
{
public:
    KVFloaterMinesweeper(const LLSD& key);
    ~KVFloaterMinesweeper();

    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void draw() override;

private:
    enum class BoardSize { Small, Medium, Large };
    enum class Difficulty { Easy, Medium, Hard };

    struct Cell
    {
        bool isMine = false;
        bool revealed = false;
        bool flagged = false;
        S32 adjacent = 0;
    };

    // S24: (re)builds the whole cell grid from scratch for the CURRENTLY
    // selected mSize/mDifficulty - destroys every existing cell button
    // (grid_panel->deleteAllChildren()) and creates fresh ones sized to the
    // new board, then resizes the floater itself to fit (top-left corner
    // kept fixed on screen - see the .cpp for why setRect() is used
    // directly instead of reshape()). This is the one method that makes
    // the three board sizes possible without pre-declaring a fixed max-size
    // grid of mostly-hidden XUI buttons like the card games do.
    void newGame();
    void rebuildGrid();
    void placeMines(S32 safeRow, S32 safeCol); // lazy - called on the first reveal, excludes that cell
    void computeAdjacents();

    void onCellClicked(S32 row, S32 col);
    void onCellRightClicked(S32 row, S32 col); // flag toggle
    void floodReveal(S32 row, S32 col); // iterative flood fill from a zero-adjacent cell
    void revealAllMines(); // mExplodedIdx must already be set by the caller
    bool checkWinCondition() const;
    void maybeSaveBestTime();

    void onSizeClicked();       // cycles the PENDING size selection - takes effect on the next New Game
    void onDifficultyClicked(); // cycles the PENDING difficulty selection - same
    void onNewGameClicked();

    void renderCell(S32 row, S32 col);
    void renderAll();
    void updateMinesRemainingText();
    void updateTimerText();
    void updateBestText();
    void setStatus(const std::string& text);
    void playSound(const char* settingsName);

    void loadBestTimes();
    void saveBestTimes();
    std::string bestTimeKey() const;

    S32 rowsForSize(BoardSize s) const;
    S32 colsForSize(BoardSize s) const;
    F32 densityForDifficulty(Difficulty d) const;
    std::string sizeLabel(BoardSize s) const;
    std::string difficultyLabel(Difficulty d) const;
    LLColor4 numberColor(S32 n) const;

    BoardSize mSize = BoardSize::Small;
    Difficulty mDifficulty = Difficulty::Easy;

    S32 mRows = 9;
    S32 mCols = 9;
    S32 mMineCount = 10;
    S32 mFlagCount = 0;
    S32 mRevealedCount = 0;
    S32 mExplodedIdx = -1; // which mine (flat index) ended the game, for the red-tint cell

    bool mMinesPlaced = false; // mines are placed lazily, excluding whichever cell is clicked first
    bool mGameOver = true;
    bool mTiming = false;
    F32 mElapsedSeconds = 0.0f;

    std::vector<Cell> mGrid;              // flat, row*mCols+col
    std::vector<LLButton*> mCellButtons;  // flat, parallel to mGrid - rebuilt every rebuildGrid()

    std::map<std::string, F32> mBestTimes; // key = "<size>_<difficulty>", seconds, absent = no best yet

    LLPanel* mGridPanel = nullptr;
    LLTextBox* mStatusText = nullptr;
    LLTextBox* mMinesText = nullptr;
    LLTextBox* mTimerText = nullptr;
    LLTextBox* mBestText = nullptr;
    LLButton* mSizeBtn = nullptr;
    LLButton* mDifficultyBtn = nullptr;
    LLButton* mNewGameBtn = nullptr;
};

#endif // LL_KVMINESWEEPER_H
