/**
 * @file   workqueue.cpp
 * @author Nat Goodspeed
 * @date   2021-10-06
 * @brief  Implementation for WorkQueue.
 * 
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (c) 2021, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "workqueue.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "llapp.h"
#include "llcoros.h"
#include LLCOROS_MUTEX_HEADER
#include "llerror.h"
#include "llevents.h"
#include "llexception.h"
#include "stringize.h"

using Mutex = LLCoros::Mutex;
using Lock  = LLCoros::LockType;

/*****************************************************************************
*   WorkQueueBase
*****************************************************************************/
LL::WorkQueueBase::WorkQueueBase(const std::string& name, bool auto_shutdown)
  : super(makeName(name))
{
    if (auto_shutdown)
{
        // Register for "LLApp" events so we can implicitly close() on viewer shutdown
        std::string listener_name = "WorkQueue:" + getKey();
        LLEventPumps::instance().obtain("LLApp").listen(
            listener_name,
            [this](const LLSD& stat)
            {
                std::string status(stat["status"]);
                if (status != "running")
                {
                    // Viewer is shutting down, close this queue
                    LL_DEBUGS("WorkQueue") << getKey() << " closing on app shutdown" << LL_ENDL;
                    close();
                }
                return false;
            });

        // Store the listener name so we can unregister in the destructor
        mListenerName = listener_name;
    }
}

LL::WorkQueueBase::~WorkQueueBase()
{
    if (!mListenerName.empty() && !LLEventPumps::wasDeleted())
    {
        LLEventPumps::instance().obtain("LLApp").stopListening(mListenerName);
}
}

void LL::WorkQueueBase::runUntilClose()
{
    try
    {
        for (;;)
        {
            callWork(pop_());
        }
    }
    catch (const Closed&)
    {
    }
}

bool LL::WorkQueueBase::runPending()
{
    for (Work work; tryPop_(work); )
    {
        callWork(work);
    }
    return ! done();
}

bool LL::WorkQueueBase::runOne()
{
    Work work;
    if (tryPop_(work))
    {
        callWork(work);
    }
    return ! done();
}

bool LL::WorkQueueBase::runUntil(const TimePoint& until)
{
    // Should we subtract some slop to allow for typical Work execution time?
    // How much slop?
    // runUntil() is simply a time-bounded runPending().
    for (Work work; TimePoint::clock::now() < until && tryPop_(work); )
    {
        callWork(work);
    }
    return ! done();
}

std::string LL::WorkQueueBase::makeName(const std::string& name)
{
    if (! name.empty())
        return name;

    static U32 discriminator = 0;
    static Mutex mutex;
    U32 num;
    {
        // Protect discriminator from concurrent access by different threads.
        // It can't be thread_local, else two racing threads will come up with
        // the same name.
        Lock lk(mutex);
        num = discriminator++;
    }
    return STRINGIZE("WorkQueue" << num);
}

namespace
{

    static const U32 STATUS_MSC_EXCEPTION = 0xE06D7363; // compiler specific

    // out_address is only settable here, during filter evaluation - Get
    // ExceptionInformation()'s result is invalid once inside the __except
    // body below, so the real fault address must be captured now or it's
    // lost (previously was: a real access-violation crash on a background
    // WorkQueue task converted to a bare "SEH, code: <n>" with no location,
    // making it undiagnosable from a crash dump - see feedback_s24_workqueue_seh_graceful_failure).
    U32 exception_filter(U32 code, struct _EXCEPTION_POINTERS* exception_infop, void*& out_address)
    {
         if (code == STATUS_MSC_EXCEPTION)
        {
            // C++ exception, go on
            return EXCEPTION_CONTINUE_SEARCH;
        }
        else
        {
            // handle it, convert to std::exception
            if (exception_infop && exception_infop->ExceptionRecord)
            {
                out_address = exception_infop->ExceptionRecord->ExceptionAddress;
            }
            return EXCEPTION_EXECUTE_HANDLER;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void cpphandle(const LL::WorkQueueBase::Work& work)
    {
        // SE and C++ can not coexists, thus two handlers
        try
        {
            work();
        }
        catch (const LLContinueError&)
        {
            // Any uncaught exception derived from LLContinueError will be caught
            // here and logged. This coroutine will terminate but the rest of the
            // viewer will carry on.
            LOG_UNHANDLED_EXCEPTION(STRINGIZE("LLContinue in work queue"));
        }
    }

    void sehandle(const LL::WorkQueueBase::Work& work)
    {
        U32 seh_code = 0;
        void* seh_address = nullptr;
        __try
        {
            // handle stop and continue exceptions first
            cpphandle(work);
        }
        __except (seh_code = GetExceptionCode(), exception_filter(seh_code, GetExceptionInformation(), seh_address))
        {
            // convert to C++ styled exception, with the real fault address
            // captured above so a caught-and-logged occurrence (see
            // callWork() below) is actually diagnosable.
            char integer_string[512];
            sprintf(integer_string, "SEH exception 0x%08lX at address %p", seh_code, seh_address);
            throw std::exception(integer_string);
        }
    }
} // anonymous namespace

void LL::WorkQueueBase::callWork(const Work& work)
{
    // S24: one queued work item throwing (including a genuine SEH exception
    // like an access violation, converted to a C++ exception by sehandle()
    // above) must not crash the whole process - log it and let the run loop
    // (runUntilClose()/runPending()/runOne()/runUntil()) move on to the next
    // item, same spirit as cpphandle()'s LLContinueError handling above.
    try
    {
        sehandle(work);
    }
    catch (const std::exception& e)
    {
        LL_WARNS("WorkQueue") << "Unhandled exception from queued work on \""
            << getKey() << "\": " << e.what() << LL_ENDL;
    }
}

void LL::WorkQueueBase::error(const std::string& msg)
{
    LL_ERRS("WorkQueue") << msg << LL_ENDL;
}

void LL::WorkQueueBase::checkCoroutine(const std::string& method)
{
    // By convention, the default coroutine on each thread has an empty name
    // string. See also LLCoros::logname().
    if (LLCoros::getName().empty())
    {
        LLTHROW(Error("Do not call " + method + " from a thread's default coroutine"));
    }
}

/*****************************************************************************
*   WorkQueue
*****************************************************************************/
LL::WorkQueue::WorkQueue(const std::string& name, size_t capacity, bool auto_shutdown):
    super(name, auto_shutdown),
    mQueue(capacity)
{
}

void LL::WorkQueue::close()
{
    mQueue.close();
}

size_t LL::WorkQueue::size()
{
    return mQueue.size();
}

bool LL::WorkQueue::isClosed()
{
    return mQueue.isClosed();
}

bool LL::WorkQueue::done()
{
    return mQueue.done();
}

bool LL::WorkQueue::post(const Work& callable)
{
    try
    {
    return mQueue.pushIfOpen(callable);
    }
    catch (std::bad_alloc&)
    {
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_ERRS("LLCoros") << "Bad memory allocation in WorkQueue::post" << LL_ENDL;
        return false;
    }
}

bool LL::WorkQueue::tryPost(const Work& callable)
{
    try
    {
    return mQueue.tryPush(callable);
    }
    catch (std::bad_alloc&)
    {
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_ERRS("LLCoros") << "Bad memory allocation in WorkQueue::tryPost" << LL_ENDL;
        return false;
    }
}

LL::WorkQueue::Work LL::WorkQueue::pop_()
{
    return mQueue.pop();
}

bool LL::WorkQueue::tryPop_(Work& work)
{
    return mQueue.tryPop(work);
}

/*****************************************************************************
*   WorkSchedule
*****************************************************************************/
LL::WorkSchedule::WorkSchedule(const std::string& name, size_t capacity, bool auto_shutdown):
    super(name, auto_shutdown),
    mQueue(capacity)
{
}

void LL::WorkSchedule::close()
{
    mQueue.close();
}

size_t LL::WorkSchedule::size()
{
    return mQueue.size();
}

bool LL::WorkSchedule::isClosed()
{
    return mQueue.isClosed();
}

bool LL::WorkSchedule::done()
{
    return mQueue.done();
}

bool LL::WorkSchedule::post(const Work& callable)
{
    // Use TimePoint::clock::now() instead of TimePoint's representation of
    // the epoch because this WorkSchedule may contain a mix of past-due
    // TimedWork items and TimedWork items scheduled for the future. Sift this
    // new item into the correct place.
    return post(callable, TimePoint::clock::now());
}

bool LL::WorkSchedule::post(const Work& callable, const TimePoint& time)
{
    return mQueue.pushIfOpen(TimedWork(time, callable));
}

bool LL::WorkSchedule::tryPost(const Work& callable)
{
    return tryPost(callable, TimePoint::clock::now());
}

bool LL::WorkSchedule::tryPost(const Work& callable, const TimePoint& time)
{
    return mQueue.tryPush(TimedWork(time, callable));
}

LL::WorkSchedule::Work LL::WorkSchedule::pop_()
{
    return std::get<0>(mQueue.pop());
}

bool LL::WorkSchedule::tryPop_(Work& work)
{
    return mQueue.tryPop(work);
}
