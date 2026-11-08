/**
 * @file kvblackjack.cpp
 * @brief KV Blackjack - standalone card-table floater implementation.
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

#include "kvblackjack.h"

#include "llagent.h"
#include "llaudioengine.h"
#include "llbutton.h"
#include "lldir.h"
#include "lliconctrl.h"
#include "lllineeditor.h"
#include "llrand.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

namespace
{
    // S24: global to the viewer install, not per-SL-account - same reasoning
    // LLFloaterMusicPlayer's playlist/EQ persistence already uses in this
    // codebase: a high score from messing around at the console isn't tied
    // to which avatar happened to be logged in when it was set.
    std::string blackjackBestScorePath()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "kv_blackjack_best.xml");
    }

    // S24: card face/back textures live in newview/skins/default/textures/cards/
    // and are deliberately NOT registered in textures.xml - that file's own
    // header comment confirms an unregistered image can be referenced directly
    // by its path relative to the textures directory, which is all 53 of these
    // need (no atlas, no preload tuning; this floater is opened rarely and the
    // images are tiny).
    const char* kCardBack = "cards/Back.png";

    const F32 SOUND_GAIN = 1.0f;
}

std::string KVFloaterBlackjack::Card::iconName() const
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

KVFloaterBlackjack::KVFloaterBlackjack(const LLSD& key)
    : LLFloater(key)
{
}

KVFloaterBlackjack::~KVFloaterBlackjack()
{
}

bool KVFloaterBlackjack::postBuild()
{
    mBankrollText = getChild<LLTextBox>("bankroll_text");
    mBetText = getChild<LLTextBox>("bet_text");
    mStatusText = getChild<LLTextBox>("status_text");
    mPlayerValueText = getChild<LLTextBox>("player_value_text");
    mDealerValueText = getChild<LLTextBox>("dealer_value_text");
    mBestScoreText = getChild<LLTextBox>("best_score_text");

    mBetUpBtn = getChild<LLButton>("bet_up_btn");
    mBetUpBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onBetUp, this));

    mBetDownBtn = getChild<LLButton>("bet_down_btn");
    mBetDownBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onBetDown, this));

    mDealBtn = getChild<LLButton>("deal_btn");
    mDealBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onDealClicked, this));

    mHitBtn = getChild<LLButton>("hit_btn");
    mHitBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onHitClicked, this));

    mStandBtn = getChild<LLButton>("stand_btn");
    mStandBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onStandClicked, this));

    mDoubleBtn = getChild<LLButton>("double_btn");
    mDoubleBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onDoubleClicked, this));

    mCashOutBtn = getChild<LLButton>("cashout_btn");
    mCashOutBtn->setClickedCallback(boost::bind(&KVFloaterBlackjack::onCashOutClicked, this));

    for (S32 i = 0; i < MAX_SLOTS; ++i)
    {
        mDealerSlots[i] = getChild<LLIconCtrl>(llformat("dealer_slot_%d", i));
        mPlayerSlots[i] = getChild<LLIconCtrl>(llformat("player_slot_%d", i));
        mDealerSlots[i]->setVisible(false);
        mPlayerSlots[i]->setVisible(false);
    }

    loadBestScore();
    updateBankrollAndBetText();
    setStatus("Press Deal to start a hand.");
    updateButtonStates();

    return LLFloater::postBuild();
}

void KVFloaterBlackjack::onClose(bool app_quitting)
{
    // S24: defensive save on close - noteBankrollForBestScore() already
    // saves immediately on every new high, but this catches the case where
    // mBestScore was bumped some other way in the future and nothing else
    // flushed it yet. Matches LLFloaterMusicPlayer's belt-and-suspenders
    // save-on-close for its own persisted state.
    saveBestScore();
}

void KVFloaterBlackjack::buildAndShuffleDeck()
{
    mDeck.clear();
    mDeck.reserve(52);
    for (S32 suit = 0; suit < 4; ++suit)
    {
        for (S32 rank = 1; rank <= 13; ++rank)
        {
            mDeck.push_back(Card{ rank, suit });
        }
    }

    // Fisher-Yates, viewer RNG (llcommon/llrand.h) - same source the rest of
    // the engine uses for anything that needs non-cryptographic randomness.
    for (S32 i = (S32)mDeck.size() - 1; i > 0; --i)
    {
        S32 j = ll_rand(i + 1);
        std::swap(mDeck[i], mDeck[j]);
    }
}

KVFloaterBlackjack::Card KVFloaterBlackjack::drawCard()
{
    // S24: a fresh 52-card deck is built per hand (see buildAndShuffleDeck(),
    // called from dealInitial()), so running out mid-hand is not reachable -
    // a hand cannot legally hold more than 21 cards before busting, let alone
    // 52. No empty-deck guard needed.
    Card c = mDeck.back();
    mDeck.pop_back();
    return c;
}

S32 KVFloaterBlackjack::handValue(const std::vector<Card>& hand) const
{
    S32 total = 0;
    S32 aces = 0;
    for (const Card& c : hand)
    {
        S32 v = llmin(c.rank, 10); // J/Q/K already 11-13, clamp to 10; ace stays 1 here
        total += v;
        if (c.rank == 1)
        {
            ++aces;
        }
    }
    // Promote one ace from 1 to 11 (adds 10) as long as it doesn't bust.
    if (aces > 0 && total + 10 <= 21)
    {
        total += 10;
    }
    return total;
}

bool KVFloaterBlackjack::isBlackjack(const std::vector<Card>& hand) const
{
    return hand.size() == 2 && handValue(hand) == 21;
}

bool KVFloaterBlackjack::isBust(const std::vector<Card>& hand) const
{
    return handValue(hand) > 21;
}

void KVFloaterBlackjack::onBetUp()
{
    if (mStage != Stage::Betting)
    {
        return;
    }
    mBet = llclamp(mBet + 5, 5, mBankroll);
    updateBankrollAndBetText();
}

void KVFloaterBlackjack::onBetDown()
{
    if (mStage != Stage::Betting)
    {
        return;
    }
    mBet = llclamp(mBet - 5, 5, mBankroll);
    updateBankrollAndBetText();
}

void KVFloaterBlackjack::onDealClicked()
{
    if (mStage != Stage::Betting)
    {
        return;
    }
    checkBroke();
    mBet = llclamp(mBet, 1, mBankroll);
    dealInitial();
}

void KVFloaterBlackjack::dealInitial()
{
    buildAndShuffleDeck();
    mPlayerHand.clear();
    mDealerHand.clear();
    mDoubled = false;
    mHandBet = mBet;
    mBankroll -= mHandBet;

    mPlayerHand.push_back(drawCard());
    mDealerHand.push_back(drawCard());
    mPlayerHand.push_back(drawCard());
    mDealerHand.push_back(drawCard()); // dealer's hole card - hidden until settle

    mStage = Stage::PlayerTurn;
    playSound("UISndClick");

    bool player_bj = isBlackjack(mPlayerHand);
    bool dealer_bj = isBlackjack(mDealerHand);

    if (player_bj || dealer_bj)
    {
        // Naturals resolve immediately, no hit/stand offered.
        mStage = Stage::DealerTurn;
        renderHands(false);
        if (player_bj && dealer_bj)
        {
            settleHand("Push - both blackjack.", mHandBet, nullptr);
        }
        else if (player_bj)
        {
            S32 payout = mHandBet + (mHandBet * 3) / 2; // stake back + 3:2
            settleHand(llformat("Blackjack! You win %d.", (mHandBet * 3) / 2), payout, "UISndMoneyChangeUp");
        }
        else
        {
            settleHand("Dealer has blackjack. You lose.", 0, "UISndInvalidOp");
        }
        return;
    }

    updateBankrollAndBetText();
    setStatus("Your move: hit, stand, or double.");
    renderHands(true);
    updateButtonStates();
}

void KVFloaterBlackjack::onHitClicked()
{
    if (mStage != Stage::PlayerTurn)
    {
        return;
    }
    mPlayerHand.push_back(drawCard());
    playSound("UISndClick");

    if (isBust(mPlayerHand))
    {
        mStage = Stage::DealerTurn;
        renderHands(false);
        settleHand("Bust! You lose.", 0, "UISndInvalidOp");
        return;
    }

    renderHands(true);
    setStatus("Your move: hit, stand, or double.");
    updateButtonStates();
}

void KVFloaterBlackjack::onStandClicked()
{
    if (mStage != Stage::PlayerTurn)
    {
        return;
    }
    mStage = Stage::DealerTurn;
    resolveDealerAndSettle();
}

void KVFloaterBlackjack::onDoubleClicked()
{
    // Only ever enabled (see updateButtonStates()) on exactly two cards
    // with enough bankroll to cover the extra stake.
    if (mStage != Stage::PlayerTurn || mPlayerHand.size() != 2 || mBankroll < mHandBet)
    {
        return;
    }
    mBankroll -= mHandBet;
    mHandBet *= 2;
    mDoubled = true;

    mPlayerHand.push_back(drawCard());
    playSound("UISndClick");

    if (isBust(mPlayerHand))
    {
        mStage = Stage::DealerTurn;
        renderHands(false);
        settleHand("Bust on double! You lose.", 0, "UISndInvalidOp");
        return;
    }

    mStage = Stage::DealerTurn;
    resolveDealerAndSettle();
}

void KVFloaterBlackjack::onCashOutClicked()
{
    // Only between hands (same window as Deal/bet) - cashing out mid-hand
    // would mean walking away from a bet already on the table, which this
    // floater never lets you do any other way either.
    if (mStage != Stage::Betting)
    {
        return;
    }
    noteBankrollForBestScore();
    setStatus(llformat("Cashed out at %d. New stake: 500.", mBankroll));
    mBankroll = 500;
    mBet = llmin(mBet, mBankroll);
    playSound("UISndMoneyChangeUp");
    updateBankrollAndBetText();
    updateButtonStates();
}

void KVFloaterBlackjack::resolveDealerAndSettle()
{
    // S24: no reveal-delay timer here - the earlier console-text design
    // used a tick-based pause for drama, but this is a real floater with
    // real card art; the dealer's hand resolves in one call and the player
    // sees the final table immediately. Nothing about the result depends
    // on wall-clock time, so there is nothing to desync by not animating it.
    while (handValue(mDealerHand) < 17)
    {
        mDealerHand.push_back(drawCard());
    }

    renderHands(false);

    S32 player_total = handValue(mPlayerHand);
    S32 dealer_total = handValue(mDealerHand);

    if (dealer_total > 21)
    {
        settleHand(llformat("Dealer busts with %d. You win %d.", dealer_total, mHandBet),
                   mHandBet * 2, "UISndMoneyChangeUp");
    }
    else if (dealer_total > player_total)
    {
        settleHand(llformat("Dealer wins, %d to %d.", dealer_total, player_total), 0, "UISndMoneyChangeDown");
    }
    else if (player_total > dealer_total)
    {
        settleHand(llformat("You win, %d to %d.", player_total, dealer_total),
                   mHandBet * 2, "UISndMoneyChangeUp");
    }
    else
    {
        settleHand(llformat("Push at %d.", player_total), mHandBet, nullptr);
    }
}

void KVFloaterBlackjack::settleHand(const std::string& resultText, S32 bankrollDelta, const char* sound)
{
    // Back to Betting, not a dead-end "Settled" state - Deal/bet need to be
    // live again so the next hand can start. The result text (set below)
    // stays on screen until the next Deal overwrites it.
    mStage = Stage::Betting;
    mBankroll += bankrollDelta;
    noteBankrollForBestScore();
    if (sound)
    {
        playSound(sound);
    }
    setStatus(resultText + "  (Deal for another hand.)");
    checkBroke();
    updateBankrollAndBetText();
    updateButtonStates();
}

void KVFloaterBlackjack::checkBroke()
{
    if (mBankroll <= 0)
    {
        mBankroll = 500;
        setStatus("Broke! Bankroll reset to 500.");
    }
}

void KVFloaterBlackjack::noteBankrollForBestScore()
{
    if (mBankroll > mBestScore)
    {
        mBestScore = mBankroll;
        // Saved immediately, not just on close - this is what makes the
        // best score survive the floater being closed (which single-
        // instance floaters in this engine can destroy, not just hide),
        // and it also means an unclean viewer exit never loses a new high.
        saveBestScore();
    }
}

void KVFloaterBlackjack::loadBestScore()
{
    mBestScore = 500;

    std::string path = blackjackBestScorePath();
    llifstream in(path.c_str());
    if (!in.is_open())
    {
        return;
    }

    LLSD root;
    LLSDSerialize::fromXML(root, in);
    if (root.isMap() && root.has("best_score"))
    {
        mBestScore = llmax(500, root["best_score"].asInteger());
    }
}

void KVFloaterBlackjack::saveBestScore()
{
    LLSD root;
    root["best_score"] = mBestScore;

    std::string path = blackjackBestScorePath();
    llofstream out(path.c_str());
    if (!out.good())
    {
        LL_WARNS() << "KV Blackjack: could not save best score to " << path << LL_ENDL;
        return;
    }
    LLSDSerialize::toXML(root, out);
}

void KVFloaterBlackjack::renderHands(bool hideDealerHole)
{
    renderRow(mDealerHand, mDealerSlots, hideDealerHole);
    renderRow(mPlayerHand, mPlayerSlots, false);

    if (mDealerValueText)
    {
        mDealerValueText->setText(hideDealerHole
            ? LLStringExplicit(llformat("Dealer shows %d", llmin(mDealerHand.empty() ? 0 : mDealerHand[0].rank, 10)))
            : LLStringExplicit(llformat("Dealer: %d", handValue(mDealerHand))));
    }
    if (mPlayerValueText)
    {
        mPlayerValueText->setText(LLStringExplicit(llformat("You: %d", handValue(mPlayerHand))));
    }
}

void KVFloaterBlackjack::renderRow(const std::vector<Card>& hand, LLIconCtrl* const* slots, bool hideLast)
{
    for (S32 i = 0; i < MAX_SLOTS; ++i)
    {
        if (!slots[i])
        {
            continue;
        }
        if (i >= (S32)hand.size())
        {
            slots[i]->setVisible(false);
            continue;
        }
        bool hide_this_one = hideLast && (i == (S32)hand.size() - 1);
        slots[i]->setValue(LLSD(hide_this_one ? std::string(kCardBack) : hand[i].iconName()));
        slots[i]->setVisible(true);
    }
}

void KVFloaterBlackjack::updateBankrollAndBetText()
{
    if (mBankrollText)
    {
        mBankrollText->setText(LLStringExplicit(llformat("Bankroll: %d", mBankroll)));
    }
    if (mBetText)
    {
        mBetText->setText(LLStringExplicit(llformat("Bet: %d", mBet)));
    }
    if (mBestScoreText)
    {
        mBestScoreText->setText(LLStringExplicit(llformat("Best: %d", mBestScore)));
    }
}

void KVFloaterBlackjack::updateButtonStates()
{
    bool betting = (mStage == Stage::Betting);
    bool playing = (mStage == Stage::PlayerTurn);

    if (mDealBtn) mDealBtn->setEnabled(betting && mBankroll > 0);
    if (mBetUpBtn) mBetUpBtn->setEnabled(betting);
    if (mBetDownBtn) mBetDownBtn->setEnabled(betting);
    if (mCashOutBtn) mCashOutBtn->setEnabled(betting);
    if (mHitBtn) mHitBtn->setEnabled(playing);
    if (mStandBtn) mStandBtn->setEnabled(playing);
    if (mDoubleBtn) mDoubleBtn->setEnabled(playing && mPlayerHand.size() == 2 && mBankroll >= mHandBet);
}

void KVFloaterBlackjack::setStatus(const std::string& text)
{
    if (mStatusText)
    {
        mStatusText->setText(LLStringExplicit(text));
    }
}

void KVFloaterBlackjack::playSound(const char* settingsName)
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
