/**
 * @file kvreversi.cpp
 * @brief KV Reversi - standalone Othello/Reversi floater implementation.
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

#include "kvreversi.h"

#include "llagent.h"
#include "llappviewer.h" // gFrameIntervalSeconds
#include "llaudioengine.h"
#include "llbutton.h"
#include "lldir.h"
#include "llrand.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "lltimer.h"
#include "llviewercontrol.h"

namespace
{
    std::string reversiStatsPath()
    {
        // S24: global to the viewer install, not per-SL-account - same
        // reasoning as KV Blackjack's best-score file.
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "kv_reversi_stats.xml");
    }

    const S32 kDirs[8][2] =
    {
        { -1, -1 }, { -1, 0 }, { -1, 1 },
        {  0, -1},             {  0, 1 },
        {  1, -1 }, {  1, 0 }, {  1, 1 }
    };

    const char* kBlackDisc = "reversi/black_disc.png";
    const char* kWhiteDisc = "reversi/white_disc.png";

    const F32 SOUND_GAIN = 1.0f;
    const F32 kFlipStepSeconds = 0.08f; // cascade speed - cheap but visibly staggered
    const F32 kAiThinkMinSeconds = 0.5f;
    const F32 kAiThinkMaxExtraSeconds = 0.4f; // total think time: 0.5-0.9s
}

KVFloaterReversi::KVFloaterReversi(const LLSD& key)
    : LLFloater(key)
{
}

KVFloaterReversi::~KVFloaterReversi()
{
}

bool KVFloaterReversi::postBuild()
{
    mStatusText = getChild<LLTextBox>("status_text");
    mScoreText = getChild<LLTextBox>("score_text");
    mStatsText = getChild<LLTextBox>("stats_text");

    mNewGameBtn = getChild<LLButton>("new_game_btn");
    mNewGameBtn->setClickedCallback(boost::bind(&KVFloaterReversi::onNewGameClicked, this));

    mDifficultyBtn = getChild<LLButton>("difficulty_btn");
    mDifficultyBtn->setClickedCallback(boost::bind(&KVFloaterReversi::onDifficultyClicked, this));

    mResetScoresBtn = getChild<LLButton>("reset_scores_btn");
    mResetScoresBtn->setClickedCallback(boost::bind(&KVFloaterReversi::onResetScoresClicked, this));

    for (S32 r = 0; r < N; ++r)
    {
        for (S32 c = 0; c < N; ++c)
        {
            LLButton* btn = getChild<LLButton>(llformat("cell_%d_%d", r, c));
            mCells[r][c] = btn;
            // S24: bound by value, not by reference - r/c are loop locals,
            // long gone by the time a click ever fires.
            btn->setClickedCallback(boost::bind(&KVFloaterReversi::onCellClicked, this, r, c));
        }
    }

    loadStats();
    updateStatsText();
    newGame();

    return LLFloater::postBuild();
}

void KVFloaterReversi::onClose(bool app_quitting)
{
    // Defensive save - saveStats() already runs immediately at every game
    // end (see checkForGameEnd()), this just catches any future code path
    // that changes the tally some other way. Same belt-and-suspenders
    // discipline as KV Blackjack's best score.
    saveStats();
}

void KVFloaterReversi::draw()
{
    if (mAnimPhase == AnimPhase::Flipping)
    {
        mFlipStepRemaining -= gFrameIntervalSeconds;
        if (mFlipStepRemaining <= 0.0f)
        {
            if (!mFlipQueue.empty())
            {
                Cell cell = mFlipQueue.front();
                mFlipQueue.erase(mFlipQueue.begin());
                mBoard[cell.first][cell.second] = mMoverColor;
                renderCell(cell.first, cell.second);
                playTickSound();
                mFlipStepRemaining = kFlipStepSeconds;
            }
            if (mFlipQueue.empty())
            {
                mAnimPhase = AnimPhase::None;
                onMoveAnimationComplete();
            }
        }
    }
    else if (mAiThinking)
    {
        mAiThinkRemaining -= gFrameIntervalSeconds;
        if (mAiThinkRemaining <= 0.0f)
        {
            mAiThinking = false;
            performAiMove();
        }
    }

    LLFloater::draw();
}

void KVFloaterReversi::newGame()
{
    for (S32 r = 0; r < N; ++r)
    {
        for (S32 c = 0; c < N; ++c)
        {
            mBoard[r][c] = EMPTY;
        }
    }
    // Standard Othello starting position.
    mBoard[3][3] = WHITE;
    mBoard[3][4] = BLACK;
    mBoard[4][3] = BLACK;
    mBoard[4][4] = WHITE;

    // S24: fair colour assignment - randomized fresh every game, not always
    // handing the human the first-move seat. Black always moves first
    // regardless of who holds it, same as the real rules; this just decides
    // who that is each time, and says so clearly.
    mHumanColor = (ll_rand(2) == 0) ? BLACK : WHITE;
    mCurrentPlayer = BLACK;
    mGameOver = false;
    mAnimPhase = AnimPhase::None;
    mFlipQueue.clear();
    mAiThinking = false;

    renderBoard();
    updateStatusAndScore();

    std::string you_are = llformat("You are playing %s.", colorName(mHumanColor).c_str());

    if (mCurrentPlayer == mHumanColor)
    {
        playerTurnBegin();
        setStatus(you_are + " You move first.");
    }
    else
    {
        setStatus(you_are + " " + colorName(aiColor()) + " moves first and is thinking...");
        beginAiThinking();
    }
}

bool KVFloaterReversi::isOnBoard(S32 r, S32 c) const
{
    return r >= 0 && r < N && c >= 0 && c < N;
}

std::vector<KVFloaterReversi::Cell> KVFloaterReversi::getFlips(S32 r, S32 c, S32 player) const
{
    std::vector<Cell> flips;
    if (!isOnBoard(r, c) || mBoard[r][c] != EMPTY)
    {
        return flips;
    }
    S32 opponent = (player == BLACK) ? WHITE : BLACK;

    for (const auto& d : kDirs)
    {
        std::vector<Cell> line;
        S32 rr = r + d[0];
        S32 cc = c + d[1];
        while (isOnBoard(rr, cc) && mBoard[rr][cc] == opponent)
        {
            line.emplace_back(rr, cc);
            rr += d[0];
            cc += d[1];
        }
        // A valid flanking line must end on the player's OWN disc, and must
        // have at least one opponent disc in between - an immediate own-disc
        // neighbour (empty `line`) flanks nothing.
        if (!line.empty() && isOnBoard(rr, cc) && mBoard[rr][cc] == player)
        {
            flips.insert(flips.end(), line.begin(), line.end());
        }
    }
    return flips;
}

std::vector<KVFloaterReversi::Cell> KVFloaterReversi::getLegalMoves(S32 player) const
{
    std::vector<Cell> moves;
    for (S32 r = 0; r < N; ++r)
    {
        for (S32 c = 0; c < N; ++c)
        {
            if (!getFlips(r, c, player).empty())
            {
                moves.emplace_back(r, c);
            }
        }
    }
    return moves;
}

S32 KVFloaterReversi::countDiscs(S32 player) const
{
    S32 total = 0;
    for (S32 r = 0; r < N; ++r)
    {
        for (S32 c = 0; c < N; ++c)
        {
            if (mBoard[r][c] == player)
            {
                ++total;
            }
        }
    }
    return total;
}

S32 KVFloaterReversi::scoreMoveForAi(S32 r, S32 c, S32 flipCount) const
{
    // S24: a light positional heuristic, not a search - corners are
    // permanent once taken (never flippable again), so they dominate the
    // score. The diagonal-adjacent "X-square" is the classic beginner
    // mistake that hands the opponent a free corner next turn, so it's
    // penalized specifically (not just "any square near a corner" - the
    // orthogonally-adjacent "C-squares" are far less dangerous and aren't
    // penalized here, keeping this a simple, explainable rule rather than
    // a full 8x8 weight table).
    bool is_corner = (r == 0 || r == N - 1) && (c == 0 || c == N - 1);
    bool is_x_square = (r == 1 || r == N - 2) && (c == 1 || c == N - 2);
    bool is_edge = (r == 0 || r == N - 1 || c == 0 || c == N - 1);

    S32 score = flipCount;
    if (is_corner) score += 20;
    else if (is_x_square) score -= 15;
    else if (is_edge) score += 3;
    return score;
}

KVFloaterReversi::Cell KVFloaterReversi::chooseAiMove(const std::vector<Cell>& legal) const
{
    // Easy: a uniformly random legal move - genuinely beatable, no strategy.
    if (mDifficulty == Difficulty::Easy || legal.size() == 1)
    {
        return legal[ll_rand((S32)legal.size())];
    }

    // Medium and Hard both pick the highest-scoring move, ties broken
    // randomly so the AI isn't eerily deterministic. Medium's score is
    // pure flip-count (greedy); Hard adds the corner/X-square/edge weights.
    S32 ai_color = aiColor();
    S32 best_score = -1000;
    std::vector<Cell> best;
    for (const Cell& mv : legal)
    {
        std::vector<Cell> flips = getFlips(mv.first, mv.second, ai_color);
        S32 score = (mDifficulty == Difficulty::Hard)
            ? scoreMoveForAi(mv.first, mv.second, (S32)flips.size())
            : (S32)flips.size();

        if (score > best_score)
        {
            best_score = score;
            best.clear();
            best.push_back(mv);
        }
        else if (score == best_score)
        {
            best.push_back(mv);
        }
    }
    return best[ll_rand((S32)best.size())];
}

void KVFloaterReversi::onCellClicked(S32 r, S32 c)
{
    if (mGameOver || mAnimPhase != AnimPhase::None || mAiThinking || mCurrentPlayer != mHumanColor)
    {
        return;
    }
    if (getFlips(r, c, mHumanColor).empty())
    {
        return; // not a legal move - silently ignored, same as clicking an occupied cell
    }

    startMoveAnimation(r, c, mHumanColor);
}

void KVFloaterReversi::startMoveAnimation(S32 r, S32 c, S32 player)
{
    std::vector<Cell> flips = getFlips(r, c, player);

    mBoard[r][c] = player;
    renderCell(r, c); // the anchor disc itself appears instantly, no cascade
    playTickSound();

    mMoverColor = player;
    mFlipQueue = flips;

    // Hints are only ever shown on the human's own turn (see renderBoard()),
    // and a move just happened, so clear them immediately rather than
    // waiting for the next full render.
    renderBoard();

    if (mFlipQueue.empty())
    {
        onMoveAnimationComplete();
    }
    else
    {
        mAnimPhase = AnimPhase::Flipping;
        mFlipStepRemaining = kFlipStepSeconds;
    }
}

void KVFloaterReversi::onMoveAnimationComplete()
{
    updateStatusAndScore();

    if (mMoverColor == mHumanColor)
    {
        std::vector<Cell> ai_moves = getLegalMoves(aiColor());
        if (ai_moves.empty())
        {
            setStatus(colorName(aiColor()) + " has no legal moves - passing back to you.");
            playerTurnBegin();
        }
        else
        {
            mCurrentPlayer = aiColor();
            setStatus(colorName(aiColor()) + " is thinking...");
            beginAiThinking();
        }
    }
    else
    {
        playerTurnBegin();
    }
}

void KVFloaterReversi::beginAiThinking()
{
    mAiThinking = true;
    mAiThinkRemaining = kAiThinkMinSeconds + (F32)ll_rand(1000) / 1000.0f * kAiThinkMaxExtraSeconds;
}

void KVFloaterReversi::performAiMove()
{
    std::vector<Cell> legal = getLegalMoves(aiColor());
    if (legal.empty())
    {
        // Shouldn't be reachable (checked before the thinking delay started),
        // but stay safe rather than animate a move that doesn't exist.
        playerTurnBegin();
        return;
    }
    Cell mv = chooseAiMove(legal);
    startMoveAnimation(mv.first, mv.second, aiColor());
}

void KVFloaterReversi::playerTurnBegin()
{
    mPlayerLegalMoves = getLegalMoves(mHumanColor);
    std::vector<Cell> ai_moves = getLegalMoves(aiColor());

    if (mPlayerLegalMoves.empty() && ai_moves.empty())
    {
        renderBoard();
        checkForGameEnd();
        return;
    }

    if (mPlayerLegalMoves.empty())
    {
        renderBoard();
        setStatus(colorName(mHumanColor) + " (you) has no legal moves - passing.");
        mCurrentPlayer = aiColor();
        beginAiThinking();
        return;
    }

    mCurrentPlayer = mHumanColor;
    renderBoard();
    updateStatusAndScore();
}

void KVFloaterReversi::checkForGameEnd()
{
    mGameOver = true;
    S32 human_discs = countDiscs(mHumanColor);
    S32 ai_discs = countDiscs(aiColor());

    std::string result;
    const char* sound = nullptr;
    if (human_discs > ai_discs)
    {
        result = llformat("You win, %d to %d!", human_discs, ai_discs);
        ++mWins;
        sound = "UISndMoneyChangeUp";
    }
    else if (ai_discs > human_discs)
    {
        result = llformat("%s wins, %d to %d.", colorName(aiColor()).c_str(), ai_discs, human_discs);
        ++mLosses;
        sound = "UISndInvalidOp";
    }
    else
    {
        result = llformat("Draw, %d to %d.", human_discs, ai_discs);
        ++mDraws;
    }

    setStatus(result + "  (New Game to play again.)");
    updateStatsText();
    saveStats();

    if (sound)
    {
        playSound(sound);
    }
}

void KVFloaterReversi::onNewGameClicked()
{
    newGame();
}

void KVFloaterReversi::onResetScoresClicked()
{
    // S24: in-progress game is untouched - this only clears the persisted
    // Wins/Losses/Draws tally, not the board. Saved immediately, same
    // "don't rely on close to flush it" discipline as every other stat
    // write in this floater.
    mWins = 0;
    mLosses = 0;
    mDraws = 0;
    updateStatsText();
    saveStats();
}

void KVFloaterReversi::onDifficultyClicked()
{
    // S24: cycle, same "click to advance" pattern as a lot of small S24
    // toggles - avoids needing three radio buttons for a three-way choice
    // that only matters before a game starts anyway.
    switch (mDifficulty)
    {
        case Difficulty::Easy:   mDifficulty = Difficulty::Medium; break;
        case Difficulty::Medium: mDifficulty = Difficulty::Hard;   break;
        case Difficulty::Hard:   mDifficulty = Difficulty::Easy;   break;
    }
    if (mDifficultyBtn)
    {
        const char* label = (mDifficulty == Difficulty::Easy) ? "Difficulty: Easy"
                           : (mDifficulty == Difficulty::Medium) ? "Difficulty: Medium"
                           : "Difficulty: Hard";
        mDifficultyBtn->setLabel(LLStringExplicit(label));
    }
}

void KVFloaterReversi::renderCell(S32 r, S32 c)
{
    LLButton* btn = mCells[r][c];
    if (!btn) return;

    if (mBoard[r][c] == BLACK)
    {
        btn->setImageOverlay(kBlackDisc);
    }
    else if (mBoard[r][c] == WHITE)
    {
        btn->setImageOverlay(kWhiteDisc);
    }
    else
    {
        btn->setImageOverlay(std::string());
    }
}

void KVFloaterReversi::renderBoard()
{
    for (S32 r = 0; r < N; ++r)
    {
        for (S32 c = 0; c < N; ++c)
        {
            renderCell(r, c);
        }
    }

    // Legal-move highlight: only meaningful on the human's own turn, and
    // never mid-animation or mid-AI-think - a lighter tint on the plain
    // green cell image, reset everywhere else.
    bool show_hints = !mGameOver && mAnimPhase == AnimPhase::None && !mAiThinking && mCurrentPlayer == mHumanColor;
    for (S32 r = 0; r < N; ++r)
    {
        for (S32 c = 0; c < N; ++c)
        {
            LLButton* btn = mCells[r][c];
            if (!btn) continue;
            btn->setColor(LLColor4::white);
        }
    }
    if (show_hints)
    {
        for (const Cell& mv : mPlayerLegalMoves)
        {
            LLButton* btn = mCells[mv.first][mv.second];
            if (btn)
            {
                btn->setColor(LLColor4(0.65f, 1.0f, 0.65f, 1.0f));
            }
        }
    }
}

void KVFloaterReversi::updateStatusAndScore()
{
    if (mScoreText)
    {
        mScoreText->setText(LLStringExplicit(llformat("%s (you): %d   %s: %d",
            colorName(mHumanColor).c_str(), countDiscs(mHumanColor),
            colorName(aiColor()).c_str(), countDiscs(aiColor()))));
    }
    if (mStatusText && !mGameOver)
    {
        setStatus(mCurrentPlayer == mHumanColor ? "Your move." : (colorName(aiColor()) + " is thinking..."));
    }
}

void KVFloaterReversi::updateStatsText()
{
    if (mStatsText)
    {
        mStatsText->setText(LLStringExplicit(
            llformat("Wins: %d   Losses: %d   Draws: %d", mWins, mLosses, mDraws)));
    }
}

void KVFloaterReversi::setStatus(const std::string& text)
{
    if (mStatusText)
    {
        mStatusText->setText(LLStringExplicit(text));
    }
}

void KVFloaterReversi::playTickSound()
{
    playSound("UISndClick");
}

void KVFloaterReversi::playSound(const char* settingsName)
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

void KVFloaterReversi::loadStats()
{
    mWins = 0;
    mLosses = 0;
    mDraws = 0;

    std::string path = reversiStatsPath();
    llifstream in(path.c_str());
    if (!in.is_open())
    {
        return;
    }

    LLSD root;
    LLSDSerialize::fromXML(root, in);
    if (root.isMap())
    {
        mWins = root["wins"].asInteger();
        mLosses = root["losses"].asInteger();
        mDraws = root["draws"].asInteger();
    }
}

void KVFloaterReversi::saveStats()
{
    LLSD root;
    root["wins"] = mWins;
    root["losses"] = mLosses;
    root["draws"] = mDraws;

    std::string path = reversiStatsPath();
    llofstream out(path.c_str());
    if (!out.good())
    {
        LL_WARNS() << "KV Reversi: could not save stats to " << path << LL_ENDL;
        return;
    }
    LLSDSerialize::toXML(root, out);
}
