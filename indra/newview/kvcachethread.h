/**
 * @file kvcachethread.h
 * @brief Dedicated worker thread for KVRAMCache operations
 *
 * Handles async texture acceptance, eviction, and cache management
 * without blocking the main rendering thread.
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
// KVCacheThread - Dedicated worker thread for RAM cache operations
//
// Priority: Below decode, above background tasks
// Purpose: Non-blocking texture acceptance, eviction, stats updates
//============================================================================

class KVCacheThread : public LLQueuedThread
{
public:
    // Request types
    enum RequestType
    {
        REQ_ACCEPT_EVICTION = 0,  // Accept texture from GPU
        REQ_PROCESS_EVICTION = 1,  // Process passive decay eviction
        REQ_UPDATE_STATS = 2       // Update cache statistics
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

    // Accept texture from GPU eviction
    class AcceptEvictionRequest : public CacheRequest
    {
    public:
        AcceptEvictionRequest(handle_t handle,
                              const LLUUID& uuid,
                              void* texture_data,
                              U64 size_bytes,
                              S32 discard_level,
                              U32 width,
                              U32 height,
                              S8 components);

        virtual ~AcceptEvictionRequest();

        bool processRequest() override;

    private:
        LLUUID mUUID;
        void* mTextureData;
        U64 mSizeBytes;
        S32 mDiscardLevel;
        U32 mWidth;
        U32 mHeight;
        S8 mComponents;
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
    handle_t queueAcceptEviction(const LLUUID& uuid,
                                  void* texture_data,
                                  U64 size_bytes,
                                  S32 discard_level,
                                  U32 width,
                                  U32 height,
                                  S8 components);

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
