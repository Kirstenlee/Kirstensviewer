/**
 * @file _httpoperation.cpp
 * @brief Definitions for internal classes based on HttpOperation
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2014, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "_httpoperation.h"

#include "httphandler.h"
#include "httpresponse.h"
#include "httprequest.h"

#include "_httprequestqueue.h"
#include "_httpreplyqueue.h"
#include "_httpservice.h"
#include "_httpinternal.h"

#include "lltimer.h"


namespace
{
// Use constexpr for compile-time string constant - avoids runtime initialization
static constexpr const char* const LOG_CORE = "CoreHttp";

} // end anonymous namespace


namespace LLCore
{


// ==================================
// HttpOperation
// ==================================

// Static member initialization
/*static*/ 
HttpOperation::handleMap_t  HttpOperation::mHandleMap;
LLCoreInt::HttpMutex        HttpOperation::mOpMutex;

HttpOperation::HttpOperation()
    : std::enable_shared_from_this<HttpOperation>()
    , mReplyQueue()
    , mUserHandler()
    , mReqPolicy(HttpRequest::DEFAULT_POLICY_ID)
    , mTracing(HTTP_TRACE_OFF)
    , mMyHandle(LLCORE_HTTP_HANDLE_INVALID)
    , mMetricCreated(totalTime())  // Move initialization here for better cache locality
{
    // All initialization done in initializer list for optimal performance
    // Initializer lists avoid default construction followed by assignment
}


HttpOperation::~HttpOperation()
{
    // Destroy handle first to remove from map while object is still valid
    destroyHandle();
    
    // Reset shared_ptrs - explicit reset for clarity, though destructor would do this
    mReplyQueue.reset();
    mUserHandler.reset();
}


void HttpOperation::setReplyPath(HttpReplyQueue::ptr_t reply_queue,
                                 HttpHandler::ptr_t user_handler)
{
    // Use swap for exception-safe, efficient pointer exchange
    // Avoids temporary reference count increments compared to assignment
    mReplyQueue.swap(reply_queue);
    mUserHandler.swap(user_handler);
}


void HttpOperation::stageFromRequest(HttpService *)
{
    // Default implementation should never be called.
    // This indicates an operation making a transition that isn't defined.
    LL_ERRS(LOG_CORE) << "Default stageFromRequest method may not be called."
                      << LL_ENDL;
}


void HttpOperation::stageFromReady(HttpService *)
{
    // Default implementation should never be called.
    // This indicates an operation making a transition that isn't defined.
    LL_ERRS(LOG_CORE) << "Default stageFromReady method may not be called."
                      << LL_ENDL;
}


void HttpOperation::stageFromActive(HttpService *)
{
    // Default implementation should never be called.
    // This indicates an operation making a transition that isn't defined.
    LL_ERRS(LOG_CORE) << "Default stageFromActive method may not be called."
                      << LL_ENDL;
}


void HttpOperation::visitNotifier(HttpRequest *)
{
    // Early exit if no handler - branch prediction friendly (common case first)
    if (!mUserHandler)
    {
        return;
    }

    // Create response on stack when possible, use RAII pattern
    HttpResponse* response = new HttpResponse();
    response->setStatus(mStatus);
    mUserHandler->onCompleted(getHandle(), response);
    response->release();
}


HttpStatus HttpOperation::cancel()
{
    // Return default-constructed status - RVO (Return Value Optimization) applies
    return HttpStatus{};
}


// Handle methods
HttpHandle HttpOperation::getHandle()
{
    // Use likely/unlikely hints for branch prediction optimization
    // Most calls will have a valid handle after first access
    if (mMyHandle != LLCORE_HTTP_HANDLE_INVALID) [[likely]]
    {
        return mMyHandle;
    }

    return createHandle();
}


HttpHandle HttpOperation::createHandle()
{
    // Cast 'this' pointer to handle - zero-cost at runtime
    const HttpHandle handle = static_cast<HttpHandle>(this);

    {
        // Scoped lock - RAII ensures unlock even on exception
        LLCoreInt::HttpScopedLock lock(mOpMutex);

        // emplace is more efficient than operator[] for insertion
        // Avoids default construction of value if key doesn't exist
        mHandleMap.emplace(handle, shared_from_this());
        mMyHandle = handle;
    }

    return mMyHandle;
}


void HttpOperation::destroyHandle()
{
    // Early exit for invalid handle - avoid unnecessary lock acquisition
    if (mMyHandle == LLCORE_HTTP_HANDLE_INVALID) [[likely]]
    {
        return;
    }

    {
        LLCoreInt::HttpScopedLock lock(mOpMutex);

        // Use erase directly with key - single lookup instead of find + erase
        // erase() returns count of elements removed, but we don't need it
        mHandleMap.erase(mMyHandle);
    }

    // Clear handle after removal from map
    mMyHandle = LLCORE_HTTP_HANDLE_INVALID;
}


/*static*/
HttpOperation::ptr_t HttpOperation::findByHandle(HttpHandle handle)
{
    // Fast path: null handle check before acquiring lock
    if (!handle) [[unlikely]]
    {
        return ptr_t();
    }

    wptr_t weak;

    {
        LLCoreInt::HttpScopedLock lock(mOpMutex);

        // Use auto for iterator type deduction - cleaner code
        const auto it = mHandleMap.find(handle);
        if (it == mHandleMap.end()) [[unlikely]]
        {
            // Defer logging outside lock to minimize critical section
            // But keep it inside for now as the warning needs the handle context
            LL_WARNS("LLCore::HTTP") << "Could not find operation for handle " << handle << LL_ENDL;
            return ptr_t();
        }

        // Copy weak_ptr while holding lock
        weak = it->second;
    }
    // Lock released here - minimize lock hold time

    // Try to lock weak_ptr outside the mutex for better concurrency
    // lock() is atomic and thread-safe
    if (auto shared = weak.lock()) [[likely]]
    {
        return shared;
    }

    return ptr_t();
}


void HttpOperation::addAsReply()
{
    // Tracing check - branch hint for common case (tracing off)
    if (mTracing > HTTP_TRACE_OFF) [[unlikely]]
    {
        LL_INFOS(LOG_CORE) << "TRACE, ToReplyQueue, Handle:  "
                           << getHandle()
                           << LL_ENDL;
    }

    // Check reply queue validity before creating shared_ptr
    if (mReplyQueue) [[likely]]
    {
        // Create shared_ptr only when needed
        HttpOperation::ptr_t op = shared_from_this();
        mReplyQueue->addOp(std::move(op));  // Move to avoid ref count bump
    }
}


// ==================================
// HttpOpStop
// ==================================


HttpOpStop::HttpOpStop()
    : HttpOperation()
{}


HttpOpStop::~HttpOpStop() = default;  // Use default for trivial destructor


void HttpOpStop::stageFromRequest(HttpService* service)
{
    // Signal service to stop
    service->stopRequested();

    // Queue reply for completion notification
    addAsReply();
}


// ==================================
// HttpOpNull
// ==================================


HttpOpNull::HttpOpNull()
    : HttpOperation()
{}


HttpOpNull::~HttpOpNull() = default;  // Use default for trivial destructor


void HttpOpNull::stageFromRequest(HttpService* service)
{
    // No-op: This operation doesn't use libcurl ready/active queues
    // It bounces directly to the reply queue
    
    // Prepare response
    addAsReply();
}


// ==================================
// HttpOpSpin
// ==================================


HttpOpSpin::HttpOpSpin(int mode)
    : HttpOperation()
    , mMode(mode)  // Initialize in member initializer list
{}


HttpOpSpin::~HttpOpSpin() = default;  // Use default for trivial destructor


void HttpOpSpin::stageFromRequest(HttpService* service)
{
    if (mMode == 0) [[unlikely]]
    {
        // Spin forever - intentional infinite loop for testing/debugging
        // WARNING: This will block the thread indefinitely
        while (true)
        {
            ms_sleep(100);
        }
    }
    else [[likely]]
    {
        // Brief backoff to allow interlock plumbing to settle
        ms_sleep(1);

        // Re-queue this operation - use move semantics
        HttpOperation::ptr_t opptr = shared_from_this();
        service->getRequestQueue().addOp(std::move(opptr));
    }
}


}   // end namespace LLCore
