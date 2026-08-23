/**
 * @file kvcachethread.h
 * @brief Dedicated worker thread for KVRAMCache's passive eviction processing
 *
 * Handles async time/pressure-based decay eviction without blocking the
 * main rendering thread. (Texture acceptance - KVRAMCache::acceptEviction() -
 * is called synchronously from the fetch worker instead; it was never
 * routed through this thread.)
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Kirstens S24 Viewer Source Code
 * $/LicenseInfo$
 */

#ifndef KV_KVCACHETHREAD_H
#define KV_KVCACHETHREAD_H

#include "llqueuedthread.h"
#include "lluuid.h"
#include "llmutex.h"
#include <memory>

//============================================================================
// KVCacheThread - Dedicated worker thread for RAM cache passive eviction
//
// Priority: Below decode, above background tasks
// Purpose: Non-blocking time/pressure-based decay eviction
//============================================================================

class KVCacheThread : public LLQueuedThread
{
public:
    // Request types
    enum RequestType
    {
        REQ_PROCESS_EVICTION = 0  // Process passive decay eviction
    };

    // Base request class
    class CacheRequest : public QueuedRequest
    {
    protected:
        virtual ~CacheRequest() = default;

    public:
        CacheRequest(handle_t handle, RequestType type, U32 flags = 0);

        RequestType getRequestType() const { return mType; }

        // Override: worker thread calls this
        virtual bool processRequest() = 0;
        virtual void finishRequest(bool success) {};

    protected:
        RequestType mType;
    };

    // Process passive eviction (time-based decay)
    class ProcessEvictionRequest : public CacheRequest
    {
    public:
        ProcessEvictionRequest(handle_t handle, F32 delta_time);

        bool processRequest() override;

    private:
        F32 mDeltaTime;
    };

public:
    KVCacheThread();
    virtual ~KVCacheThread();

    // Thread control
    void start();
    void shutdown();

    // Queue requests (called from main thread)
    void queueProcessEviction(F32 delta_time);

    // Main thread callback
    size_t update(F32 max_time_ms);

protected:
    // Override LLQueuedThread (note: processRequest is not virtual, using base implementation)
    void threadedUpdate() override;

private:
    LLMutex mUpdateMutex;
    F32 mLastEvictionTime;

    static U32 sNextHandle;
};

#endif // KV_KVCACHETHREAD_H
