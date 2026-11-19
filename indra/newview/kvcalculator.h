/**
 * @file kvcalculator.h
 * @brief KV Calculator - pocket-calculator-styled utility floater. A VFD-look
 * cyan display plus a standard four-function keypad (digits, point, clear,
 * backspace, +-*|/, =). A row of currently-blank keypad buttons is reserved
 * for future SL-centric functions (currency, LI budget, color, tip split,
 * etc.) to be wired up in a later pass.
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

#ifndef LL_KVCALCULATOR_H
#define LL_KVCALCULATOR_H

#include "llfloater.h"
#include <string>

class LLTextBox;

class KVFloaterCalculator : public LLFloater
{
public:
    KVFloaterCalculator(const LLSD& key);
    ~KVFloaterCalculator();

    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    // Fetches the public, unauthenticated SL "exchange_rate" data feed once
    // per viewer session and stores it into KVCalcRateUSD - best-effort,
    // fire-and-forget: on any failure the existing/default rate is left
    // untouched. Runs as a coroutine so the HTTP wait never blocks the main
    // thread (same LLCoreHttpUtil::HttpCoroutineAdapter pattern used by
    // llfloatergridstatus.cpp). Static and takes no floater pointer so it's
    // safe even if this floater is closed before the response arrives.
    static void fetchLiveRateCoro();
    bool mRateFetchStarted = false;

    void onDigit(const std::string& digit);
    void onPoint();
    void onClear();
    void onBackspace();
    void onOperator(char op);
    void onEquals();

    // Casio-style percent key: finishes the pending op early with its
    // second operand treated as a percentage of the first, e.g.
    // "200 x 15 %" = 30 (15% of 200), "500 - 20 %" = 400 (20% off 500),
    // "1000 + 10 %" = 1100 (10% added to 1000). With no pending op, "%"
    // just divides the current entry by 100.
    void onPercent();

    // Drops the viewer's existing F_PI constant (llmath.h) into the current
    // entry, same as typing its digits by hand, but shows the pi symbol on
    // the VFD instead of the raw decimal until the entry is edited further.
    void onPi();

    // Flips the sign of whatever's currently being entered/displayed.
    void onToggleSign();

    // Drops a pseudo-random [0,1) value (viewer's ll_frand(), llrand.h)
    // into the current entry, rounded to 4 decimal places.
    void onRandom();

    // Squares whatever's currently being entered/displayed - e.g. for a
    // parcel's area: "32 x2" = 1024 (32m x 32m).
    void onSquare();
    // Square root - the reverse of x2, e.g. turns an area back into a
    // side length: "1024 sqrt" = 32.
    void onSquareRoot();

    // Cycles the VFD text through a small fixed palette, purely cosmetic.
    void onCycleVfdColor();
    S32 mVfdColorIndex = 0;

    // Shows the current estimated L$-per-US$ rate - a quick "what's
    // today's rate" lookup.
    void onShowRate();
    // Fixed-reference breakdown of what 100 L$ is worth in US$/EUR/GBP,
    // symbol-only to fit the one-line VFD.
    void onShowHundredLBreakdown();

    // Applies mPendingOp to mAccumulator and val. On divide-by-zero or an
    // overflow/NaN result it puts a message on the VFD instead (S24: the
    // "mathematical comedians" easter egg) and resets calculator state,
    // returning false so the caller leaves the message on screen rather
    // than overwriting it.
    bool applyPendingOp(F64 val);
    // Shared result landing for applyPendingOp()/onPercent(): on NaN/overflow
    // it shows the error message and resets state, returning false; on
    // success it stores the result into mAccumulator and returns true.
    bool finishWithResult(F64 result);
    void resetAfterError();

    void refreshDisplay();
    void setVFD(const std::string& text);

    LLTextBox* mVfdText = nullptr;

    // Classic "last operator wins" four-function logic (no operator
    // precedence) - matches how a real pocket calculator behaves, not a
    // scientific/expression one.
    F64 mAccumulator = 0.0;
    char mPendingOp = 0;          // '+','-','*','/' or 0 = none yet
    std::string mEntry;           // text of the number currently being typed
    bool mEntryIsResult = false;  // true right after '=' - next digit starts fresh
    bool mEntryFromFunction = false;  // entry holds a square/root result: still an operand, but the next digit starts fresh
};

#endif // LL_KVCALCULATOR_H
