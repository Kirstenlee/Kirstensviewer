/**
 * @file kvblackjack.h
 * @brief KV Blackjack - standalone card-table floater, self-contained game
 * system fired from the S24 console ("play blackjack" / "blackjack" / "21").
 * Real card-face textures (newview/skins/default/textures/cards/), a fresh
 * shuffled deck per hand, standard house rules, a session-only bankroll.
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

#ifndef LL_KVBLACKJACK_H
#define LL_KVBLACKJACK_H

#include "llfloater.h"
#include <vector>
#include <string>

class LLTextBox;
class LLButton;
class LLLineEditor;
class LLIconCtrl;

// S24: standalone blackjack floater. Deliberately a fresh single 52-card
// deck shuffled per hand (not a persistent multi-deck shoe) - this is a
// casual console-fired toy, not a card-counting-resistant table, and a
// fresh shuffle keeps the state machine tiny: no shoe-depletion tracking,
// no reshuffle-mid-hand edge case.
class KVFloaterBlackjack : public LLFloater
{
public:
    KVFloaterBlackjack(const LLSD& key);
    ~KVFloaterBlackjack();

    bool postBuild() override;
    void onClose(bool app_quitting) override;

private:
    // rank 1=A, 2-10 pip, 11=J, 12=Q, 13=K. suit 0=S, 1=H, 2=D, 3=C.
    struct Card
    {
        S32 rank;
        S32 suit;
        std::string iconName() const;
    };

    enum class Stage
    {
        Betting,    // waiting for Deal; bet is editable. Also the state right
                    // after a hand settles - the result stays in status_text
                    // until the next Deal overwrites it, Deal/bet are live again.
        PlayerTurn, // hit/stand/double live
        DealerTurn  // dealer draws, brief reveal pause (resolved synchronously)
    };

    // S24 MAX_SLOTS: 8 fixed icon widgets per hand is generous headroom -
    // busting past 21 with aces-low on every card needs at least 8 cards
    // (A+A+...+6), astronomically unlikely but the slot array is bounds-
    // checked regardless (extra draws past the slot count still count
    // toward the hand value, they just aren't individually shown).
    static const S32 MAX_SLOTS = 8;

    void buildAndShuffleDeck();
    Card drawCard();

    void onBetUp();
    void onBetDown();
    void onDealClicked();
    void onHitClicked();
    void onStandClicked();
    void onDoubleClicked();
    void onCashOutClicked();

    void dealInitial();
    void resolveDealerAndSettle();
    void settleHand(const std::string& resultText, S32 bankrollDelta, const char* sound);

    S32 handValue(const std::vector<Card>& hand) const;
    bool isBlackjack(const std::vector<Card>& hand) const;
    bool isBust(const std::vector<Card>& hand) const;

    void renderHands(bool hideDealerHole);
    void renderRow(const std::vector<Card>& hand, LLIconCtrl* const* slots, bool hideLast);
    void updateBankrollAndBetText();
    void updateButtonStates();
    void setStatus(const std::string& text);
    void playSound(const char* settingsName);
    void checkBroke();
    void noteBankrollForBestScore(); // call after any mBankroll change that could be a new high

    void loadBestScore();
    void saveBestScore();

    std::vector<Card> mDeck;
    std::vector<Card> mPlayerHand;
    std::vector<Card> mDealerHand;

    Stage mStage = Stage::Betting;
    S32 mBankroll = 500;
    S32 mBet = 25;
    S32 mHandBet = 0;
    bool mDoubled = false;

    // S24: highest bankroll ever reached, independent of the current run -
    // loaded in postBuild()/saved in onClose(), AND saved immediately on
    // every new high (see noteBankrollForBestScore()) so it survives even
    // an unclean close, not just the ordinary close path.
    S32 mBestScore = 500;

    LLTextBox* mBankrollText = nullptr;
    LLTextBox* mBetText = nullptr;
    LLTextBox* mStatusText = nullptr;
    LLTextBox* mPlayerValueText = nullptr;
    LLTextBox* mDealerValueText = nullptr;
    LLTextBox* mBestScoreText = nullptr;

    LLButton* mBetUpBtn = nullptr;
    LLButton* mBetDownBtn = nullptr;
    LLButton* mDealBtn = nullptr;
    LLButton* mHitBtn = nullptr;
    LLButton* mStandBtn = nullptr;
    LLButton* mDoubleBtn = nullptr;
    LLButton* mCashOutBtn = nullptr;

    LLIconCtrl* mDealerSlots[MAX_SLOTS] = {};
    LLIconCtrl* mPlayerSlots[MAX_SLOTS] = {};
};

#endif // LL_KVBLACKJACK_H
