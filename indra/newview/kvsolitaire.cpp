/**
 * @file kvsolitaire.cpp
 * @brief KV Solitaire - standalone Klondike floater implementation.
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

#include "kvsolitaire.h"

#include "llagent.h"
#include "llaudioengine.h"
#include "llbutton.h"
#include "lldir.h"
#include "llrand.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

namespace
{
    std::string solitaireStatsPath()
    {
        // S24: global to the viewer install, not per-SL-account - same
        // reasoning as KV Blackjack's best-score file and KV Reversi's
        // win/loss/draw record.
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "kv_solitaire_stats.xml");
    }

    const char* kCardBack = "cards/Back.png";
    const F32 SOUND_GAIN = 1.0f;
    const LLColor4 kHighlight(1.0f, 0.95f, 0.55f, 1.0f); // soft gold - "this is selected"
}

const F32 KVFloaterSolitaire::kDoubleClickSeconds = 0.4f;

std::string KVFloaterSolitaire::Card::iconName() const
{
    static const char* const kRanks[] =
    {
        "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
    };
    static const char* const kSuits[] = { "S", "H", "D", "C" };

    std::string name = "cards/";
    name += kRanks[rank];
    name += kSuits[suit];
    name += ".png";
    return name;
}

KVFloaterSolitaire::KVFloaterSolitaire(const LLSD& key)
    : LLFloater(key)
{
}

KVFloaterSolitaire::~KVFloaterSolitaire()
{
}

bool KVFloaterSolitaire::postBuild()
{
    mStatusText = getChild<LLTextBox>("status_text");
    mStatsText = getChild<LLTextBox>("stats_text");

    mNewGameBtn = getChild<LLButton>("new_game_btn");
    mNewGameBtn->setClickedCallback(boost::bind(&KVFloaterSolitaire::onNewGameClicked, this));

    mStockBtn = getChild<LLButton>("stock_btn");
    mStockBtn->setClickedCallback(boost::bind(&KVFloaterSolitaire::onStockClicked, this));

    mWasteBtn = getChild<LLButton>("waste_btn");
    mWasteBtn->setClickedCallback(boost::bind(&KVFloaterSolitaire::onWasteClicked, this));

    for (S32 s = 0; s < 4; ++s)
    {
        mFoundationBtn[s] = getChild<LLButton>(llformat("foundation_%d", s));
        mFoundationBtn[s]->setClickedCallback(boost::bind(&KVFloaterSolitaire::onFoundationClicked, this, s));
    }

    for (S32 col = 0; col < NUM_COLS; ++col)
    {
        for (S32 row = 0; row < MAX_COL_SLOTS; ++row)
        {
            LLButton* btn = getChild<LLButton>(llformat("tab_%d_%d", col, row));
            mTableauSlots[col][row] = btn;
            // S24: each slot is bound with its OWN row baked in. Sibling
            // button rectangles overlap heavily in the fan (each is a full
            // card-height rect offset by only the reveal amount from the
            // next), so in any overlapping region the LATER row's button -
            // the one actually drawn on top there - wins the click, which
            // always matches what the player visually sees. That lets a
            // click on any exposed card correctly report its own row, not
            // just the column's absolute top.
            btn->setClickedCallback(boost::bind(&KVFloaterSolitaire::onTableauClicked, this, col, row));
        }
    }

    loadStats();
    newGame();

    return LLFloater::postBuild();
}

void KVFloaterSolitaire::onClose(bool app_quitting)
{
    // Defensive save - saveStats() already runs immediately at every game
    // end (see endGame()), this just catches any future code path that
    // changes the tally some other way. Same belt-and-suspenders discipline
    // as the other two games in this series.
    saveStats();
}

void KVFloaterSolitaire::buildAndShuffleDeck(std::vector<Card>& deck)
{
    deck.clear();
    deck.reserve(52);
    for (S32 suit = 0; suit < 4; ++suit)
    {
        for (S32 rank = 1; rank <= 13; ++rank)
        {
            deck.push_back(Card{ rank, suit, false });
        }
    }
    for (S32 i = (S32)deck.size() - 1; i > 0; --i)
    {
        S32 j = ll_rand(i + 1);
        std::swap(deck[i], deck[j]);
    }
}

void KVFloaterSolitaire::newGame()
{
    std::vector<Card> deck;
    buildAndShuffleDeck(deck);

    for (S32 s = 0; s < 4; ++s)
    {
        mFoundation[s].clear();
    }
    for (S32 col = 0; col < NUM_COLS; ++col)
    {
        mTableau[col].clear();
    }
    mWaste.clear();
    mStock.clear();

    S32 idx = 0;
    for (S32 col = 0; col < NUM_COLS; ++col)
    {
        for (S32 row = 0; row <= col; ++row)
        {
            Card c = deck[idx++];
            c.faceUp = (row == col); // only the last-dealt card in each column starts face-up
            mTableau[col].push_back(c);
        }
    }
    while (idx < (S32)deck.size())
    {
        Card c = deck[idx++];
        c.faceUp = false;
        mStock.push_back(c);
    }

    mSelectedType = SourceType::None;
    mSelectedCol = -1;
    mGameOver = false;
    mProgressMadeThisPass = true; // fresh deal - don't flag "no progress" on the very first recycle
    ++mGamesPlayed;

    renderAll();
    setStatus("New deal. Click a card to select it, or click Stock to draw.");
}

void KVFloaterSolitaire::onNewGameClicked()
{
    newGame();
}

void KVFloaterSolitaire::onStockClicked()
{
    if (mGameOver)
    {
        return;
    }
    clearSelection();

    if (!mStock.empty())
    {
        S32 n = llmin((S32)3, (S32)mStock.size());
        for (S32 i = 0; i < n; ++i)
        {
            Card c = mStock.back();
            mStock.pop_back();
            c.faceUp = true;
            mWaste.push_back(c);
        }
        playSound("UISndClick");
        renderAll();
        return;
    }

    if (!mWaste.empty())
    {
        if (!mProgressMadeThisPass)
        {
            // A full pass through stock+waste with zero tableau/foundation
            // progress - recycling again would just repeat the same cycle
            // forever. This is "no valid move left" even though technically
            // more draws are always possible.
            endGame(false);
            return;
        }
        // Recycle: waste goes back into stock in the order it will be
        // redealt in (nothing is reshuffled).
        mStock.assign(mWaste.rbegin(), mWaste.rend());
        for (Card& c : mStock)
        {
            c.faceUp = false;
        }
        mWaste.clear();
        mProgressMadeThisPass = false;
        playSound("UISndClick");
        renderAll();
        return;
    }

    // Both empty - nothing to draw or recycle. Shouldn't normally be
    // reachable (checkForStuck() would already have ended the game), but
    // check again defensively rather than leaving a dead click.
    checkForStuck();
}

bool KVFloaterSolitaire::isDoubleClick(S32 kind, S32 col, S32 row)
{
    bool is_double = (kind == mLastClickKind && col == mLastClickCol && row == mLastClickRow
                       && mClickTimer.getElapsedTimeF32() < kDoubleClickSeconds);
    mLastClickKind = kind;
    mLastClickCol = col;
    mLastClickRow = row;
    mClickTimer.reset();
    return is_double;
}

void KVFloaterSolitaire::autoSendToFoundation(const Card& c, SourceType src, S32 srcCol)
{
    for (S32 s = 0; s < 4; ++s)
    {
        if (canPlaceOnFoundation(c, s))
        {
            if (src == SourceType::Waste)
            {
                mWaste.pop_back();
            }
            else
            {
                mTableau[srcCol].pop_back();
                exposeNewTableauTop(srcCol);
            }
            mFoundation[s].push_back(c);
            clearSelection();
            onSuccessfulMove();
            return;
        }
    }
    // No legal foundation for this card right now - fall back to a normal
    // selection so the double-click isn't just a dead click.
    if (src == SourceType::Waste)
    {
        selectWasteTop();
    }
    else
    {
        selectTableauStack(srcCol, (S32)mTableau[srcCol].size() - 1);
    }
}

void KVFloaterSolitaire::onWasteClicked()
{
    if (mGameOver)
    {
        return;
    }
    if (isDoubleClick(0, -1, -1) && !mWaste.empty())
    {
        autoSendToFoundation(mWaste.back(), SourceType::Waste, -1);
        return;
    }
    if (mSelectedType == SourceType::Waste)
    {
        clearSelection();
        renderAll();
        return;
    }
    selectWasteTop();
}

void KVFloaterSolitaire::onTableauClicked(S32 col, S32 row)
{
    if (mGameOver)
    {
        return;
    }

    S32 count = (S32)mTableau[col].size();
    if (isDoubleClick(1, col, row) && row == count - 1 && count > 0 && mTableau[col].back().faceUp)
    {
        // Double-click only ever makes sense on the column's actual top
        // card - a multi-card stack can never go to a foundation anyway.
        autoSendToFoundation(mTableau[col].back(), SourceType::Tableau, col);
        return;
    }

    if (mSelectedType == SourceType::Tableau && mSelectedCol == col)
    {
        clearSelection();
        renderAll();
        return;
    }
    if (mSelectedType != SourceType::None)
    {
        tryMoveSelectedToTableau(col, row);
        return;
    }
    selectTableauStack(col, row);
}

void KVFloaterSolitaire::onFoundationClicked(S32 suit)
{
    if (mGameOver || mSelectedType == SourceType::None)
    {
        return; // foundations are never a move SOURCE in this ruleset
    }
    tryMoveSelectedToFoundation(suit);
}

void KVFloaterSolitaire::selectWasteTop()
{
    if (mWaste.empty())
    {
        return;
    }
    mSelectedType = SourceType::Waste;
    mSelectedCol = -1;
    mSelectedRow = -1;
    renderAll();
}

void KVFloaterSolitaire::selectTableauStack(S32 col, S32 row)
{
    if (row < 0 || row >= (S32)mTableau[col].size() || !mTableau[col][row].faceUp)
    {
        return;
    }
    mSelectedType = SourceType::Tableau;
    mSelectedCol = col;
    mSelectedRow = row;
    renderAll();
}

void KVFloaterSolitaire::clearSelection()
{
    mSelectedType = SourceType::None;
    mSelectedCol = -1;
    mSelectedRow = -1;
}

void KVFloaterSolitaire::tryMoveSelectedToTableau(S32 destCol, S32 clickedRow)
{
    if (mSelectedType == SourceType::Waste)
    {
        if (mWaste.empty() || !canPlaceOnTableau(mWaste.back(), destCol))
        {
            playSound("UISndInvalidOp");
            clearSelection();
            renderAll();
            return;
        }
        Card c = mWaste.back();
        mWaste.pop_back();
        mTableau[destCol].push_back(c);
        clearSelection();
        onSuccessfulMove();
        return;
    }

    if (mSelectedType == SourceType::Tableau)
    {
        S32 srcCol = mSelectedCol;
        S32 srcRow = mSelectedRow;
        if (srcCol != destCol && srcRow >= 0 && srcRow < (S32)mTableau[srcCol].size()
            && canPlaceOnTableau(mTableau[srcCol][srcRow], destCol))
        {
            moveTableauRun(srcCol, srcRow, destCol);
            return;
        }

        // S24: forward direction wasn't legal - the player may have simply
        // clicked the two cards in the opposite order (the intended
        // DESTINATION card first, the stack they actually want to move
        // second). Try it the other way before giving up: can the
        // just-clicked card's own run legally land on the originally
        // selected column instead? This is what makes splitting a
        // 2+ card run and relocating the top piece feel order-independent
        // rather than requiring "select the mover first" every time.
        if (destCol != srcCol && clickedRow >= 0 && clickedRow < (S32)mTableau[destCol].size()
            && mTableau[destCol][clickedRow].faceUp
            && canPlaceOnTableau(mTableau[destCol][clickedRow], srcCol))
        {
            moveTableauRun(destCol, clickedRow, srcCol);
            return;
        }

        playSound("UISndInvalidOp");
        clearSelection();
        renderAll();
    }
}

void KVFloaterSolitaire::moveTableauRun(S32 srcCol, S32 srcRow, S32 destCol)
{
    std::vector<Card> moving(mTableau[srcCol].begin() + srcRow, mTableau[srcCol].end());
    mTableau[srcCol].resize(srcRow);
    exposeNewTableauTop(srcCol);
    for (const Card& c : moving)
    {
        mTableau[destCol].push_back(c);
    }
    clearSelection();
    onSuccessfulMove();
}

void KVFloaterSolitaire::tryMoveSelectedToFoundation(S32 destSuit)
{
    Card c;
    bool haveSource = false;
    S32 srcCol = -1;

    if (mSelectedType == SourceType::Waste && !mWaste.empty())
    {
        c = mWaste.back();
        haveSource = true;
    }
    else if (mSelectedType == SourceType::Tableau && mSelectedCol >= 0)
    {
        // A foundation only ever takes a single card - only allow this
        // when the held "stack" is actually just the column's lone top
        // card (srcRow pointing at the last element), never a multi-card
        // run.
        S32 count = (S32)mTableau[mSelectedCol].size();
        if (mSelectedRow == count - 1 && count > 0)
        {
            c = mTableau[mSelectedCol].back();
            srcCol = mSelectedCol;
            haveSource = true;
        }
    }

    if (!haveSource || !canPlaceOnFoundation(c, destSuit))
    {
        playSound("UISndInvalidOp");
        clearSelection();
        renderAll();
        return;
    }

    if (mSelectedType == SourceType::Waste)
    {
        mWaste.pop_back();
    }
    else
    {
        mTableau[srcCol].pop_back();
        exposeNewTableauTop(srcCol);
    }
    mFoundation[destSuit].push_back(c);
    clearSelection();
    onSuccessfulMove();
}

bool KVFloaterSolitaire::canPlaceOnTableau(const Card& c, S32 col) const
{
    if (mTableau[col].empty())
    {
        return c.rank == 13; // only a King may start an empty column
    }
    const Card& top = mTableau[col].back();
    if (!top.faceUp)
    {
        return false;
    }
    return (c.isRed() != top.isRed()) && (c.rank == top.rank - 1);
}

bool KVFloaterSolitaire::canPlaceOnFoundation(const Card& c, S32 suit) const
{
    if (c.suit != suit)
    {
        return false;
    }
    if (mFoundation[suit].empty())
    {
        return c.rank == 1; // Ace starts a foundation
    }
    return mFoundation[suit].back().rank == c.rank - 1;
}

void KVFloaterSolitaire::exposeNewTableauTop(S32 col)
{
    if (!mTableau[col].empty() && !mTableau[col].back().faceUp)
    {
        mTableau[col].back().faceUp = true;
        playSound("UISndClick");
    }
}

void KVFloaterSolitaire::onSuccessfulMove()
{
    mProgressMadeThisPass = true;
    playSound("UISndClick");

    if (foundationCardCount() == 52)
    {
        endGame(true); // renders internally
        return;
    }
    checkForStuck(); // may end the game (and render) itself
    if (!mGameOver)
    {
        renderAll();
    }
}

void KVFloaterSolitaire::checkForStuck()
{
    if (mGameOver)
    {
        return;
    }
    if (mStock.empty() && mWaste.empty() && !hasAnyLegalMove())
    {
        endGame(false);
    }
}

bool KVFloaterSolitaire::hasAnyLegalMove() const
{
    if (!mWaste.empty())
    {
        const Card& c = mWaste.back();
        for (S32 s = 0; s < 4; ++s)
        {
            if (canPlaceOnFoundation(c, s)) return true;
        }
        for (S32 col = 0; col < NUM_COLS; ++col)
        {
            if (canPlaceOnTableau(c, col)) return true;
        }
    }

    for (S32 col = 0; col < NUM_COLS; ++col)
    {
        if (mTableau[col].empty() || !mTableau[col].back().faceUp)
        {
            continue;
        }
        const Card& c = mTableau[col].back();
        for (S32 s = 0; s < 4; ++s)
        {
            if (canPlaceOnFoundation(c, s)) return true;
        }
        for (S32 col2 = 0; col2 < NUM_COLS; ++col2)
        {
            if (col2 == col) continue;
            if (canPlaceOnTableau(c, col2)) return true;
        }
    }
    return false;
}

S32 KVFloaterSolitaire::foundationCardCount() const
{
    S32 total = 0;
    for (S32 s = 0; s < 4; ++s)
    {
        total += (S32)mFoundation[s].size();
    }
    return total;
}

void KVFloaterSolitaire::endGame(bool won)
{
    mGameOver = true;
    S32 score = currentScore();
    S32 count = foundationCardCount();

    if (won)
    {
        ++mGamesWon;
        playSound("UISndMoneyChangeUp");
        setStatus(llformat("You win! All 52 cards home. Score: %d  (New Game to play again.)", score));
    }
    else
    {
        playSound("UISndInvalidOp");
        setStatus(llformat("No moves left. %d/52 cards home (%d%%). Score: %d  (New Game to play again.)",
            count, count * 100 / 52, score));
    }

    if (score > mBestScore)
    {
        mBestScore = score;
    }
    saveStats();

    clearSelection();
    renderAll(); // updateStatus()'s !mGameOver guard protects the message just set above
}

void KVFloaterSolitaire::renderAll()
{
    renderStockAndWaste();
    renderFoundations();
    for (S32 col = 0; col < NUM_COLS; ++col)
    {
        renderTableau(col);
    }
    updateStatus();
}

void KVFloaterSolitaire::renderStockAndWaste()
{
    if (mStockBtn)
    {
        mStockBtn->setImageOverlay(mStock.empty() ? std::string() : std::string(kCardBack));
    }
    if (mWasteBtn)
    {
        mWasteBtn->setImageOverlay(mWaste.empty() ? std::string() : mWaste.back().iconName());
        mWasteBtn->setColor(mSelectedType == SourceType::Waste ? kHighlight : LLColor4::white);
    }
}

void KVFloaterSolitaire::renderFoundations()
{
    for (S32 s = 0; s < 4; ++s)
    {
        if (!mFoundationBtn[s]) continue;
        mFoundationBtn[s]->setImageOverlay(mFoundation[s].empty() ? std::string() : mFoundation[s].back().iconName());
    }
}

void KVFloaterSolitaire::renderTableau(S32 col)
{
    S32 count = (S32)mTableau[col].size();
    for (S32 row = 0; row < MAX_COL_SLOTS; ++row)
    {
        LLButton* btn = mTableauSlots[col][row];
        if (!btn) continue;

        if (row >= count)
        {
            // S24: row 0 stays visible even when the column is empty, as a
            // bare empty-slot placeholder - otherwise there is no clickable
            // target left to drop a King (or a King-headed stack) onto once
            // a column empties out.
            btn->setVisible(row == 0);
            if (row == 0)
            {
                btn->setImageOverlay(std::string());
                btn->setColor(LLColor4::white);
            }
            continue;
        }
        btn->setVisible(true);
        const Card& c = mTableau[col][row];
        btn->setImageOverlay(c.faceUp ? c.iconName() : std::string(kCardBack));

        bool highlight = (mSelectedType == SourceType::Tableau && mSelectedCol == col && row >= mSelectedRow);
        btn->setColor(highlight ? kHighlight : LLColor4::white);
    }
}

void KVFloaterSolitaire::updateStatus()
{
    if (mStatsText)
    {
        mStatsText->setText(LLStringExplicit(llformat("Score: %d   Best: %d   Played: %d   Won: %d",
            currentScore(), mBestScore, mGamesPlayed, mGamesWon)));
    }
    if (mStatusText && !mGameOver)
    {
        setStatus(mSelectedType != SourceType::None
            ? "Card selected - click a column or foundation to move it (or click it again to deselect)."
            : "Click a card to select it, or Stock to draw 3 (recycles when empty).");
    }
}

void KVFloaterSolitaire::setStatus(const std::string& text)
{
    if (mStatusText)
    {
        mStatusText->setText(LLStringExplicit(text));
    }
}

void KVFloaterSolitaire::playSound(const char* settingsName)
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

void KVFloaterSolitaire::loadStats()
{
    mBestScore = 0;
    mGamesPlayed = 0;
    mGamesWon = 0;

    std::string path = solitaireStatsPath();
    llifstream in(path.c_str());
    if (!in.is_open())
    {
        return;
    }

    LLSD root;
    LLSDSerialize::fromXML(root, in);
    if (root.isMap())
    {
        mBestScore = root["best_score"].asInteger();
        mGamesPlayed = root["games_played"].asInteger();
        mGamesWon = root["games_won"].asInteger();
    }
}

void KVFloaterSolitaire::saveStats()
{
    LLSD root;
    root["best_score"] = mBestScore;
    root["games_played"] = mGamesPlayed;
    root["games_won"] = mGamesWon;

    std::string path = solitaireStatsPath();
    llofstream out(path.c_str());
    if (!out.good())
    {
        LL_WARNS() << "KV Solitaire: could not save stats to " << path << LL_ENDL;
        return;
    }
    LLSDSerialize::toXML(root, out);
}
