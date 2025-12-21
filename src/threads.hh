#ifndef TIBIA_THREADS_HH_
#define TIBIA_THREADS_HH_ 1

#include "common.hh"

// =============================================================================
// Cross-platform threading support
// =============================================================================

#if OS_WINDOWS
	typedef HANDLE ThreadHandle;
	#define INVALID_THREAD_HANDLE NULL
#elif OS_LINUX
	#include <pthread.h>
	typedef pthread_t ThreadHandle;
	#define INVALID_THREAD_HANDLE ((pthread_t)0)
#endif

typedef int (ThreadFunction)(void *);

ThreadHandle StartThread(ThreadFunction *Function, void *Argument, bool Detach);
ThreadHandle StartThread(ThreadFunction *Function, void *Argument, size_t StackSize, bool Detach);
ThreadHandle StartThread(ThreadFunction *Function, void *Argument, void *Stack, size_t StackSize, bool Detach);
int JoinThread(ThreadHandle Handle);
void DelayThread(int Seconds, int MicroSeconds);

// =============================================================================
// Cross-platform Semaphore
// =============================================================================

struct Semaphore {
	Semaphore(int Value);
	~Semaphore(void);
	void up(void);
	void down(void);

	// DATA
	// =================
	int value;
#if OS_WINDOWS
	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE condition;
#elif OS_LINUX
	pthread_mutex_t mutex;
	pthread_cond_t condition;
#endif
};

#endif //TIBIA_THREADS_HH_
