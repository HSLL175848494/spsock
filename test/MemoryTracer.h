#ifndef HSLL_MEMORYTRACER
#define HSLL_MEMORYTRACER

#include <atomic>
#include <cstdlib>
#include <exception>

#if defined(_WIN32) || defined(_WIN64)
#include <crtdbg.h>
#define GET_ALLOC_SIZE(ptr) ::_msize(ptr)
#elif defined(__APPLE__) || defined(__linux__)
#include <malloc.h>
#define GET_ALLOC_SIZE(ptr) ::malloc_usable_size(ptr)
#else
#error "Unsupported platform: Memory size retrieval not implemented"
#endif

namespace HSLL::TRACING
{
	namespace VARIABLE
	{
		bool tracingFlag = false;
		std::atomic<unsigned long long> allocSize = 0;
	}
	void StopTracing()
	{
		VARIABLE::tracingFlag = false;
		VARIABLE::allocSize = 0;
	}
	void StartTracing() { VARIABLE::tracingFlag = true; }
	unsigned long long GetAllocSize() { return VARIABLE::allocSize; }
}

void *operator new(size_t size)
{
	void *ptr = std::malloc(size);

	if (!ptr)
		throw std::bad_alloc();

	if (HSLL::TRACING::VARIABLE::tracingFlag)
		HSLL::TRACING::VARIABLE::allocSize.fetch_add(GET_ALLOC_SIZE(ptr), std::memory_order_relaxed);

	return ptr;
}

void operator delete(void *ptr) noexcept
{
	if (!ptr)
		return;

	if (HSLL::TRACING::VARIABLE::tracingFlag)
		HSLL::TRACING::VARIABLE::allocSize.fetch_sub(GET_ALLOC_SIZE(ptr), std::memory_order_relaxed);

	std::free(ptr);
}

#endif // HSLL_MEMORYTRACER