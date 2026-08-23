/**
 * @file llvertexbuffer.cpp
 * @brief LLVertexBuffer implementation
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "linden_common.h"

#include "llfasttimer.h"
#include "llsys.h"
#include "llvertexbuffer.h"
#include "llgl.h"

#include "llglheaders.h"
#include "llrender.h"
#include "llvector4a.h"
#include "llshadermgr.h"
#include "llglslshader.h"
#include "llmemory.h"
#include <glm/gtc/type_ptr.hpp>
#include <future>
#include <memory>

#ifdef DX_RENDER
#include "DXDevice.h"
#include "DXVertexLayout.h"
#endif

//Next Highest Power Of Two
//helper function, returns first number > v that is a power of 2, or v if v is already a power of 2
// S24 performance code bit manipulation avoiding loop
U32 nhpo2(U32 v)
{
	if (v == 0) return 1; // Special case when input is 0

	--v; // Subtract 1 to account for numbers that are already powers of 2
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16; // Works for up to 32-bit integers
	return v + 1;
}

//which power of 2 is i?
//assumes i is a power of 2 > 0
U32 wpo2(U32 i)
{
	llassert(i > 0);
	llassert(nhpo2(i) == i);

	return 31 - _lzcnt_u32(i);   // S24 - MSVC (with intrinsics for x86)
}

struct CompareMappedRegion
{
	bool operator()(const LLVertexBuffer::MappedRegion& lhs, const LLVertexBuffer::MappedRegion& rhs)
	{
		return lhs.mStart < rhs.mStart;
	}
};

// S24 - GL work queue for deferred GL calls (experimental). Was a compile-time
// ENABLE_GL_WORK_QUEUE #define fixed at THREAD_COUNT=2 threads; now toggled and sized at
// runtime via LLVertexBuffer::sVBOWorkQueueEnabled / sVBOWorkQueueThreadCount (settings
// S24VBOWorkQueueEnabled / S24VBOWorkQueueThreadCount, applied in settings_to_globals()).
// The class definitions below are always compiled in; only queue/thread creation in
// initClass() is gated at runtime, so the feature can be flipped without a rebuild.

//============================================================================
// High performance WorkQueue for usage in real-time rendering work
class GLWorkQueue
{
public:
	using Work = std::function<void()>;

	GLWorkQueue();

	void post(Work value);

	size_t size();

	bool done();

	// Get the next element from the queue
	Work pop();

	void runOne();

	bool runPending();

	void runUntilClose();

	void close();

	bool isClosed();

	void syncGL();

private:
	std::mutex mMutex;
	std::condition_variable mCondition;
	std::queue<Work> mQueue;
	bool mClosed = false;
	GLsync mSync = nullptr;
};

GLWorkQueue::GLWorkQueue()
{
}

void GLWorkQueue::syncGL()
{
	// S24: lock now taken before checking mSync, not just around the wait/clear - runOne()
	// writes mSync unlocked otherwise, a real data race the moment more than one thread
	// touches the queue at once (see runOne()).
	std::lock_guard<std::mutex> lock(mMutex);
	if (mSync)
	{
		glWaitSync(mSync, 0, GL_TIMEOUT_IGNORED);
		mSync = 0;
	}
}

size_t GLWorkQueue::size()
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mQueue.size();
}

bool GLWorkQueue::done()
{
	return size() == 0 && isClosed();
}

void GLWorkQueue::post(GLWorkQueue::Work value)
{
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mQueue.push(std::move(value));
	}

	mCondition.notify_one();
}

// Get the next element from the queue
GLWorkQueue::Work GLWorkQueue::pop()
{
	// Lock the mutex
	{
		std::unique_lock<std::mutex> lock(mMutex);

		// Wait for a new element to become available or for the queue to close
		{
			mCondition.wait(lock, [=] { return !mQueue.empty() || mClosed; });
		}
	}

	Work ret;

	{
		std::lock_guard<std::mutex> lock(mMutex);

		// Get the next element from the queue
		if (mQueue.size() > 0)
		{
			ret = mQueue.front();
			mQueue.pop();
		}
		else
		{
			ret = []() {};
		}
	}

	return ret;
}

void GLWorkQueue::runOne()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
	Work w = pop();
	w();

	// S24: mSync read/write now guarded by mMutex to match syncGL() - see comment there.
	std::lock_guard<std::mutex> lock(mMutex);

	// Clean up previous sync
	if (mSync)
	{
		glDeleteSync(mSync);
		mSync = nullptr;
	}

	// Explicitly place fence
	mSync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void GLWorkQueue::runUntilClose()
{
	while (!isClosed())
	{
		runOne();
	}
}

void GLWorkQueue::close()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		mClosed = true;
	}

	mCondition.notify_all();
}

bool GLWorkQueue::runPending()
{
	std::unique_lock<std::mutex> lock(mMutex);
	if (mQueue.empty())
		return false;

	Work w = std::move(mQueue.front());
	mQueue.pop();
	lock.unlock();

	w();  // Execute outside the lock
	return true;
}

bool GLWorkQueue::isClosed()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
	std::lock_guard<std::mutex> lock(mMutex);
	return mClosed;
}

#include "llwindow.h"

class LLGLWorkerThread : public LLThread
{
public:
	LLGLWorkerThread(const std::string& name, GLWorkQueue* queue, LLWindow* window)
		: LLThread(name)
	{
		mWindow = window;
		mContext = mWindow->createSharedContext();
		mQueue = queue;

		// S24: createSharedContext() can legitimately fail (driver refuses another shared
		// context, or a headless window backend - LLWindowHeadless::createSharedContext()
		// unconditionally returns nullptr). Without this, run() would call real GL functions
		// on a thread with no current context - undefined, driver-dependent behavior.
		if (!mContext)
		{
			LL_WARNS("VertexBuffer") << "Failed to create shared GL context for '" << name << "' - worker will not run" << LL_ENDL;
		}
	}

	void run() override
	{
		if (!mContext)
		{
			return;
		}

		mWindow->makeContextCurrent(mContext);
		gGL.init(false);
		mQueue->runUntilClose();
		gGL.shutdown();
		mWindow->destroySharedContext(mContext);
	}

	bool isValid() const { return mContext != nullptr; }

	GLWorkQueue* mQueue;
	LLWindow* mWindow;
	void* mContext = nullptr;
};

// S24: vector, not a fixed-size array - thread count is now runtime-configurable
// (LLVertexBuffer::sVBOWorkQueueThreadCount) instead of a compile-time THREAD_COUNT.
static std::vector<LLGLWorkerThread*> sVBOThreads;
static GLWorkQueue* sQueue = nullptr;

//============================================================================
// Pool of reusable VertexBuffer state

// S24 dynamic pool size based on VRAM
// Pool size tiers:
// - Standard: <8GB VRAM = 4096 buffers (legacy GPUs like GTX 960)
// - Enhanced: 8-24GB VRAM = 8192 buffers (modern mid-high GPUs like RTX 5060ti, RX 7800 XT)
// - Extreme: 24GB+ VRAM = 16384 buffers (high-end GPUs like RTX 4090, RX 7900XTX, A6000)
namespace
{
	constexpr U32 POOL_SIZE_STANDARD = 4096;
	constexpr U32 POOL_SIZE_ENHANCED = 8192;
	constexpr U32 POOL_SIZE_EXTREME = 16384;
	constexpr F32 VRAM_THRESHOLD_ENHANCED = 8192.0f;  // 8GB
	constexpr F32 VRAM_THRESHOLD_EXTREME = 24576.0f;  // 24GB
	constexpr F32 VRAM_MINIMUM_SAFE = 256.0f;         // 256MB minimum for iGPU/undetectable

	// Calculate pool size once based on detected VRAM
	U32 calculate_pool_size()
	{
		const F32 vram_mb = static_cast<F32>(gGLManager.mVRAM);

		// Handle undetectable or iGPU VRAM gracefully
		if (vram_mb <= 0.0f || vram_mb < VRAM_MINIMUM_SAFE)
		{
			// Fallback to a conservative pool size for safety
			return POOL_SIZE_STANDARD;
		}
		else if (vram_mb >= VRAM_THRESHOLD_EXTREME)
		{
			return POOL_SIZE_EXTREME;
		}
		else if (vram_mb >= VRAM_THRESHOLD_ENHANCED)
		{
			return POOL_SIZE_ENHANCED;
		}
		else
		{
			return POOL_SIZE_STANDARD;
		}
	}

	// Thread-local buffer pool with automatic cleanup
	struct BufferPool
	{
		GLuint* pool = nullptr;
		U32 index = 0;
		U32 size = 0;

		BufferPool()
		{
			size = calculate_pool_size();
			pool = new GLuint[size];
		}

		~BufferPool()
		{
			delete[] pool;
		}
	};
}

static GLuint gen_buffer()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

	GLuint ret = 0;

	// Use the thread-local BufferPool defined in the anonymous namespace so
	// pool size is determined at runtime by calculate_pool_size().
	thread_local static BufferPool sPool;

	// Fill the name pool the first time (or when we've exhausted it).
	if (sPool.index == 0)
	{
		LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("gen buffer");
		sPool.index = sPool.size;

		if (!gGLManager.mIsAMD)
		{
			glGenBuffers((GLsizei)sPool.size, sPool.pool);
		}
		else
		{ // work around for AMD driver bug
			for (U32 i = 0; i < sPool.size; ++i)
			{
				glGenBuffers(1, sPool.pool + i);
			}
		}
	}

	ret = sPool.pool[--sPool.index];
	return ret;
}

static void delete_buffers(S32 count, GLuint* buffers)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	// Wait a few frames before actually deleting buffers to avoid
	// synchronization issues with the GPU
	static std::vector<GLuint> sFreeList[4];

	if (gGLManager.mInited)
	{
		U32 idx = LLImageGL::sFrameCount % 4;

		for (S32 i = 0; i < count; ++i)
		{
			sFreeList[idx].push_back(buffers[i]);
		}

		idx = (LLImageGL::sFrameCount + 3) % 4;

		if (!sFreeList[idx].empty())
		{
            glDeleteBuffers((GLsizei)sFreeList[idx].size(), sFreeList[idx].data());
            sFreeList[idx].resize(0);
		}
	}
}


#define ANALYZE_VBO_POOL 0

// VBO Pool interface
class LLVBOPool
{
public:
	virtual ~LLVBOPool() = default;
	virtual void allocate(GLenum type, U32 size, GLuint& name, U8*& data) = 0;
	virtual void free(GLenum type, U32 size, GLuint name, U8* data) = 0;
	virtual U64 getVramBytesUsed() = 0;
};

// VBO Pool for Apple GPUs (as in M1/M2 etc, not Intel macs)
// Effectively disables VBO pooling
class LLAppleVBOPool final : public LLVBOPool
{
public:
	U64 mAllocated = 0;

	U64 getVramBytesUsed() override
	{
		return mAllocated;
	}

	void allocate(GLenum type, U32 size, GLuint& name, U8*& data) override
	{
		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
		STOP_GLERROR;
		llassert(type == GL_ARRAY_BUFFER || type == GL_ELEMENT_ARRAY_BUFFER);
		llassert(name == 0); // non zero name indicates a gl name that wasn't freed
		llassert(data == nullptr);  // non null data indicates a buffer that wasn't freed
		llassert(size >= 2);  // any buffer size smaller than a single index is nonsensical

		mAllocated += size;

		{ //allocate a new buffer
			LL_PROFILE_GPU_ZONE("vbo alloc");
			// ON OS X, we don't allocate a VBO until the last possible moment
			// in unmapBuffer
			data = (U8*)ll_aligned_malloc_16(size);
			STOP_GLERROR;
		}
	}

	void free(GLenum type, U32 size, GLuint name, U8* data) override
	{
		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
		llassert(type == GL_ARRAY_BUFFER || type == GL_ELEMENT_ARRAY_BUFFER);
		llassert(size >= 2);

		if (data)
		{
			ll_aligned_free_16(data);
		}

		mAllocated -= size;
		STOP_GLERROR;
		if (name)
		{
			delete_buffers(1, &name);
		}
		STOP_GLERROR;
	}
};

// VBO Pool for GPUs that benefit from VBO pooling
class LLDefaultVBOPool final : public LLVBOPool
{
public:
	typedef std::chrono::steady_clock::time_point Time;
	struct Entry
	{
		U8* mData;
		GLuint mGLName;
		Time mAge;
	};

	~LLDefaultVBOPool() override
	{
		clear();
	}

	typedef std::unordered_map<U32, std::list<Entry>> Pool;

	Pool mVBOPool;
	Pool mIBOPool;

	U32 mTouchCount = 0;

	U64 mDistributed = 0;
	U64 mAllocated = 0;
	U64 mReserved = 0;
	U32 mMisses = 0;
	U32 mHits = 0;

	U64 getVramBytesUsed() override
	{
		return mAllocated + mReserved;
	}

	// increase the size to some common value (e.g. a power of two) to increase hit rate
	void adjustSize(U32& size)
	{
		// size = nhpo2(size);  // (193/303)/580 MB (distributed/allocated)/reserved in VBO Pool. Overhead: 66 percent. Hit rate: 77 percent

		//(245/276)/385 MB (distributed/allocated)/reserved in VBO Pool. Overhead: 57 percent. Hit rate: 69 percent
		//(187/209)/397 MB (distributed/allocated)/reserved in VBO Pool. Overhead: 112 percent. Hit rate: 76 percent
		U32 block_size = llmax(nhpo2(size) / 8, (U32)16);
		size += block_size - (size % block_size);
	}

	void allocate(GLenum type, U32 size, GLuint& name, U8*& data) override
	{
		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
		llassert(type == GL_ARRAY_BUFFER || type == GL_ELEMENT_ARRAY_BUFFER);
		llassert(name == 0); // non zero name indicates a gl name that wasn't freed
		llassert(data == nullptr);  // non null data indicates a buffer that wasn't freed
		llassert(size >= 2);  // any buffer size smaller than a single index is nonsensical

		mDistributed += size;
		adjustSize(size);
		mAllocated += size;

		auto& pool = type == GL_ELEMENT_ARRAY_BUFFER ? mIBOPool : mVBOPool;

		Pool::iterator iter = pool.find(size);
		if (iter == pool.end())
		{ // cache miss, allocate a new buffer
			LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo pool miss");
			LL_PROFILE_GPU_ZONE("vbo alloc");

			mMisses++;
			name = gen_buffer();

			// S24: when the VBO work queue is live, defer this (data-less) storage
			// reservation to a worker's shared context. The buffer NAME is valid across the
			// whole share group immediately (glGenBuffers only reserves an integer), but the
			// storage allocation itself must be genuinely complete - not just "probably done
			// soon" - before this name is bound/written on the main thread.
			//
			// CRASH FIX: an earlier version of this used GLWorkQueue::syncGL(), which waits
			// on "whatever fence the queue currently holds", not a fence for THIS specific
			// job - post() returns before the worker is even guaranteed to have started.
			// The very first call after enabling the feature had no prior fence to wait on
			// at all, so syncGL() silently no-opped and the main thread went on to bind/draw
			// a buffer with no GPU storage behind it yet - undefined behavior that manifested
			// as an instant, log-less crash (GPU driver fault) on the first VBO pool miss of
			// the session. Fixed by fencing and waiting on THIS job specifically via a
			// promise/future, bypassing the queue's shared, job-agnostic mSync entirely.
			// This does mean the main thread genuinely blocks until the worker has issued the
			// GL commands - there is no way to hand this off as fire-and-forget without a
			// correctness gap, given the queue's single-fence design.
			if (sQueue && LLVertexBuffer::sVBOWorkQueueEnabled)
			{
				auto fence_promise = std::make_shared<std::promise<GLsync>>();
				std::future<GLsync> fence_future = fence_promise->get_future();

				sQueue->post([type, size, name, fence_promise]()
				{
					glBindBuffer(type, name);
					glBufferData(type, size, nullptr, GL_DYNAMIC_DRAW);
					fence_promise->set_value(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
				});

				try
				{
					GLsync fence = fence_future.get();
					if (fence)
					{
						glWaitSync(fence, 0, GL_TIMEOUT_IGNORED);
						glDeleteSync(fence);
					}
				}
				catch (const std::future_error&)
				{
					// S24: promise was destroyed without being fulfilled - only possible if
					// the queue was closed (shutdown) between post() and the worker picking
					// this job up. Nothing to wait on in that case; fall through.
				}
			}
			else
			{
				glBindBuffer(type, name);
				glBufferData(type, size, nullptr, GL_DYNAMIC_DRAW);

				// S24: only update the bind cache when THIS thread actually did the bind -
				// when deferred above, the main thread hasn't bound anything in its own
				// context yet and must still take the real bind path the first time it uses
				// this buffer (sGLRenderBuffer/sGLRenderIndices are thread_local; see header).
				if (type == GL_ELEMENT_ARRAY_BUFFER)
				{
					LLVertexBuffer::sGLRenderIndices = name;
				}
				else
				{
					LLVertexBuffer::sGLRenderBuffer = name;
				}
			}

			data = (U8*)ll_aligned_malloc_16(size);
		}
		else
		{
			mHits++;
			llassert(mReserved >= size); // assert if accounting gets messed up
			mReserved -= size;

			std::list<Entry>& entries = iter->second;
			Entry& entry = entries.back();
			name = entry.mGLName;
			data = entry.mData;

			entries.pop_back();
			if (entries.empty())
			{
				pool.erase(iter);
			}
		}

		clean();
	}

	void free(GLenum type, U32 size, GLuint name, U8* data) override
	{
		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
		llassert(type == GL_ARRAY_BUFFER || type == GL_ELEMENT_ARRAY_BUFFER);
		llassert(size >= 2);
		llassert(name != 0);
		llassert(data != nullptr);

		clean();

		llassert(mDistributed >= size);
		mDistributed -= size;
		adjustSize(size);
		llassert(mAllocated >= size);
		mAllocated -= size;
		mReserved += size;

		auto& pool = type == GL_ELEMENT_ARRAY_BUFFER ? mIBOPool : mVBOPool;

		Pool::iterator iter = pool.find(size);

		if (iter == pool.end())
		{
			std::list<Entry> newlist;
			newlist.push_front({ data, name, std::chrono::steady_clock::now() });
			pool[size] = newlist;
		}
		else
		{
			iter->second.push_front({ data, name, std::chrono::steady_clock::now() });
		}
	}

	// clean periodically (clean gets called for every alloc/free)
	void clean()
	{
		mTouchCount++;
		if (mTouchCount < 1024) // clean every 1k touches
		{
			return;
		}
		mTouchCount = 0;

		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

		std::unordered_map<U32, std::list<Entry>>* pools[] = { &mVBOPool, &mIBOPool };

		using namespace std::chrono_literals;

		Time cutoff = std::chrono::steady_clock::now() - 5s;

		for (auto* pool : pools)
		{
			for (Pool::iterator iter = pool->begin(); iter != pool->end(); )
			{
				auto& entries = iter->second;

				while (!entries.empty() && entries.back().mAge < cutoff)
				{
					LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vbo cache timeout");
					auto& entry = entries.back();
					ll_aligned_free_16(entry.mData);
					delete_buffers(1, &entry.mGLName);
					llassert(mReserved >= iter->first);
					mReserved -= iter->first;
					entries.pop_back();

				}

				if (entries.empty())
				{
					iter = pool->erase(iter);
				}
				else
				{
					++iter;
				}
			}
		}

	}

	void clear()
	{
		for (auto& entries : mIBOPool)
		{
			for (auto& entry : entries.second)
			{
				ll_aligned_free_16(entry.mData);
				delete_buffers(1, &entry.mGLName);
			}
		}

		for (auto& entries : mVBOPool)
		{
			for (auto& entry : entries.second)
			{
				ll_aligned_free_16(entry.mData);
				delete_buffers(1, &entry.mGLName);
			}
		}

		mReserved = 0;

		mIBOPool.clear();
		mVBOPool.clear();
	}
};

static LLVBOPool* sVBOPool = nullptr;

void LLVertexBufferData::drawWithMatrix()
{
	if (!mVB)
	{
		llassert(false);
		// Not supposed to happen, check buffer generation
		return;
	}

#ifdef DX_RENDER
	// S24 (task #54): see LLVertexBufferData::mDXImage's comment - replay via
	// a real bind(LLImageGL*) instead of GL's raw-GLuint bindManual(mTexName).
	if (mDXImage)
	{
		gGL.getTexUnit(0)->bind(mDXImage.get());
	}
	else
	{
		gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
#else
	if (mTexName)
	{
		gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, mTexName);
	}
	else
	{
		gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
#endif

	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.pushMatrix();
	gGL.loadMatrix(glm::value_ptr(mModelView));
	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.pushMatrix();
	gGL.loadMatrix(glm::value_ptr(mProjection));
	gGL.matrixMode(LLRender::MM_TEXTURE0);
	gGL.pushMatrix();
	gGL.loadMatrix(glm::value_ptr(mTexture0));

	mVB->setBuffer();
	mVB->drawArrays(mMode, 0, mCount);

	gGL.popMatrix();
	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.popMatrix();
	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.popMatrix();
}

void LLVertexBufferData::draw()
{
	if (!mVB)
	{
		llassert(false);
		// Not supposed to happen, check buffer generation
		return;
	}

#ifdef DX_RENDER
	// S24 (task #54): see LLVertexBufferData::mDXImage's comment - replay via
	// a real bind(LLImageGL*) instead of GL's raw-GLuint bindManual(mTexName).
	if (mDXImage)
	{
		gGL.getTexUnit(0)->bind(mDXImage.get());
	}
	else
	{
		gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
#else
	if (mTexName)
	{
		gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, mTexName);
	}
	else
	{
		gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
	}
#endif

	mVB->setBuffer();
	mVB->drawArrays(mMode, 0, mCount);
}

//============================================================================

//static
U64 LLVertexBuffer::getBytesAllocated()
{
	return sVBOPool ? sVBOPool->getVramBytesUsed() : 0;
}

//============================================================================
//
//static
thread_local U32 LLVertexBuffer::sGLRenderBuffer = 0;
thread_local U32 LLVertexBuffer::sGLRenderIndices = 0;
#ifdef DX_RENDER
thread_local ID3D11Buffer* LLVertexBuffer::sDXRenderBuffer = nullptr;
thread_local ID3D11Buffer* LLVertexBuffer::sDXRenderIndices = nullptr;
thread_local LLGLSLShader* LLVertexBuffer::sDXLastShader = nullptr;
#endif

// S24: set from settings_to_globals() in llappviewer.cpp - see llvertexbuffer.h for why
// llrender can't read gSavedSettings directly (mirrors LLRender::sGLCoreProfile).
bool LLVertexBuffer::sVBOWorkQueueEnabled = false;
U32 LLVertexBuffer::sVBOWorkQueueThreadCount = 2;
U32 LLVertexBuffer::sLastMask = 0;
U32 LLVertexBuffer::sVertexCount = 0;

//NOTE: each component must be AT LEAST 4 bytes in size to avoid a performance penalty on AMD hardware
const U32 LLVertexBuffer::sTypeSize[LLVertexBuffer::TYPE_MAX] =
{
	sizeof(LLVector4), // TYPE_VERTEX,
	sizeof(LLVector4), // TYPE_NORMAL,
	sizeof(LLVector2), // TYPE_TEXCOORD0,
	sizeof(LLVector2), // TYPE_TEXCOORD1,
	sizeof(LLVector2), // TYPE_TEXCOORD2,
	sizeof(LLVector2), // TYPE_TEXCOORD3,
	sizeof(LLColor4U), // TYPE_COLOR,
	sizeof(LLColor4U), // TYPE_EMISSIVE, only alpha is used currently
	sizeof(LLVector4), // TYPE_TANGENT,
	sizeof(F32),       // TYPE_WEIGHT,
	sizeof(LLVector4), // TYPE_WEIGHT4,
	sizeof(LLVector4), // TYPE_CLOTHWEIGHT,
	sizeof(U64),       // TYPE_JOINT,
	sizeof(LLVector4), // TYPE_TEXTURE_INDEX (actually exists as position.w), no extra data, but stride is 16 bytes
};

static const std::string vb_type_name[] =
{
	"TYPE_VERTEX",
	"TYPE_NORMAL",
	"TYPE_TEXCOORD0",
	"TYPE_TEXCOORD1",
	"TYPE_TEXCOORD2",
	"TYPE_TEXCOORD3",
	"TYPE_COLOR",
	"TYPE_EMISSIVE",
	"TYPE_TANGENT",
	"TYPE_WEIGHT",
	"TYPE_WEIGHT4",
	"TYPE_CLOTHWEIGHT",
	"TYPE_JOINT"
	"TYPE_TEXTURE_INDEX",
	"TYPE_MAX",
	"TYPE_INDEX",
};

const U32 LLVertexBuffer::sGLMode[LLRender::NUM_MODES] =
{
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_LINE_LOOP,
};

//static
void LLVertexBuffer::setupClientArrays(U32 data_mask)
{
	if (sLastMask != data_mask)
	{
		for (U32 i = 0; i < TYPE_MAX; ++i)
		{
			S32 loc = i;

			U32 mask = 1 << i;

			if (sLastMask & (1 << i))
			{ //was enabled
				if (!(data_mask & mask))
				{ //needs to be disabled
					glDisableVertexAttribArray(loc);
				}
			}
			else
			{   //was disabled
				if (data_mask & mask)
				{ //needs to be enabled
					glEnableVertexAttribArray(loc);
				}
			}
		}
	}

	sLastMask = data_mask;
}

//static
void LLVertexBuffer::drawArrays(U32 mode, const std::vector<LLVector3>& pos)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	gGL.begin(mode);
	for (auto& v : pos)
	{
		gGL.vertex3fv(v.mV);
	}
	gGL.end();
	gGL.flush();
}

//static
void LLVertexBuffer::drawElements(U32 mode, const LLVector4a* pos, const LLVector2* tc, U32 num_indices, const U16* indicesp)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	llassert(LLGLSLShader::sCurBoundShaderPtr != NULL);

	STOP_GLERROR;

	gGL.syncMatrices();

	unbind();

	gGL.begin(mode);

	if (tc != nullptr)
	{
		for (U32 i = 0; i < num_indices; ++i)
		{
			U16 idx = indicesp[i];
			gGL.texCoord2fv(tc[idx].mV);
			gGL.vertex3fv(pos[idx].getF32ptr());
		}
	}
	else
	{
		for (U32 i = 0; i < num_indices; ++i)
		{
			U16 idx = indicesp[i];
			gGL.vertex3fv(pos[idx].getF32ptr());
		}
	}
	gGL.end();
	gGL.flush();
}

bool LLVertexBuffer::validateRange(U32 start, U32 end, U32 count, U32 indices_offset) const
{
	if (!gDebugGL)
	{
		return true;
	}

	llassert(start < mNumVerts);
	llassert(end < mNumVerts);

	if (start >= mNumVerts ||
		end >= mNumVerts)
	{
		LL_ERRS() << "Bad vertex buffer draw range: [" << start << ", " << end << "] vs " << mNumVerts << LL_ENDL;
	}

	if (indices_offset >= mNumIndices ||
		indices_offset + count > mNumIndices)
	{
		LL_ERRS() << "Bad index buffer draw range: [" << indices_offset << ", " << indices_offset + count << "]" << LL_ENDL;
	}

	{
#if 0  // not a reliable test for VBOs that are not backed by a CPU buffer
		U16* idx = (U16*)mMappedIndexData + indices_offset;
		for (U32 i = 0; i < count; ++i)
		{
			llassert(idx[i] >= start);
		 llassert(idx[i] <= end);

			if (idx[i] < start || idx[i] > end)
			{
				LL_ERRS() << "Index out of range: " << idx[i] << " not in [" << start << ", " << end << "]" << LL_ENDL;
			}
		}

		LLVector4a* v = (LLVector4a*)mMappedData;

		for (U32 i = start; i <= end; ++i)
		{
			if (!v[i].isFinite3())
			{
				LL_ERRS() << "Non-finite vertex position data detected." << LL_ENDL;
			}
		}

		LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;

		if (shader && shader->mFeatures.mIndexedTextureChannels > 1)
		{
			LLVector4a* v = (LLVector4a*)mMappedData;

			for (U32 i = start; i < end; i++)
			{
				U32 idx = (U32)(v[i][3] + 0.25f);
				if (idx >= (U32)shader->mFeatures.mIndexedTextureChannels)
				{
					LL_ERRS() << "Bad texture index found in vertex data stream." << LL_ENDL;
				}
			}
		}
#endif
	}

	return true;
}

#if LL_PROFILER_ENABLE_RENDER_DOC
void LLVertexBuffer::setLabel(const char* label) {
	LL_LABEL_OBJECT_GL(GL_BUFFER, mGLBuffer, strlen(label), label);
}
#endif

void LLVertexBuffer::clone(LLVertexBuffer& target) const
{
	target.mTypeMask = mTypeMask;
	target.mIndicesType = mIndicesType;
	target.mIndicesStride = mIndicesStride;
	if (target.getNumVerts() != getNumVerts() ||
		target.getNumIndices() != getNumIndices())
	{
		target.allocateBuffer(getNumVerts(), getNumIndices());
	}
}

#ifdef DX_RENDER
namespace
{
	// D3D11 has no equivalent of GL_TRIANGLE_FAN or GL_LINE_LOOP (dropped
	// after D3D9) - mapped to D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED and asserted
	// against below. Neither is used by the base (unrigged, non-UI) geometry
	// converted so far; revisit if/when a pool that needs one is converted.
	const D3D11_PRIMITIVE_TOPOLOGY sDXMode[LLRender::NUM_MODES] =
	{
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // TRIANGLES
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, // TRIANGLE_STRIP
		D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,     // TRIANGLE_FAN - not yet supported
		D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,     // POINTS
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST,      // LINES
		D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,     // LINE_STRIP
		D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,     // LINE_LOOP - not yet supported
	};

	// S24 (2026-08-02): the true universal DX_RENDER draw chokepoint - every
	// 3D pool draw, every LLRender::flush() immediate-mode caller, and every
	// direct drawRange()/drawArrays()/drawRangeFast() caller (e.g. the BRDF
	// LUT bake) funnels through here. Until now none of the three re-asserted
	// VS/PS at all - both LLGLSLShader::bind()'s sCurBoundShaderPtr-based
	// early-out (llglslshader.cpp:1229) AND syncMatrices()'s own constant-
	// buffer selection (llrender.cpp:1320) are driven by the SAME tracker, so
	// a raw ctx->VSSetShader()/PSSetShader() call anywhere that doesn't
	// update that tracker (two real, historical instances already found and
	// fixed this session: dxpipeline.cpp's presentDeferredScreen(), the
	// llviewerwindow.cpp fresh-shader font test) leaves every draw here
	// running under stale GPU-bound shaders with zero warning. DXUIBatch's
	// own flush() was hardened the same way earlier - this closes the same
	// class at the actual universal chokepoint instead of just the 2D UI
	// batch path.
	//
	// Cache-and-compare, not unconditional rebind: D3D11 SetShader calls are
	// cheap but not free at this call frequency (every 3D draw, every frame).
	// Mirrors the exact skip-when-unchanged invariant LLGLSLShader::bind()'s
	// own early-out already protects, just enforced at the real Draw() point
	// instead of trusting that bind() was both called AND that nothing raw-
	// bound anything in between.
	ID3D11VertexShader* sLastBoundVS = nullptr;
	ID3D11PixelShader* sLastBoundPS = nullptr;

	// S24 (2026-08-02): TEMPORARY - see getAndResetDXDrawCallCount()'s
	// header comment. Incremented by every real ctx->Draw()/DrawIndexed()
	// call below.
	U32 sDXDrawCallCount = 0;

	// If sCurBoundShaderPtr is null, leave the context untouched - this is
	// the deliberate post-unbind() state the two raw-bind sites above already
	// rely on (unbind() nulls both the tracker and the real VS/PS so the
	// NEXT real bind() is forced to do a genuine rebind); asserting a shader
	// here when the tracker says "none" would fight that convention instead
	// of complementing it.
	void assertShaderStagesBound()
	{
		LLGLSLShader* shader = LLGLSLShader::sCurBoundShaderPtr;
		if (!shader)
		{
			return;
		}
		ID3D11VertexShader* vs = shader->mDXVertexShader.getVS();
		ID3D11PixelShader* ps = shader->mDXPixelShader.getPS();
		if (vs != sLastBoundVS || ps != sLastBoundPS)
		{
			gDXDevice.getContext()->VSSetShader(vs, nullptr, 0);
			gDXDevice.getContext()->PSSetShader(ps, nullptr, 0);
			sLastBoundVS = vs;
			sLastBoundPS = ps;
		}
	}
}

//static
U32 LLVertexBuffer::getAndResetDXDrawCallCount()
{
	U32 count = sDXDrawCallCount;
	sDXDrawCallCount = 0;
	return count;
}
#endif

void LLVertexBuffer::drawRange(U32 mode, U32 start, U32 end, U32 count, U32 indices_offset) const
{
	llassert(validateRange(start, end, count, indices_offset));

#ifdef DX_RENDER
	llassert(mDXBuffer.getBuffer() == sDXRenderBuffer);
	llassert(mDXIndices.getBuffer() == sDXRenderIndices);
	llassert(sDXMode[mode] != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
	gGL.syncMatrices();
	assertShaderStagesBound();
	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	ctx->IASetPrimitiveTopology(sDXMode[mode]);
	ctx->DrawIndexed(count, indices_offset, 0);
	++sDXDrawCallCount;
	// S24 (DX_RENDER diagnostic, 2026-07-28): TEMPORARY - correlates the
	// D3D11 debug-layer VS/PS linkage-error messages (see
	// project_dxrender_vsps_linkage_bug memory) with WHICH shader was bound
	// for this draw - the message text alone never identifies the shader.
	// Cheap: logPendingDebugMessages() only does real logging/I/O the first
	// time each distinct message ID is seen.
	gDXDevice.logPendingDebugMessages(LLGLSLShader::sCurBoundShaderPtr ? LLGLSLShader::sCurBoundShaderPtr->mName.c_str() : "?");
	return;
#endif

	llassert(mGLBuffer == sGLRenderBuffer);
	llassert(mGLIndices == sGLRenderIndices);
	gGL.syncMatrices();
	STOP_GLERROR;
	glDrawRangeElements(sGLMode[mode], start, end, count, mIndicesType,
		(GLvoid*)(indices_offset * (size_t)mIndicesStride));
	STOP_GLERROR;
}

void LLVertexBuffer::drawRangeFast(U32 mode, U32 start, U32 end, U32 count, U32 indices_offset) const
{
#ifdef DX_RENDER
	// S24 (2026-08-02): no gGL.syncMatrices() call in this "fast" variant
	// (pre-existing - callers of drawRangeFast() are expected to have
	// already synced matrices themselves), but the shader-stage assertion
	// applies regardless of that - see assertShaderStagesBound()'s comment.
	assertShaderStagesBound();
	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	ctx->IASetPrimitiveTopology(sDXMode[mode]);
	ctx->DrawIndexed(count, indices_offset, 0);
	++sDXDrawCallCount;
	gDXDevice.logPendingDebugMessages(LLGLSLShader::sCurBoundShaderPtr ? LLGLSLShader::sCurBoundShaderPtr->mName.c_str() : "?");
	return;
#endif

	glDrawRangeElements(sGLMode[mode], start, end, count, mIndicesType,
		(GLvoid*)(indices_offset * (size_t)mIndicesStride));
}

void LLVertexBuffer::draw(U32 mode, U32 count, U32 indices_offset) const
{
	drawRange(mode, 0, mNumVerts - 1, count, indices_offset);
}

void LLVertexBuffer::drawArrays(U32 mode, U32 first, U32 count) const
{
	llassert(first + count <= mNumVerts);

#ifdef DX_RENDER
	llassert(mDXBuffer.getBuffer() == sDXRenderBuffer);
	llassert(mDXIndices.getBuffer() == sDXRenderIndices);
	llassert(sDXMode[mode] != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED);
	gGL.syncMatrices();
	assertShaderStagesBound();
	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	ctx->IASetPrimitiveTopology(sDXMode[mode]);
	ctx->Draw(count, first);
	++sDXDrawCallCount;
	gDXDevice.logPendingDebugMessages(LLGLSLShader::sCurBoundShaderPtr ? LLGLSLShader::sCurBoundShaderPtr->mName.c_str() : "?");
	return;
#endif

	llassert(mGLBuffer == sGLRenderBuffer);
	llassert(mGLIndices == sGLRenderIndices);

	gGL.syncMatrices();
	STOP_GLERROR;
	glDrawArrays(sGLMode[mode], first, count);
	STOP_GLERROR;
}

//static
void LLVertexBuffer::initClass(LLWindow* window)
{
	llassert(sVBOPool == nullptr);

#ifdef DX_RENDER
	// genBuffer()/genIndices() bypass sVBOPool entirely under DX_RENDER (see
	// there) - GL's VBO-orphaning pool and worker-thread-based deferred
	// buffer creation are driver-quirk workarounds with no D3D11 equivalent
	// need, so there's nothing to initialize here.
	return;
#endif

	if (gGLManager.mIsApple)
	{
		LL_INFOS() << "VBO Pooling Disabled" << LL_ENDL;
		sVBOPool = new LLAppleVBOPool();
	}
	else
	{
		LL_INFOS() << "VBO Pooling Enabled" << LL_ENDL;
		sVBOPool = new LLDefaultVBOPool();
	}

	// S24: runtime-toggled GL work queue (see comment above GLWorkQueue class definition).
	if (sVBOWorkQueueEnabled)
	{
		sQueue = new GLWorkQueue();

		for (U32 i = 0; i < sVBOWorkQueueThreadCount; ++i)
		{
			LLGLWorkerThread* worker = new LLGLWorkerThread("VBO Worker", sQueue, window);
			if (worker->isValid())
			{
				worker->start();
				sVBOThreads.push_back(worker);
			}
			else
			{
				// S24: context creation failed (warning already logged in the constructor) -
				// don't start or keep a dead thread object around.
				delete worker;
			}
		}

		if (sVBOThreads.empty())
		{
			// S24: every worker failed to get a shared context - fully disable the queue so
			// producers (see LLDefaultVBOPool::allocate()) fall back to the synchronous path
			// instead of posting work that would sit unprocessed forever.
			LL_WARNS("VertexBuffer") << "VBO work queue enabled but no workers could start - disabling" << LL_ENDL;
			delete sQueue;
			sQueue = nullptr;
		}
	}
}

//static
void LLVertexBuffer::unbind()
{
#ifdef DX_RENDER
	// No GL bind-state concept under DX_RENDER; IASetVertexBuffers/
	// IASetIndexBuffer are reissued fresh at every setBuffer() call, so
	// there's nothing to explicitly unbind on the GPU side - just reset the
	// bookkeeping fields callers (setBuffer(), drawRange()'s assert) check.
	sDXRenderBuffer = nullptr;
	sDXRenderIndices = nullptr;
#else
	STOP_GLERROR;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	STOP_GLERROR;
	sGLRenderBuffer = 0;
	sGLRenderIndices = 0;
#endif
}

//static
void LLVertexBuffer::cleanupClass()
{
	unbind();

#ifdef DX_RENDER
	DXVertexLayout::clear();
#endif

	delete sVBOPool;
	sVBOPool = nullptr;

	if (sQueue)
	{
		sQueue->close();
		for (LLGLWorkerThread* worker : sVBOThreads)
		{
			worker->shutdown();
			delete worker;
		}
		sVBOThreads.clear();

		delete sQueue;
		sQueue = nullptr;
	}
}

//----------------------------------------------------------------------------

LLVertexBuffer::LLVertexBuffer(U32 typemask)
	: LLRefCount(),
	mTypeMask(typemask)
{
	//zero out offsets
	for (U32 i = 0; i < TYPE_MAX; i++)
	{
		mOffsets[i] = 0;
	}
}

// list of mapped buffers
// NOTE: must not be LLPointer<LLVertexBuffer> to avoid breaking non-ref-counted LLVertexBuffer instances
static std::vector<LLVertexBuffer*> sMappedBuffers;

//static
void LLVertexBuffer::flushBuffers()
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	// must only be called from main thread
	for (auto& buffer : sMappedBuffers)
	{
		buffer->_unmapBuffer();
		buffer->mMapped = false;
	}

	sMappedBuffers.resize(0);
}

//static
U32 LLVertexBuffer::calcOffsets(const U32& typemask, U32* offsets, U32 num_vertices)
{
	U32 offset = 0;
	for (U32 i = 0; i < TYPE_TEXTURE_INDEX; i++)
	{
		U32 mask = 1 << i;
		if (typemask & mask)
		{
			if (offsets && LLVertexBuffer::sTypeSize[i])
			{
				offsets[i] = offset;
				offset += LLVertexBuffer::sTypeSize[i] * num_vertices;
				offset = (offset + 0xF) & ~0xF;
			}
		}
	}

	offsets[TYPE_TEXTURE_INDEX] = offsets[TYPE_VERTEX] + 12;

	return offset;
}

//static
U32 LLVertexBuffer::calcVertexSize(const U32& typemask)
{
	U32 size = 0;
	for (U32 i = 0; i < TYPE_TEXTURE_INDEX; i++)
	{
		U32 mask = 1 << i;
		if (typemask & mask)
		{
			size += LLVertexBuffer::sTypeSize[i];
		}
	}

	return size;
}

// protected, use unref()
//virtual
LLVertexBuffer::~LLVertexBuffer()
{
	if (mMapped)
	{ // is on the mapped buffer list but doesn't need to be flushed
		mMapped = false;
		unmapBuffer();
	}

	destroyGLBuffer();
	destroyGLIndices();

	if (mMappedData)
	{
		LL_ERRS() << "Failed to clear vertex buffer's vertices" << LL_ENDL;
	}
	if (mMappedIndexData)
	{
		LL_ERRS() << "Failed to clear vertex buffer's indices" << LL_ENDL;
	}
};

//----------------------------------------------------------------------------

void LLVertexBuffer::genBuffer(U32 size)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

#ifdef DX_RENDER
	// No VBO-orphaning pool under DX_RENDER (that's a GL driver-quirk
	// workaround with no D3D11 equivalent need) - mMappedData is just a
	// plain CPU shadow buffer; mDXBuffer is the real GPU-side resource,
	// uploaded from it in flush_vbo().
	llassert(mSize == 0);
	llassert(mMappedData == nullptr);

	mSize = size;
	mMappedData = (U8*)ll_aligned_malloc_16(size);
	mDXBuffer.createVertexBuffer(size, nullptr);
#else
	llassert(sVBOPool);

	if (sVBOPool)
	{
		llassert(mSize == 0);
		llassert(mGLBuffer == 0);
		llassert(mMappedData == nullptr);

		mSize = size;
		sVBOPool->allocate(GL_ARRAY_BUFFER, mSize, mGLBuffer, mMappedData);
	}
#endif
}

void LLVertexBuffer::genIndices(U32 size)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;

#ifdef DX_RENDER
	llassert(mIndicesSize == 0);
	llassert(mMappedIndexData == nullptr);

	mIndicesSize = size;
	mMappedIndexData = (U8*)ll_aligned_malloc_16(size);
	mDXIndices.createIndexBuffer(size, nullptr);
#else
	llassert(sVBOPool);

	if (sVBOPool)
	{
		llassert(mIndicesSize == 0);
		llassert(mGLIndices == 0);
		llassert(mMappedIndexData == nullptr);
		mIndicesSize = size;
		sVBOPool->allocate(GL_ELEMENT_ARRAY_BUFFER, mIndicesSize, mGLIndices, mMappedIndexData);
	}
#endif
}

bool LLVertexBuffer::createGLBuffer(U32 size)
{
	if (mGLBuffer || mMappedData)
	{
		destroyGLBuffer();
	}

	if (size == 0)
	{
		return true;
	}

	bool success = true;

	genBuffer(size);

	if (!mMappedData)
	{
		success = false;
	}
	return success;
}

bool LLVertexBuffer::createGLIndices(U32 size)
{
	if (mGLIndices)
	{
		destroyGLIndices();
	}

	if (size == 0)
	{
		return true;
	}

	bool success = true;

	genIndices(size);

	if (!mMappedIndexData)
	{
		success = false;
	}
	return success;
}

void LLVertexBuffer::destroyGLBuffer()
{
	if (mGLBuffer || mMappedData)
	{
		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
#ifdef DX_RENDER
		if (mMappedData)
		{
			ll_aligned_free_16(mMappedData);
		}
		mDXBuffer.destroy();
#else
		//llassert(sVBOPool);
		if (sVBOPool)
		{
			sVBOPool->free(GL_ARRAY_BUFFER, mSize, mGLBuffer, mMappedData);
		}
#endif

		mSize = 0;
		mGLBuffer = 0;
		mMappedData = nullptr;
	}
}

void LLVertexBuffer::destroyGLIndices()
{
	if (mGLIndices || mMappedIndexData)
	{
		LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
#ifdef DX_RENDER
		if (mMappedIndexData)
		{
			ll_aligned_free_16(mMappedIndexData);
		}
		mDXIndices.destroy();
#else
		//llassert(sVBOPool);
		if (sVBOPool)
		{
			sVBOPool->free(GL_ELEMENT_ARRAY_BUFFER, mIndicesSize, mGLIndices, mMappedIndexData);
		}
#endif

		mIndicesSize = 0;
		mGLIndices = 0;
		mMappedIndexData = nullptr;
	}
}

bool LLVertexBuffer::updateNumVerts(U32 nverts)
{
	llassert(nverts >= 0);

	bool success = true;

	U32 needed_size = calcOffsets(mTypeMask, mOffsets, nverts);

	if (needed_size != mSize)
	{
		success &= createGLBuffer(needed_size);
	}

	llassert(mSize == needed_size);
	mNumVerts = nverts;
	return success;
}

bool LLVertexBuffer::updateNumIndices(U32 nindices)
{
	llassert(nindices >= 0);

	bool success = true;

	U32 needed_size = sizeof(U16) * nindices;

	if (needed_size != mIndicesSize)
	{
		success &= createGLIndices(needed_size);
	}

	llassert(mIndicesSize == needed_size);
	mNumIndices = nindices;
	return success;
}

bool LLVertexBuffer::allocateBuffer(U32 nverts, U32 nindices)
{
	if (nverts < 0 || nindices < 0)
	{
		LL_ERRS() << "Bad vertex buffer allocation: " << nverts << " : " << nindices << LL_ENDL;
	}

	bool success = true;

	success &= updateNumVerts(nverts);
	success &= updateNumIndices(nindices);

	return success;
}

//----------------------------------------------------------------------------

// if no gap between region and given range exists, expand region to cover given range and return true
// otherwise return false
bool expand_region(LLVertexBuffer::MappedRegion& region, U32 start, U32 end)
{
	if (end < region.mStart ||
		start > region.mEnd)
	{ //gap exists, do not merge
		return false;
	}

	region.mStart = llmin(region.mStart, start);
	region.mEnd = llmax(region.mEnd, end);

	return true;
}

// Map for data access
U8* LLVertexBuffer::mapVertexBuffer(LLVertexBuffer::AttributeType type, U32 index, S32 count)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	_mapBuffer();

	if (count == -1)
	{
		count = mNumVerts - index;
	}

	if (!gGLManager.mIsApple)
	{
		U32 start = mOffsets[type] + sTypeSize[type] * index;
		U32 end = start + sTypeSize[type] * count - 1;

		bool flagged = false;
		// flag region as mapped
		for (U32 i = 0; i < mMappedVertexRegions.size(); ++i)
		{
			MappedRegion& region = mMappedVertexRegions[i];
			if (expand_region(region, start, end))
			{
				flagged = true;
				break;
			}
		}

		if (!flagged)
		{
			//didn't expand an existing region, make a new one
			mMappedVertexRegions.push_back({ start, end });
		}
	}
	return mMappedData + mOffsets[type] + sTypeSize[type] * index;
}

U8* LLVertexBuffer::mapIndexBuffer(U32 index, S32 count)
{
	LL_PROFILE_ZONE_SCOPED_CATEGORY_VERTEX;
	_mapBuffer();

	if (count == -1)
	{
		count = mNumIndices - index;
	}

	if (!gGLManager.mIsApple)
	{
		U32 start = sizeof(U16) * index;
		U32 end = start + sizeof(U16) * count - 1;

		bool flagged = false;
		// flag region as mapped
		for (U32 i = 0; i < mMappedIndexRegions.size(); ++i)
		{
			MappedRegion& region = mMappedIndexRegions[i];
			if (expand_region(region, start, end))
			{
				flagged = true;
				break;
			}
		}

		if (!flagged)
		{
			//didn't expand an existing region, make a new one
			mMappedIndexRegions.push_back({ start, end });
		}
	}

	return mMappedIndexData + sizeof(U16) * index;
}

// flush the given byte range
//  target -- "target" parameter for glBufferSubData
//  start -- first byte to copy
//  end -- last byte to copy (NOT last byte + 1)
//  data -- data to be flushed
//  dst -- mMappedData or mMappedIndexData
void LLVertexBuffer::flush_vbo(GLenum target, U32 start, U32 end, void* data, U8* dst)
{
	if (gGLManager.mIsApple)
	{
		// on OS X, flush_vbo doesn't actually write to the GL buffer, so be sure to call
		// _mapBuffer to tag the buffer for flushing to GL
		_mapBuffer();
		LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("vb memcpy");
		STOP_GLERROR;
		// copy into mapped buffer
		memcpy(dst + start, data, end - start + 1);
	}
	else
	{
#ifdef DX_RENDER
		// D3D11_USAGE_DYNAMIC has no partial-range update - Map(WRITE_DISCARD)
		// invalidates the whole resource, so dst (mMappedData/mMappedIndexData)
		// must hold the complete, authoritative buffer contents before the
		// full-buffer upload. Callers that already write directly into dst
		// (mapVertexBuffer()/getXXXStrider()) pass data == dst + start, so this
		// memcpy is skipped; callers with an external source (setPositionData()
		// and friends) need this copy to keep dst authoritative for the bytes
		// outside their own touched range.
		if (end != 0 && (U8*)data != dst + start)
		{
			memcpy(dst + start, data, end - start + 1);
		}

		DXBuffer& buf = (target == GL_ARRAY_BUFFER) ? mDXBuffer : mDXIndices;
		U32 full_size = (target == GL_ARRAY_BUFFER) ? mSize : mIndicesSize;
		buf.upload(dst, full_size);

		// S24 (2026-07-23): a staging-buffer readback diagnostic here (part
		// of the "no text" investigation, stage 6 discovery) confirmed the
		// GPU vertex buffer always holds exactly the same TEXCOORD0 bytes
		// just written into `dst` - the upload path itself was never the
		// issue. See the project's open-issues ledger for the full history.
#else
		llassert(target == GL_ARRAY_BUFFER ? sGLRenderBuffer == mGLBuffer : sGLRenderIndices == mGLIndices);

		// skip mapped data and stream to GPU via glBufferSubData
		if (end != 0)
		{
			LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("glBufferSubData");
			LL_PROFILE_ZONE_NUM(start);
			LL_PROFILE_ZONE_NUM(end);
			LL_PROFILE_ZONE_NUM(end - start);

			constexpr U32 block_size = 65536;

			for (U32 i = start; i <= end; i += block_size)
			{
				//LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("glBufferSubData block");
				//LL_PROFILE_GPU_ZONE("glBufferSubData");
				U32 tend = llmin(i + block_size, end);
				U32 size = tend - i + 1;
				glBufferSubData(target, i, size, (U8*)data + (i - start));
			}
		}
#endif // DX_RENDER
	}
}

void LLVertexBuffer::unmapBuffer()
{
	flushBuffers();
}

void LLVertexBuffer::_mapBuffer()
{
	if (!mMapped)
	{
		mMapped = true;
		sMappedBuffers.push_back(this);
	}
}

void LLVertexBuffer::_unmapBuffer()
{
	STOP_GLERROR;
	if (!mMapped)
	{
		return;
	}

	struct SortMappedRegion
	{
		bool operator()(const MappedRegion& lhs, const MappedRegion& rhs)
		{
			return lhs.mStart < rhs.mStart;
		}
	};

	if (gGLManager.mIsApple)
	{
		STOP_GLERROR;
		if (mMappedData)
		{
			if (mGLBuffer)
			{
				delete_buffers(1, &mGLBuffer);
			}
			mGLBuffer = gen_buffer();
			glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
			sGLRenderBuffer = mGLBuffer;
			glBufferData(GL_ARRAY_BUFFER, mSize, mMappedData, GL_STATIC_DRAW);
		}
		else if (mGLBuffer != sGLRenderBuffer)
		{
			glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
			sGLRenderBuffer = mGLBuffer;
		}
		STOP_GLERROR;

		if (mMappedIndexData)
		{
			if (mGLIndices)
			{
				delete_buffers(1, &mGLIndices);
			}

			mGLIndices = gen_buffer();
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
			sGLRenderIndices = mGLIndices;

			glBufferData(GL_ELEMENT_ARRAY_BUFFER, mIndicesSize, mMappedIndexData, GL_STATIC_DRAW);
		}
		else if (mGLIndices != sGLRenderIndices)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
			sGLRenderIndices = mGLIndices;
		}
		STOP_GLERROR;
	}
	else
	{
		if (!mMappedVertexRegions.empty())
		{
			LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("unmapBuffer - vertex");

#ifndef DX_RENDER
			// No GL bind-state concept under DX_RENDER - flush_vbo()'s DX_RENDER
			// branch uploads via Map/Unmap directly, no bound-buffer prerequisite.
			if (sGLRenderBuffer != mGLBuffer)
			{
				glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
				sGLRenderBuffer = mGLBuffer;
			}
#endif

			U32 start = 0;
			U32 end = 0;

			std::sort(mMappedVertexRegions.begin(), mMappedVertexRegions.end(), SortMappedRegion());

			for (U32 i = 0; i < mMappedVertexRegions.size(); ++i)
			{
				const MappedRegion& region = mMappedVertexRegions[i];
				if (region.mStart == end + 1)
				{
					end = region.mEnd;
				}
				else
				{
					flush_vbo(GL_ARRAY_BUFFER, start, end, (U8*)mMappedData + start, mMappedData);
					start = region.mStart;
					end = region.mEnd;
				}
			}

			flush_vbo(GL_ARRAY_BUFFER, start, end, (U8*)mMappedData + start, mMappedData);
			mMappedVertexRegions.clear();
		}

		if (!mMappedIndexRegions.empty())
		{
			LL_PROFILE_ZONE_NAMED_CATEGORY_VERTEX("unmapBuffer - index");

#ifndef DX_RENDER
			if (mGLIndices != sGLRenderIndices)
			{
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
				sGLRenderIndices = mGLIndices;
			}
#endif
			U32 start = 0;
			U32 end = 0;

			std::sort(mMappedIndexRegions.begin(), mMappedIndexRegions.end(), SortMappedRegion());

			for (U32 i = 0; i < mMappedIndexRegions.size(); ++i)
			{
				const MappedRegion& region = mMappedIndexRegions[i];
				if (region.mStart == end + 1)
				{
					end = region.mEnd;
				}
				else
				{
					flush_vbo(GL_ELEMENT_ARRAY_BUFFER, start, end, (U8*)mMappedIndexData + start, mMappedIndexData);
					start = region.mStart;
					end = region.mEnd;
				}
			}

			flush_vbo(GL_ELEMENT_ARRAY_BUFFER, start, end, (U8*)mMappedIndexData + start, mMappedIndexData);
			mMappedIndexRegions.clear();
		}
	}
}

//----------------------------------------------------------------------------

template <class T, LLVertexBuffer::AttributeType type> struct VertexBufferStrider
{
	typedef LLStrider<T> strider_t;
	static bool get(LLVertexBuffer& vbo,
		strider_t& strider,
		S32 index, S32 count)
	{
		if (type == LLVertexBuffer::TYPE_INDEX)
		{
			U8* ptr = vbo.mapIndexBuffer(index, count);

			if (ptr == NULL)
			{
				LL_WARNS() << "mapIndexBuffer failed!" << LL_ENDL;
				return false;
			}

			strider = (T*)ptr;
			strider.setStride(0);
			return true;
		}
		else if (vbo.hasDataType(type))
		{
			U32 stride = LLVertexBuffer::sTypeSize[type];

			U8* ptr = vbo.mapVertexBuffer(type, index, count);

			if (ptr == NULL)
			{
				LL_WARNS() << "mapVertexBuffer failed!" << LL_ENDL;
				return false;
			}

			strider = (T*)ptr;
			strider.setStride(stride);
			return true;
		}
		else
		{
			LL_ERRS() << "VertexBufferStrider could not find valid vertex data." << LL_ENDL;
		}
		return false;
	}
};

bool LLVertexBuffer::getVertexStrider(LLStrider<LLVector3>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector3, TYPE_VERTEX>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getVertexStrider(LLStrider<LLVector4a>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4a, TYPE_VERTEX>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getIndexStrider(LLStrider<U16>& strider, U32 index, S32 count)
{
	llassert(mIndicesStride == 2); // cannot access 32-bit indices with U16 strider
	llassert(mIndicesType == GL_UNSIGNED_SHORT);
	return VertexBufferStrider<U16, TYPE_INDEX>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTexCoord0Strider(LLStrider<LLVector2>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector2, TYPE_TEXCOORD0>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTexCoord1Strider(LLStrider<LLVector2>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector2, TYPE_TEXCOORD1>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTexCoord2Strider(LLStrider<LLVector2>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector2, TYPE_TEXCOORD2>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getNormalStrider(LLStrider<LLVector3>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector3, TYPE_NORMAL>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getNormalStrider(LLStrider<LLVector4a>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4a, TYPE_NORMAL>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTangentStrider(LLStrider<LLVector3>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector3, TYPE_TANGENT>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getTangentStrider(LLStrider<LLVector4a>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4a, TYPE_TANGENT>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getColorStrider(LLStrider<LLColor4U>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLColor4U, TYPE_COLOR>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getEmissiveStrider(LLStrider<LLColor4U>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLColor4U, TYPE_EMISSIVE>::get(*this, strider, index, count);
}
bool LLVertexBuffer::getWeightStrider(LLStrider<F32>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<F32, TYPE_WEIGHT>::get(*this, strider, index, count);
}

bool LLVertexBuffer::getWeight4Strider(LLStrider<LLVector4>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4, TYPE_WEIGHT4>::get(*this, strider, index, count);
}

bool LLVertexBuffer::getClothWeightStrider(LLStrider<LLVector4>& strider, U32 index, S32 count)
{
	return VertexBufferStrider<LLVector4, TYPE_CLOTHWEIGHT>::get(*this, strider, index, count);
}

//----------------------------------------------------------------------------

// Set for rendering
void LLVertexBuffer::setBuffer()
{
	STOP_GLERROR;

	if (mMapped)
	{
		LL_WARNS_ONCE() << "Missing call to unmapBuffer or flushBuffers" << LL_ENDL;
		_unmapBuffer();
	}

	// no data may be pending
	llassert(mMappedVertexRegions.empty());
	llassert(mMappedIndexRegions.empty());

	// a shader must be bound
	llassert(LLGLSLShader::sCurBoundShaderPtr);

#ifdef DX_RENDER
	// Unlike GL's exact data_mask/mTypeMask superset assert, D3D11 input
	// layouts tolerate a vertex buffer providing more attributes than the
	// shader consumes (extra elements are simply unused) - so there's no
	// equivalent check here. setupVertexBuffer() builds the layout from this
	// buffer's own mTypeMask; CreateInputLayout itself fails correctly (see
	// DXVertexLayout::getOrCreate()'s LL_WARNS) if the bound VS needs an
	// attribute this buffer lacks.
	if (sDXRenderBuffer != mDXBuffer.getBuffer())
	{
		sDXRenderBuffer = mDXBuffer.getBuffer();
		setupVertexBuffer();
		sDXLastShader = LLGLSLShader::sCurBoundShaderPtr;
	}
	else if (sDXLastShader != LLGLSLShader::sCurBoundShaderPtr)
	{
		setupVertexBuffer();
		sDXLastShader = LLGLSLShader::sCurBoundShaderPtr;
	}

	if (mDXIndices.getBuffer() != sDXRenderIndices)
	{
		sDXRenderIndices = mDXIndices.getBuffer();
		gDXDevice.getContext()->IASetIndexBuffer(sDXRenderIndices,
			mIndicesType == GL_UNSIGNED_SHORT ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
	}

	STOP_GLERROR;
	return;
#endif

	U32 data_mask = LLGLSLShader::sCurBoundShaderPtr->mAttributeMask;

	// this Vertex Buffer must provide all necessary attributes for currently bound shader
	llassert_msg((data_mask & mTypeMask) == data_mask,
		"Attribute mask mismatch! mTypeMask should be a superset of data_mask.  data_mask: 0x"
		<< std::hex << data_mask << " mTypeMask: 0x" << mTypeMask << " Missing: 0x" << (data_mask & ~mTypeMask) << std::dec);

	if (sGLRenderBuffer != mGLBuffer)
	{
		glBindBuffer(GL_ARRAY_BUFFER, mGLBuffer);
		sGLRenderBuffer = mGLBuffer;

		setupVertexBuffer();
	}
	else if (sLastMask != data_mask)
	{
		setupVertexBuffer();
		sLastMask = data_mask;
	}

	if (mGLIndices != sGLRenderIndices)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mGLIndices);
		sGLRenderIndices = mGLIndices;
	}

	STOP_GLERROR;
}

// virtual (default)
void LLVertexBuffer::setupVertexBuffer()
{
	STOP_GLERROR;

#ifdef DX_RENDER
	// LLVertexBuffer is struct-of-arrays (calcOffsets() gives each attribute
	// its own contiguous block spanning all vertices), not interleaved - so
	// each active attribute gets its own D3D11 input slot, all views into
	// the same mDXBuffer resource at different offsets/strides. Built from
	// this buffer's own mTypeMask (see setBuffer()'s comment on why, unlike
	// GL, there's no need to consult the bound shader's attribute mask here).
	// Enumeration order MUST match DXVertexLayout::getOrCreate()'s InputSlot
	// assignment (bits 0-5, then color/emissive, then tangent, then weight,
	// then clothweight).
	llassert(LLGLSLShader::sCurBoundShaderPtr);
	ID3DBlob* vs_bytecode = LLGLSLShader::sCurBoundShaderPtr->mDXVertexShader.getVSBytecode();
	if (!vs_bytecode)
	{
		return;
	}

	ID3D11Buffer* vb = mDXBuffer.getBuffer();
	// S24 (2026-08-09, task #157): bumped 11->12 to fit the new MAP_WEIGHT4
	// slot - see DXVertexLayout.cpp's matching elements[] array comment.
	ID3D11Buffer* buffers[12];
	UINT strides[12];
	UINT offsets[12];
	UINT count = 0;

	static const AttributeType kSimple[6] = { TYPE_VERTEX, TYPE_NORMAL, TYPE_TEXCOORD0, TYPE_TEXCOORD1, TYPE_TEXCOORD2, TYPE_TEXCOORD3 };
	for (AttributeType type : kSimple)
	{
		if (mTypeMask & (1u << type))
		{
			buffers[count] = vb;
			strides[count] = sTypeSize[type];
			offsets[count] = mOffsets[type];
			++count;
		}
	}

	if ((mTypeMask & MAP_COLOR) || (mTypeMask & MAP_EMISSIVE))
	{
		// S24 (2026-08-09): which data source (color vs emissive) feeds the
		// shared COLOR0 slot depends on which one the BOUND SHADER actually
		// wants, not just which ones this vertex buffer happens to carry. A
		// single batch/VBO can carry BOTH per-vertex color and emissive data
		// simultaneously (any alpha-blended face that also has classic Glow
		// enabled - llvovolume.cpp unions MAP_EMISSIVE into the group mask
		// alongside MAP_COLOR), and different passes over that SAME buffer
		// use different shaders wanting different data in COLOR0: the main
		// alpha-blend pass (alphaV.hlsl) wants real diffuse_color, the
		// separate glow-accumulation pass (emissiveV.hlsl/pbrglowV.hlsl)
		// wants emissive - both declare their input as plain "COLOR0" in
		// HLSL, so D3D11 bytecode reflection alone can't tell them apart
		// (unlike GL's mapAttributes(), which resolves by attribute NAME,
		// not just semantic slot - see DXVertexLayout.cpp's kColor comment
		// for the equivalent D3D11-side "COLOR0 is shared" convention).
		// The previous unconditional "prefer emissive whenever present"
		// rule silently corrupted the main alpha-blend pass's color/alpha
		// for any face with both Transparency and classic Glow enabled -
		// found via a simple-prim-specific investigation (most tested mesh
		// content uses GLTF/PBR material emissive instead, a texture
		// channel that never sets MAP_EMISSIVE, so this never surfaced
		// there). Identify the emissive-accumulation shaders by name since
		// reflection can't distinguish them structurally; falls back to
		// emissive if that's genuinely the only data this buffer carries.
		const std::string& bound_name = LLGLSLShader::sCurBoundShaderPtr->mName;
		bool wants_emissive_in_color0 =
			bound_name == "Deferred Emissive Shader" ||
			bound_name == " PBR Glow Shader" ||
			bound_name == "Skinned Deferred Emissive Shader" ||
			bound_name == "Skinned  PBR Glow Shader";
		AttributeType src = ((mTypeMask & MAP_EMISSIVE) && (wants_emissive_in_color0 || !(mTypeMask & MAP_COLOR)))
			? TYPE_EMISSIVE : TYPE_COLOR;
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_COLOR];
		offsets[count] = mOffsets[src];
		++count;
	}

	if (mTypeMask & MAP_TANGENT)
	{
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_TANGENT];
		offsets[count] = mOffsets[TYPE_TANGENT];
		++count;
	}

	if (mTypeMask & MAP_WEIGHT)
	{
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_WEIGHT];
		offsets[count] = mOffsets[TYPE_WEIGHT];
		++count;
	}

	if (mTypeMask & MAP_WEIGHT4)
	{
		// S24 (2026-08-09, task #157): rigged-mesh attachment/clothing
		// skinning (objectSkinV.hlsl's "weight4 : BLENDWEIGHT" input) - was
		// unconditionally rejected by DXVertexLayout::getOrCreate() until
		// now (see that file's kWeight4 comment), meaning every rigged
		// mesh draw call could never get a valid input layout at all.
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_WEIGHT4];
		offsets[count] = mOffsets[TYPE_WEIGHT4];
		++count;
	}

	if (mTypeMask & MAP_CLOTHWEIGHT)
	{
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_CLOTHWEIGHT];
		offsets[count] = mOffsets[TYPE_CLOTHWEIGHT];
		++count;
	}

	if (mTypeMask & MAP_TEXTURE_INDEX)
	{
		// Same underlying buffer as TYPE_VERTEX, different input slot - see
		// DXVertexLayout.h's header comment: this is packed into position's
		// otherwise-unused W component (mOffsets[TYPE_TEXTURE_INDEX] is
		// already TYPE_VERTEX's offset + 12, computed by the shared,
		// non-DX_RENDER-specific calcOffsets()), not a separate stream.
		buffers[count] = vb;
		strides[count] = sTypeSize[TYPE_VERTEX];
		offsets[count] = mOffsets[TYPE_TEXTURE_INDEX];
		++count;
	}

	ID3D11DeviceContext* ctx = gDXDevice.getContext();
	ctx->IASetVertexBuffers(0, count, buffers, strides, offsets);

	ID3D11InputLayout* layout = DXVertexLayout::getOrCreate(mTypeMask, vs_bytecode->GetBufferPointer(), vs_bytecode->GetBufferSize(),
		LLGLSLShader::sCurBoundShaderPtr->mName.c_str());
	ctx->IASetInputLayout(layout);

	// S24 (2026-07-23): a diagnostic here (part of the "no text"
	// investigation, stage 6 discovery) confirmed the runtime
	// IASetVertexBuffers()/IASetInputLayout() parameters bound at draw time
	// exactly match the GPU buffer's own, already-verified-correct
	// TEXCOORD0 content - input assembly's own state-setting was never the
	// issue either. See the project's open-issues ledger for the full
	// history and where the investigation concluded.
	return;
#endif

	U8* base = nullptr;

	U32 data_mask = LLGLSLShader::sCurBoundShaderPtr->mAttributeMask;

	if (data_mask & MAP_NORMAL)
	{
		AttributeType loc = TYPE_NORMAL;
		void* ptr = (void*)(base + mOffsets[TYPE_NORMAL]);
		glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_NORMAL], ptr);
	}
	if (data_mask & MAP_TEXCOORD3)
	{
		AttributeType loc = TYPE_TEXCOORD3;
		void* ptr = (void*)(base + mOffsets[TYPE_TEXCOORD3]);
		glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD3], ptr);
	}
	if (data_mask & MAP_TEXCOORD2)
	{
		AttributeType loc = TYPE_TEXCOORD2;
		void* ptr = (void*)(base + mOffsets[TYPE_TEXCOORD2]);
		glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD2], ptr);
	}
	if (data_mask & MAP_TEXCOORD1)
	{
		AttributeType loc = TYPE_TEXCOORD1;
		void* ptr = (void*)(base + mOffsets[TYPE_TEXCOORD1]);
		glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD1], ptr);
	}
	if (data_mask & MAP_TANGENT)
	{
		AttributeType loc = TYPE_TANGENT;
		void* ptr = (void*)(base + mOffsets[TYPE_TANGENT]);
		glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TANGENT], ptr);
	}
	if (data_mask & MAP_TEXCOORD0)
	{
		AttributeType loc = TYPE_TEXCOORD0;
		void* ptr = (void*)(base + mOffsets[TYPE_TEXCOORD0]);
		glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_TEXCOORD0], ptr);
	}
	if (data_mask & MAP_COLOR)
	{
		AttributeType loc = TYPE_COLOR;
		//bind emissive instead of color pointer if emissive is present
		void* ptr = (data_mask & MAP_EMISSIVE) ? (void*)(base + mOffsets[TYPE_EMISSIVE]) : (void*)(base + mOffsets[TYPE_COLOR]);
		glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_COLOR], ptr);
	}
	if (data_mask & MAP_EMISSIVE)
	{
		AttributeType loc = TYPE_EMISSIVE;
		void* ptr = (void*)(base + mOffsets[TYPE_EMISSIVE]);
		glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_EMISSIVE], ptr);

		if (!(data_mask & MAP_COLOR))
		{ //map emissive to color channel when color is not also being bound to avoid unnecessary shader swaps
			loc = TYPE_COLOR;
			glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_EMISSIVE], ptr);
		}
	}
	if (data_mask & MAP_WEIGHT)
	{
		AttributeType loc = TYPE_WEIGHT;
		void* ptr = (void*)(base + mOffsets[TYPE_WEIGHT]);
		glVertexAttribPointer(loc, 1, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_WEIGHT], ptr);
	}
	if (data_mask & MAP_WEIGHT4)
	{
		AttributeType loc = TYPE_WEIGHT4;
		void* ptr = (void*)(base + mOffsets[TYPE_WEIGHT4]);
		glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_WEIGHT4], ptr);
	}
	if (data_mask & MAP_JOINT)
	{
		AttributeType loc = TYPE_JOINT;
		void* ptr = (void*)(base + mOffsets[TYPE_JOINT]);
		glVertexAttribIPointer(loc, 4, GL_UNSIGNED_SHORT, LLVertexBuffer::sTypeSize[TYPE_JOINT], ptr);
	}
	if (data_mask & MAP_CLOTHWEIGHT)
	{
		AttributeType loc = TYPE_CLOTHWEIGHT;
		void* ptr = (void*)(base + mOffsets[TYPE_CLOTHWEIGHT]);
		glVertexAttribPointer(loc, 4, GL_FLOAT, GL_TRUE, LLVertexBuffer::sTypeSize[TYPE_CLOTHWEIGHT], ptr);
	}
	if (data_mask & MAP_TEXTURE_INDEX)
	{
		AttributeType loc = TYPE_TEXTURE_INDEX;
		void* ptr = (void*)(base + mOffsets[TYPE_VERTEX] + 12);
		glVertexAttribIPointer(loc, 1, GL_UNSIGNED_INT, LLVertexBuffer::sTypeSize[TYPE_VERTEX], ptr);
	}
	if (data_mask & MAP_VERTEX)
	{
		AttributeType loc = TYPE_VERTEX;
		void* ptr = (void*)(base + mOffsets[TYPE_VERTEX]);
		glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, LLVertexBuffer::sTypeSize[TYPE_VERTEX], ptr);
	}
	STOP_GLERROR;
}

void LLVertexBuffer::setPositionData(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, 0, sizeof(LLVector4a) * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord0Data(const LLVector2* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD0], mOffsets[TYPE_TEXCOORD0] + sTypeSize[TYPE_TEXCOORD0] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord1Data(const LLVector2* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD1], mOffsets[TYPE_TEXCOORD1] + sTypeSize[TYPE_TEXCOORD1] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setColorData(const LLColor4U* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_COLOR], mOffsets[TYPE_COLOR] + sTypeSize[TYPE_COLOR] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setNormalData(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_NORMAL], mOffsets[TYPE_NORMAL] + sTypeSize[TYPE_NORMAL] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTangentData(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TANGENT], mOffsets[TYPE_TANGENT] + sTypeSize[TYPE_TANGENT] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setWeight4Data(const LLVector4a* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_WEIGHT4], mOffsets[TYPE_WEIGHT4] + sTypeSize[TYPE_WEIGHT4] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setJointData(const U64* data)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_JOINT], mOffsets[TYPE_JOINT] + sTypeSize[TYPE_JOINT] * getNumVerts() - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setIndexData(const U16* data)
{
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(U16) * getNumIndices() - 1, (U8*)data, mMappedIndexData);
}

void LLVertexBuffer::setIndexData(const U32* data)
{
	if (mIndicesType != GL_UNSIGNED_INT)
	{ // HACK -- vertex buffers are initialized as 16-bit indices, but can be switched to 32-bit indices
		mIndicesType = GL_UNSIGNED_INT;
		mIndicesStride = 4;
		mNumIndices /= 2;
	}
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(U32) * getNumIndices() - 1, (U8*)data, mMappedIndexData);
}

void LLVertexBuffer::setPositionData(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, offset * sizeof(LLVector4a), (offset + count) * sizeof(LLVector4a) - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setNormalData(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_NORMAL] + offset * sTypeSize[TYPE_NORMAL], mOffsets[TYPE_NORMAL] + (offset + count) * sTypeSize[TYPE_NORMAL] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord0Data(const LLVector2* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD0] + offset * sTypeSize[TYPE_TEXCOORD0], mOffsets[TYPE_TEXCOORD0] + (offset + count) * sTypeSize[TYPE_TEXCOORD0] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTexCoord1Data(const LLVector2* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TEXCOORD1] + offset * sTypeSize[TYPE_TEXCOORD1], mOffsets[TYPE_TEXCOORD1] + (offset + count) * sTypeSize[TYPE_TEXCOORD1] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setColorData(const LLColor4U* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_COLOR] + offset * sTypeSize[TYPE_COLOR], mOffsets[TYPE_COLOR] + (offset + count) * sTypeSize[TYPE_COLOR] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setTangentData(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_TANGENT] + offset * sTypeSize[TYPE_TANGENT], mOffsets[TYPE_TANGENT] + (offset + count) * sTypeSize[TYPE_TANGENT] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setWeight4Data(const LLVector4a* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_WEIGHT4] + offset * sTypeSize[TYPE_WEIGHT4], mOffsets[TYPE_WEIGHT4] + (offset + count) * sTypeSize[TYPE_WEIGHT4] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setJointData(const U64* data, U32 offset, U32 count)
{
	flush_vbo(GL_ARRAY_BUFFER, mOffsets[TYPE_JOINT] + offset * sTypeSize[TYPE_JOINT], mOffsets[TYPE_JOINT] + (offset + count) * sTypeSize[TYPE_JOINT] - 1, (U8*)data, mMappedData);
}

void LLVertexBuffer::setIndexData(const U16* data, U32 offset, U32 count)
{
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, offset * sizeof(U16), (offset + count) * sizeof(U16) - 1, (U8*)data, mMappedIndexData);
}

void LLVertexBuffer::setIndexData(const U32* data, U32 offset, U32 count)
{
	if (mIndicesType != GL_UNSIGNED_INT)
	{ // HACK -- vertex buffers are initialized as 16-bit indices, but can be switched to 32-bit indices
		mIndicesType = GL_UNSIGNED_INT;
		mIndicesStride = 4;
		mNumIndices /= 2;
	}
	flush_vbo(GL_ELEMENT_ARRAY_BUFFER, offset * sizeof(U32), (offset + count) * sizeof(U32) - 1, (U8*)data, mMappedIndexData);
}