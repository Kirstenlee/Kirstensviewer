/**
 * @file kvreversi.h
 * @brief KV Reversi - standalone Othello/Reversi floater, self-contained game
 * system fired from the S24 console ("play reversi" / "reversi" / "othello").
 * Procedural board/disc textures (newview/skins/default/textures/reversi/),
 * 3 AI difficulty levels, a randomly-assigned player colour each game, a fake
 * "thinking" delay and a cascading flip-reveal for some real interactivity,
 * win/loss/draw stats persisted across sessions.
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

#ifndef LL_KVREVERSI_H
#define LL_KVREVERSI_H

#include "llfloater.h"
#include <vector>
#include <utility>
#include <string>

class LLButton;
class LLTextBox;

class KVFloaterReversi : public LLFloater
{
public:
    KVFloaterReversi(const LLSD& key);
    ~KVFloaterReversi();

    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void draw() override;

private:
    static const S32 N = 8;

    enum Piece { EMPTY = 0, BLACK = 1, WHITE = 2 }; // board values - Black always moves first, per the real rules
    enum class Difficulty { Easy, Medium, Hard };
    enum class AnimPhase { None, Flipping };

    typedef std::pair<S32, S32> Cell;

    void newGame();
    void onCellClicked(S32 r, S32 c);
    void onNewGameClicked();
    void onDifficultyClicked();
    void onResetScoresClicked();

    // Core rules - direction-agnostic flood along all 8 rays from (r,c).
    std::vector<Cell> getFlips(S32 r, S32 c, S32 player) const;
    std::vector<Cell> getLegalMoves(S32 player) const;
    bool isOnBoard(S32 r, S32 c) const;
    S32 countDiscs(S32 player) const;

    S32 aiColor() const { return (mHumanColor == BLACK) ? WHITE : BLACK; }
    std::string colorName(S32 player) const { return (player == BLACK) ? "Black" : "White"; }

    Cell chooseAiMove(const std::vector<Cell>& legal) const;
    S32 scoreMoveForAi(S32 r, S32 c, S32 flipCount) const;

    // S24: a move is never applied all at once - place the anchor disc
    // instantly, then queue the discs it flips for a staggered reveal
    // (ticked from draw(), see mFlipQueue/mFlipStepRemaining below) so a
    // turn reads as a small cascade rather than an instant colour swap.
    void startMoveAnimation(S32 r, S32 c, S32 player);
    void onMoveAnimationComplete();
    void beginAiThinking();
    void performAiMove();

    void playerTurnBegin();
    void checkForGameEnd();

    void renderBoard();
    void renderCell(S32 r, S32 c);
    void updateStatusAndScore();
    void updateStatsText();
    void setStatus(const std::string& text);
    void playTickSound();
    void playSound(const char* settingsName);

    void loadStats();
    void saveStats();

    S32 mBoard[N][N] = {};
    S32 mCurrentPlayer = BLACK;
    bool mGameOver = true;
    std::vector<Cell> mPlayerLegalMoves; // recomputed each time it becomes the human's turn

    S32 mHumanColor = BLACK; // randomized fresh each newGame() - see newGame()
    Difficulty mDifficulty = Difficulty::Medium;

    // S24: cascading flip-reveal state, ticked from draw() like every other
    // per-frame timer in this codebase's console/floater family (see
    // KVDebugConsole::draw()'s own mPendingScan.delay_remaining for the
    // identical idiom this mirrors).
    AnimPhase mAnimPhase = AnimPhase::None;
    std::vector<Cell> mFlipQueue;
    S32 mMoverColor = EMPTY; // whose move this animation belongs to
    F32 mFlipStepRemaining = 0.0f;

    // S24: fake "thinking" delay before the AI's move actually resolves -
    // without it the AI plays instantly, which reads as broken rather than
    // fast. Also ticked from draw().
    bool mAiThinking = false;
    F32 mAiThinkRemaining = 0.0f;

    S32 mWins = 0;
    S32 mLosses = 0;
    S32 mDraws = 0;

    LLTextBox* mStatusText = nullptr;
    LLTextBox* mScoreText = nullptr;
    LLTextBox* mStatsText = nullptr;
    LLButton* mNewGameBtn = nullptr;
    LLButton* mDifficultyBtn = nullptr;
    LLButton* mResetScoresBtn = nullptr;

    LLButton* mCells[N][N] = {};
};

#endif // LL_KVREVERSI_H
