/**
 * @file kvminesweeper.cpp
 * @brief KV Minesweeper - standalone Minesweeper floater implementation.
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

#include "llviewerprecompiledheaders.h"

#include "kvminesweeper.h"

#include "llagent.h"
#include "llappviewer.h" // gFrameIntervalSeconds
#include "llaudioengine.h"
#include "llbutton.h"
#include "lldir.h"
#include "llpanel.h"
#include "llrand.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"

namespace
{
    std::string minesweeperBestPath()
    {
        // S24: global to the viewer install, not per-SL-account - same
        // reasoning as every other best-score/stats file in this series.
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "kv_minesweeper_best.xml");
    }

    const char* kCovered = "minesweeper/covered.png";
    const char* kRevealed = "minesweeper/revealed.png";
    const char* kFlag = "minesweeper/flag.png";
    const char* kMine = "minesweeper/mine.png";

    const S32 CELL = 24;
    const S32 GRID_LEFT = 12;
    const S32 GRID_TOP_OFFSET = 112; // distance from the floater's TOP edge down to the grid panel's top
    const S32 BOTTOM_MARGIN = 12;
    const S32 CONTROL_MIN_WIDTH = 380; // narrowest the floater can be and still fit the control row (Small board is narrower than this on its own)

    const F32 SOUND_GAIN = 1.0f;
    const LLColor4 kExplodedTint(1.0f, 0.4f, 0.4f, 1.0f);
}

KVFloaterMinesweeper::KVFloaterMinesweeper(const LLSD& key)
    : LLFloater(key)
{
}

KVFloaterMinesweeper::~KVFloaterMinesweeper()
{
}

bool KVFloaterMinesweeper::postBuild()
{
    mStatusText = getChild<LLTextBox>("status_text");
    mMinesText = getChild<LLTextBox>("mines_text");
    mTimerText = getChild<LLTextBox>("timer_text");
    mBestText = getChild<LLTextBox>("best_text");
    mGridPanel = getChild<LLPanel>("grid_panel");

    mSizeBtn = getChild<LLButton>("size_btn");
    mSizeBtn->setClickedCallback(boost::bind(&KVFloaterMinesweeper::onSizeClicked, this));

    mDifficultyBtn = getChild<LLButton>("difficulty_btn");
    mDifficultyBtn->setClickedCallback(boost::bind(&KVFloaterMinesweeper::onDifficultyClicked, this));

    mNewGameBtn = getChild<LLButton>("new_game_btn");
    mNewGameBtn->setClickedCallback(boost::bind(&KVFloaterMinesweeper::onNewGameClicked, this));

    loadBestTimes();
    newGame();

    return LLFloater::postBuild();
}

void KVFloaterMinesweeper::onClose(bool app_quitting)
{
    // best times are already saved immediately on every new best (see
    // maybeSaveBestTime()) - nothing in-progress is worth persisting here.
}

void KVFloaterMinesweeper::draw()
{
    if (mTiming && !mGameOver)
    {
        mElapsedSeconds += gFrameIntervalSeconds;
        updateTimerText();
    }
    LLFloater::draw();
}

S32 KVFloaterMinesweeper::rowsForSize(BoardSize s) const
{
    switch (s)
    {
        case BoardSize::Small:  return 9;
        case BoardSize::Medium: return 16;
        case BoardSize::Large:  return 16;
    }
    return 9;
}

S32 KVFloaterMinesweeper::colsForSize(BoardSize s) const
{
    switch (s)
    {
        case BoardSize::Small:  return 9;
        case BoardSize::Medium: return 16;
        case BoardSize::Large:  return 30;
    }
    return 9;
}

F32 KVFloaterMinesweeper::densityForDifficulty(Difficulty d) const
{
    switch (d)
    {
        case Difficulty::Easy:   return 0.10f;
        case Difficulty::Medium: return 0.15f;
        case Difficulty::Hard:   return 0.20f;
    }
    return 0.10f;
}

std::string KVFloaterMinesweeper::sizeLabel(BoardSize s) const
{
    switch (s)
    {
        case BoardSize::Small:  return "Small";
        case BoardSize::Medium: return "Medium";
        case BoardSize::Large:  return "Large";
    }
    return "Small";
}

std::string KVFloaterMinesweeper::difficultyLabel(Difficulty d) const
{
    switch (d)
    {
        case Difficulty::Easy:   return "Easy";
        case Difficulty::Medium: return "Medium";
        case Difficulty::Hard:   return "Hard";
    }
    return "Easy";
}

LLColor4 KVFloaterMinesweeper::numberColor(S32 n) const
{
    // S24: the classic Windows Minesweeper number palette.
    switch (n)
    {
        case 1: return LLColor4(0.1f, 0.1f, 0.9f, 1.0f);   // blue
        case 2: return LLColor4(0.0f, 0.5f, 0.0f, 1.0f);   // green
        case 3: return LLColor4(0.85f, 0.1f, 0.1f, 1.0f);  // red
        case 4: return LLColor4(0.0f, 0.0f, 0.5f, 1.0f);   // navy
        case 5: return LLColor4(0.5f, 0.0f, 0.0f, 1.0f);   // maroon
        case 6: return LLColor4(0.0f, 0.5f, 0.5f, 1.0f);   // teal
        case 7: return LLColor4(0.05f, 0.05f, 0.05f, 1.0f); // black
        default: return LLColor4(0.5f, 0.5f, 0.5f, 1.0f);  // gray (8)
    }
}

void KVFloaterMinesweeper::onSizeClicked()
{
    switch (mSize)
    {
        case BoardSize::Small:  mSize = BoardSize::Medium; break;
        case BoardSize::Medium: mSize = BoardSize::Large;  break;
        case BoardSize::Large:  mSize = BoardSize::Small;  break;
    }
    if (mSizeBtn)
    {
        mSizeBtn->setLabel(LLStringExplicit("Size: " + sizeLabel(mSize)));
    }
    updateBestText();
}

void KVFloaterMinesweeper::onDifficultyClicked()
{
    switch (mDifficulty)
    {
        case Difficulty::Easy:   mDifficulty = Difficulty::Medium; break;
        case Difficulty::Medium: mDifficulty = Difficulty::Hard;   break;
        case Difficulty::Hard:   mDifficulty = Difficulty::Easy;   break;
    }
    if (mDifficultyBtn)
    {
        mDifficultyBtn->setLabel(LLStringExplicit("Difficulty: " + difficultyLabel(mDifficulty)));
    }
    updateBestText();
}

void KVFloaterMinesweeper::onNewGameClicked()
{
    newGame();
}

void KVFloaterMinesweeper::newGame()
{
    rebuildGrid();
    setStatus("Left-click to reveal, right-click to flag. First click is always safe.");
    updateMinesRemainingText();
    updateTimerText();
    updateBestText();
}

void KVFloaterMinesweeper::rebuildGrid()
{
    mRows = rowsForSize(mSize);
    mCols = colsForSize(mSize);
    S32 totalCells = mRows * mCols;
    mMineCount = llclamp((S32)llround(totalCells * densityForDifficulty(mDifficulty)), 1, totalCells - 9);

    mGrid.assign(totalCells, Cell());
    mFlagCount = 0;
    mRevealedCount = 0;
    mExplodedIdx = -1;
    mMinesPlaced = false;
    mGameOver = false;
    mTiming = false;
    mElapsedSeconds = 0.0f;

    if (mGridPanel)
    {
        mGridPanel->deleteAllChildren();
    }
    mCellButtons.assign(totalCells, nullptr);

    S32 gridWidth = mCols * CELL;
    S32 gridHeight = mRows * CELL;
    S32 floaterWidth = llmax(gridWidth + 2 * GRID_LEFT, CONTROL_MIN_WIDTH);
    S32 floaterHeight = GRID_TOP_OFFSET + gridHeight + BOTTOM_MARGIN;

    // S24: resize by editing the rect directly rather than calling
    // reshape() - reshape() anchors BOTTOM-left and grows the rect
    // up/right, which would walk the title bar up the screen every time
    // the board grows (Small -> Large). setShape() (the same call the
    // floater's own resize handles use for a live drag-resize, see
    // llresizehandle.cpp/llresizebar.cpp) does the full reshape+reposition
    // dance correctly in one proven step - a raw setRect() with manually
    // adjusted mRight/mBottom skipped whatever housekeeping that path does
    // and left the control-bar buttons/text visually corrupted after a
    // resize ("obliterated"). None of this floater's children use
    // follows="right" (every control-bar element is left-anchored, see the
    // XUI), so there's no follows-based repositioning to worry about either
    // way - setShape() just needed to be the one doing the resizing.
    LLRect cur = getRect();
    LLRect target(cur.mLeft, cur.mTop, cur.mLeft + floaterWidth, cur.mTop - floaterHeight);
    setShape(target, false);

    if (mGridPanel)
    {
        LLRect panelRect(GRID_LEFT, floaterHeight - GRID_TOP_OFFSET,
                          GRID_LEFT + gridWidth, floaterHeight - GRID_TOP_OFFSET - gridHeight);
        mGridPanel->setShape(panelRect, false);

        for (S32 row = 0; row < mRows; ++row)
        {
            for (S32 col = 0; col < mCols; ++col)
            {
                S32 localTop = gridHeight - row * CELL;
                S32 localBottom = localTop - CELL;
                S32 localLeft = col * CELL;
                S32 localRight = localLeft + CELL;

                LLButton::Params btn_p;
                btn_p.name(llformat("cell_%d_%d", row, col));
                btn_p.rect(LLRect(localLeft, localTop, localRight, localBottom));
                btn_p.image_unselected(LLUI::getUIImage(kCovered));
                btn_p.image_selected(LLUI::getUIImage(kCovered));
                btn_p.scale_image(true);
                btn_p.handle_right_mouse(true);
                btn_p.tab_stop(false);

                LLButton* btn = LLUICtrlFactory::create<LLButton>(btn_p);
                btn->setClickedCallback(boost::bind(&KVFloaterMinesweeper::onCellClicked, this, row, col));
                btn->setRightMouseDownCallback(boost::bind(&KVFloaterMinesweeper::onCellRightClicked, this, row, col));
                mGridPanel->addChild(btn);
                mCellButtons[row * mCols + col] = btn;
            }
        }
    }

    renderAll();
}

void KVFloaterMinesweeper::placeMines(S32 safeRow, S32 safeCol)
{
    S32 total = mRows * mCols;
    S32 safeIdx = safeRow * mCols + safeCol;

    std::vector<S32> candidates;
    candidates.reserve(total - 1);
    for (S32 i = 0; i < total; ++i)
    {
        if (i != safeIdx)
        {
            candidates.push_back(i);
        }
    }

    S32 toPlace = llmin(mMineCount, (S32)candidates.size());
    for (S32 i = 0; i < toPlace; ++i)
    {
        S32 j = i + ll_rand((S32)candidates.size() - i);
        std::swap(candidates[i], candidates[j]);
        mGrid[candidates[i]].isMine = true;
    }

    computeAdjacents();
    mMinesPlaced = true;
}

void KVFloaterMinesweeper::computeAdjacents()
{
    for (S32 r = 0; r < mRows; ++r)
    {
        for (S32 c = 0; c < mCols; ++c)
        {
            Cell& cell = mGrid[r * mCols + c];
            if (cell.isMine)
            {
                continue;
            }
            S32 count = 0;
            for (S32 dr = -1; dr <= 1; ++dr)
            {
                for (S32 dc = -1; dc <= 1; ++dc)
                {
                    if (dr == 0 && dc == 0) continue;
                    S32 nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= mRows || nc < 0 || nc >= mCols) continue;
                    if (mGrid[nr * mCols + nc].isMine) ++count;
                }
            }
            cell.adjacent = count;
        }
    }
}

void KVFloaterMinesweeper::onCellClicked(S32 row, S32 col)
{
    if (mGameOver)
    {
        return;
    }

    Cell& cell = mGrid[row * mCols + col];
    if (cell.flagged || cell.revealed)
    {
        return;
    }

    if (!mMinesPlaced)
    {
        placeMines(row, col);
        mTiming = true;
        mElapsedSeconds = 0.0f;
    }

    if (cell.isMine)
    {
        cell.revealed = true;
        mGameOver = true;
        mTiming = false;
        mExplodedIdx = row * mCols + col;
        revealAllMines();
        setStatus("Boom! Game over - New Game to try again.");
        playSound("UISndInvalidOp");
        renderAll();
        return;
    }

    floodReveal(row, col);
    playSound("UISndClick");

    if (checkWinCondition())
    {
        mGameOver = true;
        mTiming = false;
        setStatus(llformat("You win! Time: %ds", (S32)mElapsedSeconds));
        maybeSaveBestTime();
        playSound("UISndMoneyChangeUp");
    }

    renderAll();
    updateMinesRemainingText();
}

void KVFloaterMinesweeper::onCellRightClicked(S32 row, S32 col)
{
    if (mGameOver)
    {
        return;
    }

    Cell& cell = mGrid[row * mCols + col];
    if (cell.revealed)
    {
        return;
    }

    cell.flagged = !cell.flagged;
    mFlagCount += cell.flagged ? 1 : -1;
    playSound("UISndClick");
    renderCell(row, col);
    updateMinesRemainingText();
}

void KVFloaterMinesweeper::floodReveal(S32 row, S32 col)
{
    std::vector<S32> stack;
    stack.push_back(row * mCols + col);

    while (!stack.empty())
    {
        S32 idx = stack.back();
        stack.pop_back();

        Cell& cell = mGrid[idx];
        if (cell.revealed || cell.flagged || cell.isMine)
        {
            continue;
        }
        cell.revealed = true;
        ++mRevealedCount;

        if (cell.adjacent == 0)
        {
            S32 r = idx / mCols;
            S32 c = idx % mCols;
            for (S32 dr = -1; dr <= 1; ++dr)
            {
                for (S32 dc = -1; dc <= 1; ++dc)
                {
                    if (dr == 0 && dc == 0) continue;
                    S32 nr = r + dr, nc = c + dc;
                    if (nr < 0 || nr >= mRows || nc < 0 || nc >= mCols) continue;
                    S32 nidx = nr * mCols + nc;
                    const Cell& ncell = mGrid[nidx];
                    if (!ncell.revealed && !ncell.flagged && !ncell.isMine)
                    {
                        stack.push_back(nidx);
                    }
                }
            }
        }
    }
}

void KVFloaterMinesweeper::revealAllMines()
{
    for (Cell& cell : mGrid)
    {
        if (cell.isMine)
        {
            cell.revealed = true;
        }
    }
}

bool KVFloaterMinesweeper::checkWinCondition() const
{
    return mRevealedCount == mRows * mCols - mMineCount;
}

void KVFloaterMinesweeper::maybeSaveBestTime()
{
    std::string key = bestTimeKey();
    std::map<std::string, F32>::iterator it = mBestTimes.find(key);
    if (it == mBestTimes.end() || mElapsedSeconds < it->second)
    {
        mBestTimes[key] = mElapsedSeconds;
        saveBestTimes();
        updateBestText();
    }
}

void KVFloaterMinesweeper::renderCell(S32 row, S32 col)
{
    S32 idx = row * mCols + col;
    LLButton* btn = mCellButtons[idx];
    if (!btn)
    {
        return;
    }
    const Cell& cell = mGrid[idx];

    if (!cell.revealed)
    {
        btn->setImages(kCovered, kCovered);
        btn->setColor(LLColor4::white);
        btn->setImageOverlay(cell.flagged ? std::string(kFlag) : std::string());
        btn->setLabel(LLStringExplicit(""));
        return;
    }

    btn->setImages(kRevealed, kRevealed);
    btn->setColor(idx == mExplodedIdx ? kExplodedTint : LLColor4::white);

    if (cell.isMine)
    {
        btn->setImageOverlay(std::string(kMine));
        btn->setLabel(LLStringExplicit(""));
    }
    else if (cell.adjacent > 0)
    {
        btn->setImageOverlay(std::string());
        btn->setLabel(LLStringExplicit(llformat("%d", cell.adjacent)));
        btn->setUnselectedLabelColor(numberColor(cell.adjacent));
    }
    else
    {
        btn->setImageOverlay(std::string());
        btn->setLabel(LLStringExplicit(""));
    }
}

void KVFloaterMinesweeper::renderAll()
{
    for (S32 row = 0; row < mRows; ++row)
    {
        for (S32 col = 0; col < mCols; ++col)
        {
            renderCell(row, col);
        }
    }
}

void KVFloaterMinesweeper::updateMinesRemainingText()
{
    if (mMinesText)
    {
        mMinesText->setText(LLStringExplicit(llformat("Mines: %d", mMineCount - mFlagCount)));
    }
}

void KVFloaterMinesweeper::updateTimerText()
{
    if (mTimerText)
    {
        mTimerText->setText(LLStringExplicit(llformat("Time: %ds", (S32)mElapsedSeconds)));
    }
}

void KVFloaterMinesweeper::updateBestText()
{
    if (!mBestText)
    {
        return;
    }
    std::map<std::string, F32>::const_iterator it = mBestTimes.find(bestTimeKey());
    if (it != mBestTimes.end())
    {
        mBestText->setText(LLStringExplicit(llformat("Best: %ds", (S32)it->second)));
    }
    else
    {
        mBestText->setText(LLStringExplicit("Best: --"));
    }
}

void KVFloaterMinesweeper::setStatus(const std::string& text)
{
    if (mStatusText)
    {
        mStatusText->setText(LLStringExplicit(text));
    }
}

void KVFloaterMinesweeper::playSound(const char* settingsName)
{
    if (!gAudiop)
    {
        return;
    }
    LLUUID sound_id(gSavedSettings.getString(settingsName));
    if (sound_id.notNull())
    {
        gAudiop->triggerSound(sound_id, gAgent.getID(), SOUND_GAIN, LLAudioEngine::AUDIO_TYPE_UI);
    }
}

std::string KVFloaterMinesweeper::bestTimeKey() const
{
    std::string key = sizeLabel(mSize) + "_" + difficultyLabel(mDifficulty);
    LLStringUtil::toLower(key);
    return key;
}

void KVFloaterMinesweeper::loadBestTimes()
{
    mBestTimes.clear();

    std::string path = minesweeperBestPath();
    llifstream in(path.c_str());
    if (!in.is_open())
    {
        return;
    }

    LLSD root;
    LLSDSerialize::fromXML(root, in);
    if (!root.isMap())
    {
        return;
    }
    for (LLSD::map_const_iterator it = root.beginMap(); it != root.endMap(); ++it)
    {
        mBestTimes[it->first] = (F32)it->second.asReal();
    }
}

void KVFloaterMinesweeper::saveBestTimes()
{
    LLSD root;
    for (std::map<std::string, F32>::const_iterator it = mBestTimes.begin(); it != mBestTimes.end(); ++it)
    {
        root[it->first] = it->second;
    }

    std::string path = minesweeperBestPath();
    llofstream out(path.c_str());
    if (!out.good())
    {
        LL_WARNS() << "KV Minesweeper: could not save best times to " << path << LL_ENDL;
        return;
    }
    LLSDSerialize::toXML(root, out);
}
