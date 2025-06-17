#ifndef TIBIA_THREAD_HH_
#define TIBIA_THREAD_HH_ 1

#include "common.hh"
#include <pthread.h>

typedef pthread_t ThreadHandle;
typedef int (ThreadFunction)(void *);

// TODO(fusion): Probably have another way to tell whether the thread was
// successfully spawned or not?
constexpr ThreadHandle INVALID_THREAD_HANDLE = 0;

ThreadHandle StartThread(ThreadFunction *Function, void *Argument, bool Detach);
ThreadHandle StartThread(ThreadFunction *Function, void *Argument, size_t StackSize, bool Detach);
ThreadHandle StartThread(ThreadFunction *Function, void *Argument, void *Stack, size_t StackSize, bool Detach);
int JoinThread(ThreadHandle Handle);
void DelayThread(int Seconds, int MicroSeconds);

struct Semaphore {
	Semaphore(int Value);
	~Semaphore(void);
	void up(void);
	void down(void);

	// DATA
	// =================
	int value;
	pthread_mutex_t mutex;
	pthread_cond_t condition;
};

#endif //TIBIA_THREAD_HH_
