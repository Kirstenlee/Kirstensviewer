/**
 * @file kvbench.h
 * @brief KV Bench - a small, self-contained performance test: Math (CPU),
 * RAM bandwidth, Disk bandwidth and Graphics (GPU fill-rate), combined into
 * one "Kmarks" score. No network/external dependency at all - every sub-
 * test measures something real on the local machine, nothing to curate or
 * maintain going forward. Math/RAM/Disk run on a worker thread (same
 * LLActionThread-style poll shape already used by lllogchat.cpp's
 * LLLoadHistoryThread) so the viewer stays responsive; Graphics runs
 * in-frame via draw() since DX11 submission is main-thread only here.
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

#ifndef LL_KVBENCH_H
#define LL_KVBENCH_H

#include "llfloater.h"
#include "llthread.h"
#include "llmutex.h"
#include "lltimer.h"
#include <memory>
#include <string>

class LLPanel;

class LLButton;
class LLTextBox;

// S24: worker thread running the three off-GPU sub-tests sequentially.
// Shape copied from lllogchat.h's LLActionThread (mMutex + isFinished()
// polled by the main thread every draw(), not a WorkQueue callback) -
// that's a real, already-shipped pattern in this codebase for exactly this
// "do blocking work off-thread, main thread polls for completion" need.
class KVBenchThread : public LLThread
{
public:
    KVBenchThread();
    ~KVBenchThread();

    void run() override;

    bool isFinished();
    S32 getStage(); // 0=Math, 1=RAM, 2=Disk, 3=done

    // Only meaningful once isFinished() is true. All guarded by mMutex
    // internally (simple getter-returns-copy, no external locking needed).
    F64 getMathOpsPerSec();
    F64 getRamGBPerSec();
    F64 getDiskMBPerSec(); // average of write+read throughput
    F64 getDiskWriteMBPerSec();
    F64 getDiskReadMBPerSec();

private:
    void runMath();
    void runRam();
    void runDisk();
    void setStage(S32 stage);

    LLMutex mMutex;
    bool mFinished;
    S32 mStage;

    F64 mMathOpsPerSec;
    F64 mRamGBPerSec;
    F64 mDiskWriteMBPerSec;
    F64 mDiskReadMBPerSec;
};

class KVFloaterBench : public LLFloater
{
public:
    KVFloaterBench(const LLSD& key);
    ~KVFloaterBench();

    bool postBuild() override;
    void draw() override;
    void onClose(bool app_quitting) override;

private:
    void onRunClicked();
    void startGraphicsTest();
    void finishAndScore();
    void setStatus(const std::string& text);
    void resetDisplay();

    // S24: the graphics sub-test draws a heavy batch of overlapping quads
    // every frame for a fixed duration and counts frames - the simplest
    // meaningful GPU/fill-rate+driver-overhead test achievable with this
    // floater's own existing raw-draw tools (dx_rect_2d), no new shader
    // pipeline needed. Must run on the main/render thread, hence driven
    // from draw() rather than the worker thread.
    bool mGraphicsBenchActive = false;
    LLTimer mGraphicsBenchTimer;
    S32 mGraphicsBenchFrames = 0;
    F64 mGfxFps = 0.0;

    std::unique_ptr<KVBenchThread> mWorkerThread;
    bool mRunning = false;

    LLTextBox* mStatusText = nullptr;
    LLTextBox* mScoreText = nullptr;
    LLTextBox* mCompareText = nullptr;
    LLTextBox* mDetailText = nullptr;
    LLTextBox* mDetailText2 = nullptr;
    LLButton* mRunBtn = nullptr;

    // S24: the graphics sub-test draws its quad batch into this panel's
    // rect every frame while active - drawn directly in draw() (dx_rect_2d),
    // not as XUI children.
    LLPanel* mGfxPanel = nullptr;
};

#endif // LL_KVBENCH_H
