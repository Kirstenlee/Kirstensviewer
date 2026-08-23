/**
 * @file kvcachethread.cpp
 * @brief Implementation of dedicated KVRAMCache worker thread
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Kirstens S24 Viewer Source Code
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "kvcachethread.h"
#include "kvramcache.h"
#include "lltimer.h"

//============================================================================
// Static initialization
//============================================================================
U32 KVCacheThread::sNextHandle = 1;

//============================================================================
// CacheRequest base class
//============================================================================
KVCacheThread::CacheRequest::CacheRequest(handle_t handle, RequestType type, U32 flags)
:   QueuedRequest(handle, flags | FLAG_AUTO_COMPLETE | FLAG_AUTO_DELETE),
    mType(type)
{
}

//============================================================================
// ProcessEvictionRequest
//============================================================================
KVCacheThread::ProcessEvictionRequest::ProcessEvictionRequest(handle_t handle, F32 delta_time)
:   CacheRequest(handle, REQ_PROCESS_EVICTION),
    mDeltaTime(delta_time)
{
}

bool KVCacheThread::ProcessEvictionRequest::processRequest()
{
    // Call passive eviction logic on worker thread
    KVRAMCache& cache = KVRAMCache::instance();
    cache.processPassiveEviction(mDeltaTime);
    return true;
}

//============================================================================
// KVCacheThread main class
//============================================================================
KVCacheThread::KVCacheThread()
:   LLQueuedThread("KVCache", false), // Not single-threaded
    mLastEvictionTime(0.0f)
{
}

KVCacheThread::~KVCacheThread()
{
    shutdown();
}

void KVCacheThread::start()
{
    if (!isQuitting())
    {
        LLThread::start();
        mLastEvictionTime = LLTimer::getElapsedSeconds();
    }
}

void KVCacheThread::shutdown()
{
    if (!isQuitting())
    {
        setQuitting();
        unpause();
    }

    // Wait for thread to finish
    while (!isStopped())
    {
        ms_sleep(10);
    }
}

void KVCacheThread::queueProcessEviction(F32 delta_time)
{
    // Only queue if enough time has passed
    LLMutexLock lock(&mUpdateMutex);

    handle_t handle = sNextHandle++;
    ProcessEvictionRequest* req = new ProcessEvictionRequest(handle, delta_time);
    addRequest(req);
}

size_t KVCacheThread::update(F32 max_time_ms)
{
    // Called from main thread - process completed requests
    return LLQueuedThread::update(max_time_ms);
}

void KVCacheThread::threadedUpdate()
{
    // Worker thread idle/update logic - base class handles request processing
    // We don't need additional logic here since requests are self-contained
}
