/**
 * @file kvcalculator.cpp
 * @brief See kvcalculator.h.
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

#include "kvcalculator.h"

#include "llbutton.h"
#include "llcorehttputil.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace
{
    F64 parseFloatOr(const std::string& s, F64 fallback)
    {
        if (s.empty()) return fallback;
        return strtod(s.c_str(), nullptr);
    }

    F64 applyOp(F64 a, F64 b, char op)
    {
        switch (op)
        {
            case '+': return a + b;
            case '-': return a - b;
            case '*': return a * b;
            case '/': return a / b; // divide-by-zero handled by the caller before this is reached
            default:  return b;
        }
    }

    // Matches the keypad button labels ('x' for multiply) rather than the
    // internal op char, so the VFD reads the same symbol the user pressed.
    std::string opSymbol(char op)
    {
        return (op == '*') ? "x" : std::string(1, op);
    }

    const LLColor4 VFD_COLORS[] = {
        LLColor4(0.15f, 0.95f, 0.95f, 1.f), // cyan (default)
        LLColor4(0.95f, 0.55f, 0.15f, 1.f), // amber
        LLColor4(0.35f, 0.95f, 0.35f, 1.f), // green
        LLColor4(0.95f, 0.35f, 0.75f, 1.f), // magenta
        LLColor4(0.95f, 0.95f, 0.35f, 1.f), // yellow
    };
    constexpr S32 VFD_COLOR_COUNT = 5;

    // Casio-style percent semantics: b is treated as a percentage of a,
    // combined per the pending operator (b itself, e.g. 0 != 0.0 for '/',
    // is guarded by the caller before this is reached).
    F64 applyPercentOp(F64 a, F64 b, char op)
    {
        switch (op)
        {
            case '+': return a + a * (b / 100.0);
            case '-': return a - a * (b / 100.0);
            case '*': return a * (b / 100.0);
            case '/': return a / (b / 100.0);
            default:  return a * (b / 100.0);
        }
    }

    // Trims to a sane number of decimals and strips trailing zeros so the
    // VFD reads like a real calculator ("12.5" not "12.50000000").
    std::string formatNumber(F64 v)
    {
        if (std::fabs(v) < 1e-12) v = 0.0;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(8) << v;
        std::string s = ss.str();
        if (s.find('.') != std::string::npos)
        {
            size_t last = s.find_last_not_of('0');
            if (s[last] == '.') last--;
            s.erase(last + 1);
        }
        return s;
    }
}

KVFloaterCalculator::KVFloaterCalculator(const LLSD& key)
    : LLFloater(key)
{
}

KVFloaterCalculator::~KVFloaterCalculator()
{
}

void KVFloaterCalculator::onOpen(const LLSD& key)
{
    LLFloater::onOpen(key);

    if (!mRateFetchStarted)
    {
        mRateFetchStarted = true;
        LLCoros::instance().launch("KVFloaterCalculator::fetchLiveRateCoro",
            boost::bind(&KVFloaterCalculator::fetchLiveRateCoro));
    }
}

// static - deliberately touches no floater instance, so it stays safe to
// run to completion even if this floater has already been closed.
void KVFloaterCalculator::fetchLiveRateCoro()
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter =
        std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("fetchLiveRateCoro", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    static const std::string url = "https://api.secondlife.com/datafeeds/homepage.txt";

    LLSD result = httpAdapter->getRawAndSuspend(httpRequest, url);

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);
    if (!status) return; // best-effort: leave whatever rate is already saved

    const LLSD::Binary& rawBody = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    std::string body(rawBody.begin(), rawBody.end());

    // Feed format is alternating lines: key, then value, e.g.
    //   exchange_rate_updated_slt\n2026-11-09 15:22:58\nexchange_rate\n248.08\n
    std::istringstream stream(body);
    std::string line, prevKey;
    while (std::getline(stream, line))
    {
        if (prevKey == "exchange_rate")
        {
            F32 rate = (F32)strtod(line.c_str(), nullptr);
            if (rate > 0.f)
            {
                gSavedSettings.setF32("KVCalcRateUSD", rate);
            }
            break;
        }
        prevKey = line;
    }
}

bool KVFloaterCalculator::postBuild()
{
    mVfdText = getChild<LLTextBox>("vfd_text");

    for (S32 d = 0; d <= 9; ++d)
    {
        std::string name = llformat("kp_%d", d);
        getChild<LLButton>(name)->setCommitCallback(
            boost::bind(&KVFloaterCalculator::onDigit, this, llformat("%d", d)));
    }
    getChild<LLButton>("kp_point")->setCommitCallback(boost::bind(&KVFloaterCalculator::onPoint, this));
    getChild<LLButton>("kp_clear")->setCommitCallback(boost::bind(&KVFloaterCalculator::onClear, this));
    getChild<LLButton>("kp_backspace")->setCommitCallback(boost::bind(&KVFloaterCalculator::onBackspace, this));
    getChild<LLButton>("kp_div")->setCommitCallback(boost::bind(&KVFloaterCalculator::onOperator, this, '/'));
    getChild<LLButton>("kp_mul")->setCommitCallback(boost::bind(&KVFloaterCalculator::onOperator, this, '*'));
    getChild<LLButton>("kp_sub")->setCommitCallback(boost::bind(&KVFloaterCalculator::onOperator, this, '-'));
    getChild<LLButton>("kp_add")->setCommitCallback(boost::bind(&KVFloaterCalculator::onOperator, this, '+'));
    getChild<LLButton>("kp_equals")->setCommitCallback(boost::bind(&KVFloaterCalculator::onEquals, this));

    getChild<LLButton>("kp_fn1")->setCommitCallback(boost::bind(&KVFloaterCalculator::onPercent, this));
    getChild<LLButton>("kp_fn2")->setCommitCallback(boost::bind(&KVFloaterCalculator::onShowRate, this));
    getChild<LLButton>("kp_fn3")->setCommitCallback(boost::bind(&KVFloaterCalculator::onShowHundredLBreakdown, this));
    getChild<LLButton>("kp_fn4")->setCommitCallback(boost::bind(&KVFloaterCalculator::onPi, this));
    getChild<LLButton>("kp_fn5")->setCommitCallback(boost::bind(&KVFloaterCalculator::onToggleSign, this));
    getChild<LLButton>("kp_fn6")->setCommitCallback(boost::bind(&KVFloaterCalculator::onRandom, this));
    getChild<LLButton>("kp_fn7")->setCommitCallback(boost::bind(&KVFloaterCalculator::onSquare, this));
    getChild<LLButton>("kp_fn8")->setCommitCallback(boost::bind(&KVFloaterCalculator::onSquareRoot, this));
    getChild<LLButton>("kp_fn9")->setCommitCallback(boost::bind(&KVFloaterCalculator::onCycleVfdColor, this));

    setVFD("0");

    return LLFloater::postBuild();
}

void KVFloaterCalculator::setVFD(const std::string& text)
{
    if (mVfdText) mVfdText->setText(text);
}

void KVFloaterCalculator::refreshDisplay()
{
    setVFD(mEntry.empty() ? "0" : mEntry);
}

void KVFloaterCalculator::onDigit(const std::string& digit)
{
    if (mEntryIsResult)
    {
        mEntry.clear();
        mEntryIsResult = false;
    }

    if (mEntry == "0")
    {
        mEntry.clear();
    }

    if (mEntry.size() < 15)
    {
        mEntry += digit;
    }

    refreshDisplay();
}

void KVFloaterCalculator::onPoint()
{
    if (mEntryIsResult)
    {
        mEntry.clear();
        mEntryIsResult = false;
    }

    if (mEntry.empty())
    {
        mEntry = "0";
    }

    if (mEntry.find('.') == std::string::npos)
    {
        mEntry += ".";
    }

    refreshDisplay();
}

void KVFloaterCalculator::onClear()
{
    mEntry.clear();
    mAccumulator = 0.0;
    mPendingOp = 0;
    mEntryIsResult = false;
    setVFD("0");
}

void KVFloaterCalculator::onBackspace()
{
    if (mEntryIsResult) return;

    if (!mEntry.empty())
    {
        mEntry.erase(mEntry.size() - 1);
    }

    refreshDisplay();
}

bool KVFloaterCalculator::finishWithResult(F64 result)
{
    if (std::isnan(result))
    {
        setVFD("Err");
        resetAfterError();
        return false;
    }

    if (std::isinf(result) || std::fabs(result) > 1e15)
    {
        setVFD("Overflow");
        resetAfterError();
        return false;
    }

    mAccumulator = result;
    return true;
}

bool KVFloaterCalculator::applyPendingOp(F64 val)
{
    if (mPendingOp == '/' && val == 0.0)
    {
        setVFD("VERY FUNNY!");
        resetAfterError();
        return false;
    }

    return finishWithResult(applyOp(mAccumulator, val, mPendingOp));
}

void KVFloaterCalculator::resetAfterError()
{
    mAccumulator = 0.0;
    mPendingOp = 0;
    mEntry.clear();
    mEntryIsResult = true; // next digit starts fresh; VFD keeps the message until then
}

void KVFloaterCalculator::onOperator(char op)
{
    F64 val = parseFloatOr(mEntry, mAccumulator);

    if (mPendingOp != 0 && !mEntryIsResult)
    {
        if (!applyPendingOp(val)) return;
    }
    else
    {
        mAccumulator = val;
    }

    mPendingOp = op;
    mEntry.clear();
    mEntryIsResult = true;
    setVFD(formatNumber(mAccumulator) + " " + opSymbol(op));
}

void KVFloaterCalculator::onPercent()
{
    F64 b = parseFloatOr(mEntry, 0.0);

    if (mPendingOp == 0)
    {
        mAccumulator = b / 100.0;
        mEntry.clear();
        mEntryIsResult = true;
        setVFD(formatNumber(mAccumulator));
        return;
    }

    if (mPendingOp == '/' && b == 0.0)
    {
        setVFD("VERY FUNNY!");
        resetAfterError();
        return;
    }

    if (!finishWithResult(applyPercentOp(mAccumulator, b, mPendingOp))) return;

    mPendingOp = 0;
    mEntry.clear();
    mEntryIsResult = true;
    setVFD(formatNumber(mAccumulator));
}

void KVFloaterCalculator::onPi()
{
    if (mEntryIsResult)
    {
        mEntry.clear();
        mEntryIsResult = false;
    }

    mEntry = formatNumber((F64)F_PI);
    setVFD("\xcf\x80"); // U+03C0 GREEK SMALL LETTER PI
}

void KVFloaterCalculator::onToggleSign()
{
    if (!mEntry.empty())
    {
        if (mEntry[0] == '-')
        {
            mEntry.erase(0, 1);
        }
        else if (mEntry != "0")
        {
            mEntry.insert(0, "-");
        }
        refreshDisplay();
        return;
    }

    // Nothing actively being typed - flip whatever result/value is showing.
    mAccumulator = -mAccumulator;
    setVFD(formatNumber(mAccumulator));
}

void KVFloaterCalculator::onRandom()
{
    if (mEntryIsResult)
    {
        mEntry.clear();
        mEntryIsResult = false;
    }

    F32 r = ll_frand(); // [0, 1)
    mEntry = llformat("%.4f", r);
    refreshDisplay();
}

void KVFloaterCalculator::onSquare()
{
    if (!mEntry.empty())
    {
        F64 v = parseFloatOr(mEntry, 0.0);
        F64 result = v * v;

        if (std::isnan(result) || std::isinf(result) || std::fabs(result) > 1e15)
        {
            setVFD(std::isnan(result) ? "Err" : "Overflow");
            resetAfterError();
            return;
        }

        mEntry = formatNumber(result);
        refreshDisplay();
        return;
    }

    if (!finishWithResult(mAccumulator * mAccumulator)) return;
    setVFD(formatNumber(mAccumulator));
}

void KVFloaterCalculator::onSquareRoot()
{
    if (!mEntry.empty())
    {
        F64 v = parseFloatOr(mEntry, 0.0);
        F64 result = std::sqrt(v);

        if (std::isnan(result) || std::isinf(result) || std::fabs(result) > 1e15)
        {
            setVFD(std::isnan(result) ? "Err" : "Overflow");
            resetAfterError();
            return;
        }

        mEntry = formatNumber(result);
        refreshDisplay();
        return;
    }

    if (!finishWithResult(std::sqrt(mAccumulator))) return;
    setVFD(formatNumber(mAccumulator));
}

void KVFloaterCalculator::onCycleVfdColor()
{
    mVfdColorIndex = (mVfdColorIndex + 1) % VFD_COLOR_COUNT;
    if (mVfdText) mVfdText->setColor(VFD_COLORS[mVfdColorIndex]);
}

void KVFloaterCalculator::onShowRate()
{
    F32 rate = gSavedSettings.getF32("KVCalcRateUSD");
    if (rate <= 0.f) rate = 250.f;

    mEntry.clear();
    mPendingOp = 0;
    mEntryIsResult = true;
    setVFD(llformat("%.2f L$/$1", rate));
}

void KVFloaterCalculator::onShowHundredLBreakdown()
{
    F32 rateUsd = gSavedSettings.getF32("KVCalcRateUSD");
    if (rateUsd <= 0.f) rateUsd = 250.f;
    F32 usdToEur = gSavedSettings.getF32("KVCalcUsdToEur");
    if (usdToEur <= 0.f) usdToEur = 0.92f;
    F32 usdToGbp = gSavedSettings.getF32("KVCalcUsdToGbp");
    if (usdToGbp <= 0.f) usdToGbp = 0.79f;

    F64 usd = 100.0 / (F64)rateUsd;
    F64 eur = usd * (F64)usdToEur;
    F64 gbp = usd * (F64)usdToGbp;

    mEntry.clear();
    mPendingOp = 0;
    mEntryIsResult = true;
    setVFD(llformat("100L$ = $%.2f \xe2\x82\xac%.2f \xc2\xa3%.2f", usd, eur, gbp));
}

void KVFloaterCalculator::onEquals()
{
    if (mPendingOp == 0) return;

    F64 val = parseFloatOr(mEntry, mAccumulator);
    if (!applyPendingOp(val)) return;

    mPendingOp = 0;
    mEntry.clear();
    mEntryIsResult = true;
    setVFD(formatNumber(mAccumulator));
}
