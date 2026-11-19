/**
 * @file kvbench.cpp
 * @brief KV Bench - standalone performance-test floater implementation. See
 * kvbench.h for the overall design.
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

#include "kvbench.h"

#include "dxpipeline.h"
#include "llbutton.h"
#include "lldir.h"
#include "llpanel.h"
#include "pipeline.h" // gPipeline
#include "llrender2dutils.h" // dx_rect_2d
#include "lltextbox.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <malloc.h>
#include <vector>

#include "llwin32headers.h"

namespace
{
    // S24: target durations per sub-test - long enough for a stable reading,
    // short enough that the whole benchmark finishes in well under 10s.
    constexpr F64 MATH_TEST_SECONDS = 2.0;
    constexpr F64 RAM_TEST_SECONDS = 2.0;
    constexpr F64 DISK_TEST_SECONDS = 1.5; // per phase (write, then read)
    constexpr F64 GFX_TEST_SECONDS = 3.0;

    constexpr size_t RAM_BUFFER_BYTES = 64 * 1024 * 1024;
    constexpr size_t DISK_BUFFER_BYTES = 64 * 1024 * 1024;

    // S24: CALIBRATED against Kirstenlee's own reference rig (AMD 9900X3D /
    // RTX 5080) - average of 3 real runs on that machine (not estimates):
    //   Math:  73.09M / 72.93M / 72.94M ops/s -> 72.99M
    //   RAM:   29.9 / 29.9 / 29.9 GB/s         -> 29.9
    //   Disk:  4860 / 4246 / 4186 MB/s         -> 4431  (unbuffered write-through I/O)
    //   Gfx:   571 / 568 / 571 fps             -> 570
    // A sub-score of 250 means "exactly matches the reference rig" for that
    // one test; the 4 sub-scores sum to 1000 Kmarks at parity.
    constexpr F64 REF_MATH_OPS_PER_SEC = 72990000.0;
    constexpr F64 REF_RAM_GB_PER_SEC = 29.9;
    constexpr F64 REF_DISK_MB_PER_SEC = 4431.0;
    constexpr F64 REF_GFX_FPS = 570.0;

    constexpr S32 GFX_QUADS_PER_FRAME = 400;

    std::string benchTempFilePath()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_CACHE, "kvbench_tmp.dat");
    }

    // S24: keeps the detail line short regardless of magnitude - the Math
    // sub-test's raw ops/sec is routinely 8-9 digits on its own, which blew
    // the single-line detail_text off the edge of its panel before this.
    std::string formatRate(F64 value)
    {
        if (value >= 1.0e9) return llformat("%.2fB", value / 1.0e9);
        if (value >= 1.0e6) return llformat("%.2fM", value / 1.0e6);
        if (value >= 1.0e3) return llformat("%.2fK", value / 1.0e3);
        return llformat("%.1f", value);
    }
}

// ---------------------------------------------------------------------------
// KVBenchThread
// ---------------------------------------------------------------------------

KVBenchThread::KVBenchThread()
    : LLThread("KVBenchThread"),
      mFinished(false),
      mStage(0),
      mMathOpsPerSec(0.0),
      mRamGBPerSec(0.0),
      mDiskWriteMBPerSec(0.0),
      mDiskReadMBPerSec(0.0)
{
}

KVBenchThread::~KVBenchThread()
{
}

void KVBenchThread::setStage(S32 stage)
{
    mMutex.lock();
    mStage = stage;
    mMutex.unlock();
}

S32 KVBenchThread::getStage()
{
    mMutex.lock();
    S32 s = mStage;
    mMutex.unlock();
    return s;
}

bool KVBenchThread::isFinished()
{
    mMutex.lock();
    bool f = mFinished;
    mMutex.unlock();
    return f;
}

F64 KVBenchThread::getMathOpsPerSec()
{
    mMutex.lock();
    F64 v = mMathOpsPerSec;
    mMutex.unlock();
    return v;
}

F64 KVBenchThread::getRamGBPerSec()
{
    mMutex.lock();
    F64 v = mRamGBPerSec;
    mMutex.unlock();
    return v;
}

F64 KVBenchThread::getDiskWriteMBPerSec()
{
    mMutex.lock();
    F64 v = mDiskWriteMBPerSec;
    mMutex.unlock();
    return v;
}

F64 KVBenchThread::getDiskReadMBPerSec()
{
    mMutex.lock();
    F64 v = mDiskReadMBPerSec;
    mMutex.unlock();
    return v;
}

F64 KVBenchThread::getDiskMBPerSec()
{
    return (getDiskWriteMBPerSec() + getDiskReadMBPerSec()) / 2.0;
}

void KVBenchThread::run()
{
    // S24 : each stage is skipped, and each loop below ends early, once the thread is asked to quit
    setStage(0);
    runMath();
    setStage(1);
    if (!isQuitting()) runRam();
    setStage(2);
    if (!isQuitting()) runDisk();
    setStage(3);

    mMutex.lock();
    mFinished = true;
    mMutex.unlock();
}

// S24: a real dependency chain (each iteration's input is the previous
// iteration's output) so the compiler can't vectorize/reorder it away, and
// the final accumulator is stored into a member so the whole loop can't be
// dead-code-eliminated either. Reports iterations/sec, not a more abstract
// "FLOPS" figure - simpler, and just as valid as a relative comparison
// since every machine runs the exact same loop body.
void KVBenchThread::runMath()
{
    LLTimer timer;
    F64 acc = 1.0;
    U64 iterations = 0;

    while (!isQuitting() && timer.getElapsedTimeF64() < MATH_TEST_SECONDS)
    {
        for (S32 i = 0; i < 10000; ++i)
        {
            acc = std::sin(acc) + std::cos(acc * 1.0000001) + std::sqrt(std::fabs(acc) + 1.0);
        }
        iterations += 10000;
    }

    F64 elapsed = timer.getElapsedTimeF64();
    mMutex.lock();
    mMathOpsPerSec = elapsed > 0.0 ? (F64)iterations / elapsed : 0.0;
    // Fold the accumulator into the result in a way that can never change
    // the reported number in practice (it's finite, bounded, and this adds
    // a vanishingly small amount) - purely to give the optimizer a reason
    // it can't prove the loop had no effect.
    mMathOpsPerSec += (acc - acc);
    mMutex.unlock();
}

void KVBenchThread::runRam()
{
    std::vector<U8> src(RAM_BUFFER_BYTES);
    std::vector<U8> dst(RAM_BUFFER_BYTES);
    for (size_t i = 0; i < RAM_BUFFER_BYTES; ++i)
    {
        src[i] = (U8)(i & 0xFF);
    }

    LLTimer timer;
    U64 totalBytes = 0;
    while (!isQuitting() && timer.getElapsedTimeF64() < RAM_TEST_SECONDS)
    {
        memcpy(dst.data(), src.data(), RAM_BUFFER_BYTES);
        totalBytes += RAM_BUFFER_BYTES;
        // Touch dst so the copy can't be proven dead and elided - swap
        // roles each pass instead of a separate "use" step.
        std::swap(src, dst);
    }

    F64 elapsed = timer.getElapsedTimeF64();
    F64 gbPerSec = elapsed > 0.0 ? ((F64)totalBytes / 1.0e9) / elapsed : 0.0;
    mMutex.lock();
    mRamGBPerSec = gbPerSec;
    mMutex.unlock();
}

void KVBenchThread::runDisk()
{
    // S24 : write-through, unbuffered I/O with sector-aligned buffers, so the figures
    // measure the drive and not the OS page cache that re-reading one file would hit.
    // 64 MiB is a multiple of any sector size.
    void* buffer = _aligned_malloc(DISK_BUFFER_BYTES, 4096);
    void* readBuf = _aligned_malloc(DISK_BUFFER_BYTES, 4096);
    if (!buffer || !readBuf)
    {
        _aligned_free(buffer);
        _aligned_free(readBuf);
        return;
    }
    U8* fill = static_cast<U8*>(buffer);
    for (size_t i = 0; i < DISK_BUFFER_BYTES; ++i)
    {
        fill[i] = (U8)((i * 2654435761u) & 0xFF);
    }

    const std::wstring path = utf8str_to_utf16str(benchTempFilePath());

    // Write phase: repeatedly (re)write the file for a fixed duration,
    // same "loop until elapsed" shape as the RAM test for a stable reading
    // rather than trusting a single write.
    {
        LLTimer timer;
        U64 totalBytes = 0;
        while (!isQuitting() && timer.getElapsedTimeF64() < DISK_TEST_SECONDS)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                break;
            }
            DWORD written = 0;
            const BOOL ok = WriteFile(file, buffer, (DWORD)DISK_BUFFER_BYTES, &written, nullptr);
            FlushFileBuffers(file);
            CloseHandle(file);
            if (!ok)
            {
                break;
            }
            totalBytes += written;
        }
        F64 elapsed = timer.getElapsedTimeF64();
        F64 mbPerSec = elapsed > 0.0 ? ((F64)totalBytes / 1.0e6) / elapsed : 0.0;
        mMutex.lock();
        mDiskWriteMBPerSec = mbPerSec;
        mMutex.unlock();
    }

    // Read phase: repeatedly read the same file back for a fixed duration.
    {
        LLTimer timer;
        U64 totalBytes = 0;
        while (!isQuitting() && timer.getElapsedTimeF64() < DISK_TEST_SECONDS)
        {
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                break;
            }
            DWORD bytesRead = 0;
            const BOOL ok = ReadFile(file, readBuf, (DWORD)DISK_BUFFER_BYTES, &bytesRead, nullptr);
            CloseHandle(file);
            if (!ok)
            {
                break;
            }
            totalBytes += bytesRead;
        }
        F64 elapsed = timer.getElapsedTimeF64();
        F64 mbPerSec = elapsed > 0.0 ? ((F64)totalBytes / 1.0e6) / elapsed : 0.0;
        mMutex.lock();
        mDiskReadMBPerSec = mbPerSec;
        mMutex.unlock();
    }

    DeleteFileW(path.c_str());
    _aligned_free(buffer);
    _aligned_free(readBuf);
}

// ---------------------------------------------------------------------------
// KVFloaterBench
// ---------------------------------------------------------------------------

KVFloaterBench::KVFloaterBench(const LLSD& key)
    : LLFloater(key)
{
}

KVFloaterBench::~KVFloaterBench()
{
}

bool KVFloaterBench::postBuild()
{
    mStatusText = getChild<LLTextBox>("status_text");
    mScoreText = getChild<LLTextBox>("score_text");
    mCompareText = getChild<LLTextBox>("compare_text");
    mDetailText = getChild<LLTextBox>("detail_text");
    mDetailText2 = getChild<LLTextBox>("detail_text2");
    mGfxPanel = getChild<LLPanel>("gfx_panel");

    mRunBtn = getChild<LLButton>("run_btn");
    mRunBtn->setClickedCallback(boost::bind(&KVFloaterBench::onRunClicked, this));

    resetDisplay();

    return LLFloater::postBuild();
}

void KVFloaterBench::onClose(bool app_quitting)
{
    if (mWorkerThread)
    {
        mWorkerThread->shutdown();
        mWorkerThread.reset();
    }
    // S24: never leave the world hidden if the floater is closed mid-test -
    // the pushRenderTypeMask() in startGraphicsTest() must always be
    // balanced by exactly one pop, whichever path gets there first.
    if (mGraphicsBenchActive)
    {
        mGraphicsBenchActive = false;
        DXPipeline::popRenderTypeMask(gPipeline);
    }
}

void KVFloaterBench::resetDisplay()
{
    setStatus("Click Run Benchmark to start.");
    if (mScoreText) mScoreText->setText(LLStringExplicit("--- Kmarks"));
    if (mCompareText) mCompareText->setText(LLStringExplicit(""));
    if (mDetailText) mDetailText->setText(LLStringExplicit(""));
    if (mDetailText2) mDetailText2->setText(LLStringExplicit(""));
}

void KVFloaterBench::onRunClicked()
{
    if (mRunning)
    {
        return;
    }
    mRunning = true;
    mRunBtn->setEnabled(false);
    if (mScoreText) mScoreText->setText(LLStringExplicit("--- Kmarks"));
    if (mCompareText) mCompareText->setText(LLStringExplicit(""));
    if (mDetailText) mDetailText->setText(LLStringExplicit(""));
    if (mDetailText2) mDetailText2->setText(LLStringExplicit(""));
    setStatus("Running: Math...");

    mWorkerThread = std::make_unique<KVBenchThread>();
    mWorkerThread->start();
}

void KVFloaterBench::startGraphicsTest()
{
    setStatus("Running: Graphics...");
    mGraphicsBenchActive = true;
    mGraphicsBenchFrames = 0;
    mGraphicsBenchTimer.reset();

    // S24: hides the whole 3D world (sky/terrain/avatars/everything) for
    // the few seconds this sub-test runs, so the result reflects this
    // floater's own draw work rather than whatever happens to be in view -
    // same push/clear-all/pop shape llsphereprobes.cpp uses around its own
    // render-type-sensitive work. The floater's own UI is untouched (XUI/
    // LLView draws through a separate path from gPipeline's draw pools).
    DXPipeline::pushRenderTypeMask(gPipeline);
    DXPipeline::clearAllRenderTypes(gPipeline);
}

void KVFloaterBench::setStatus(const std::string& text)
{
    if (mStatusText)
    {
        mStatusText->setText(LLStringExplicit(text));
    }
}

void KVFloaterBench::finishAndScore()
{
    F64 mathOps = mWorkerThread->getMathOpsPerSec();
    F64 ramGB = mWorkerThread->getRamGBPerSec();
    F64 diskMB = mWorkerThread->getDiskMBPerSec();
    F64 diskWriteMB = mWorkerThread->getDiskWriteMBPerSec();
    F64 diskReadMB = mWorkerThread->getDiskReadMBPerSec();

    F64 mathScore = (mathOps / REF_MATH_OPS_PER_SEC) * 250.0;
    F64 ramScore = (ramGB / REF_RAM_GB_PER_SEC) * 250.0;
    F64 diskScore = (diskMB / REF_DISK_MB_PER_SEC) * 250.0;
    F64 gfxScore = (mGfxFps / REF_GFX_FPS) * 250.0;

    F64 total = mathScore + ramScore + diskScore + gfxScore;
    F64 pct = ((total - 1000.0) / 1000.0) * 100.0;

    if (mScoreText)
    {
        mScoreText->setText(LLStringExplicit(llformat("%d Kmarks", (S32)ll_round((float)total))));
    }
    if (mCompareText)
    {
        if (pct >= 0.0)
        {
            mCompareText->setText(LLStringExplicit(
                llformat("%.1f%% faster than Kirsten's reference rig", pct)));
        }
        else
        {
            mCompareText->setText(LLStringExplicit(
                llformat("%.1f%% slower than Kirsten's reference rig", -pct)));
        }
    }
    if (mDetailText)
    {
        mDetailText->setText(LLStringExplicit(llformat(
            "Math %s ops/s   RAM %.1f GB/s", formatRate(mathOps).c_str(), ramGB)));
    }
    if (mDetailText2)
    {
        mDetailText2->setText(LLStringExplicit(llformat(
            "Disk %.0f MB/s (w%.0f r%.0f)   Gfx %.0f fps", diskMB, diskWriteMB, diskReadMB, mGfxFps)));
    }

    setStatus("Done. Click Run Benchmark to try again.");
    mRunBtn->setEnabled(true);
    mRunning = false;
    mWorkerThread.reset();
}

void KVFloaterBench::draw()
{
    LLFloater::draw();

    if (mWorkerThread && !mGraphicsBenchActive)
    {
        if (mWorkerThread->isFinished())
        {
            startGraphicsTest();
        }
        else
        {
            static const char* const kStageNames[] = { "Math", "RAM", "Disk" };
            S32 stage = mWorkerThread->getStage();
            setStatus(llformat("Running: %s...", kStageNames[llclamp(stage, 0, 2)]));
        }
    }

    if (mGraphicsBenchActive && mGfxPanel)
    {
        LLRect panelRect;
        mGfxPanel->localRectToOtherView(mGfxPanel->getLocalRect(), &panelRect, this);

        // S24: a fixed-size heavy quad batch every frame - the simplest
        // meaningful fill-rate + draw-call-overhead stress achievable with
        // this floater's own raw-draw tools. Positions/colors vary per
        // frame (based on the frame counter) purely so the GPU can't coast
        // on an identical, cacheable draw - the actual work is real.
        S32 w = llmax(panelRect.getWidth(), 1);
        S32 h = llmax(panelRect.getHeight(), 1);
        for (S32 i = 0; i < GFX_QUADS_PER_FRAME; ++i)
        {
            S32 seed = mGraphicsBenchFrames * GFX_QUADS_PER_FRAME + i;
            S32 qx = panelRect.mLeft + (seed * 37) % w;
            S32 qy = panelRect.mBottom + (seed * 53) % h;
            S32 qw = 20 + (seed % 20);
            S32 qh = 20 + ((seed * 7) % 20);
            F32 r = (F32)((seed * 3) & 0xFF) / 255.f;
            F32 g = (F32)((seed * 5) & 0xFF) / 255.f;
            F32 b = (F32)((seed * 11) & 0xFF) / 255.f;
            dx_rect_2d(LLRect(qx, qy + qh, qx + qw, qy), LLColor4(r, g, b, 0.6f));
        }

        ++mGraphicsBenchFrames;
        F64 elapsed = mGraphicsBenchTimer.getElapsedTimeF64();
        if (elapsed >= GFX_TEST_SECONDS)
        {
            mGfxFps = elapsed > 0.0 ? (F64)mGraphicsBenchFrames / elapsed : 0.0;
            mGraphicsBenchActive = false;
            DXPipeline::popRenderTypeMask(gPipeline);
            finishAndScore();
        }
    }
}
