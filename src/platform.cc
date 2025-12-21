#include "platform.hh"
#include <string.h>

// =============================================================================
// Windows Implementation
// =============================================================================

#if OS_WINDOWS

#include <map>
#include <mutex>

// Global socket initialization flag
static bool g_socketsInitialized = false;

// Thread event management
static std::mutex g_threadEventsMutex;
static std::map<ThreadID, PlatformEvent*> g_threadEvents;

// =============================================================================
// Platform Event Structure (Windows)
// =============================================================================

struct PlatformEvent {
	HANDLE handle;
	volatile int signalType;
	CRITICAL_SECTION cs;
};

struct PlatformTimer {
	HANDLE handle;
	volatile int overrunCount;
	bool repeating;
	int interval_ms;
	HANDLE threadHandle;
	volatile bool running;
};

struct PlatformDir {
	HANDLE handle;
	WIN32_FIND_DATAA findData;
	bool firstRead;
	char path[MAX_PATH];
};

// =============================================================================
// Process and Thread functions (Windows)
// =============================================================================

ProcessID Platform_GetProcessID(void) {
	return GetCurrentProcessId();
}

ThreadID Platform_GetThreadID(void) {
	return GetCurrentThreadId();
}

// =============================================================================
// Socket functions (Windows)
// =============================================================================

bool Platform_InitSockets(void) {
	if (g_socketsInitialized) return true;

	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		return false;
	}
	g_socketsInitialized = true;
	return true;
}

void Platform_CleanupSockets(void) {
	if (g_socketsInitialized) {
		WSACleanup();
		g_socketsInitialized = false;
	}
}

SocketHandle Platform_CreateSocket(int af, int type, int protocol) {
	return socket(af, type, protocol);
}

int Platform_CloseSocket(SocketHandle socket) {
	return closesocket(socket);
}

int Platform_SetSocketOption(SocketHandle socket, int level, int optname, const void* optval, int optlen) {
	return setsockopt(socket, level, optname, (const char*)optval, optlen);
}

int Platform_Bind(SocketHandle socket, const struct sockaddr* addr, int addrlen) {
	return bind(socket, addr, addrlen);
}

int Platform_Listen(SocketHandle socket, int backlog) {
	return listen(socket, backlog);
}

SocketHandle Platform_Accept(SocketHandle socket, struct sockaddr* addr, int* addrlen) {
	return accept(socket, addr, addrlen);
}

int Platform_Send(SocketHandle socket, const void* buf, int len, int flags) {
	return send(socket, (const char*)buf, len, flags);
}

int Platform_Recv(SocketHandle socket, void* buf, int len, int flags) {
	return recv(socket, (char*)buf, len, flags);
}

int Platform_SetNonBlocking(SocketHandle socket, bool nonBlocking) {
	u_long mode = nonBlocking ? 1 : 0;
	return ioctlsocket(socket, FIONBIO, &mode);
}

int Platform_GetLastSocketError(void) {
	return WSAGetLastError();
}

const char* Platform_GetSocketErrorString(int error) {
	static char buffer[256];
	FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		buffer,
		sizeof(buffer),
		NULL
	);
	return buffer;
}

int Platform_Poll(SocketHandle socket, int events, int timeout) {
	fd_set readfds, writefds, exceptfds;
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	if (events & 1) FD_SET(socket, &readfds);   // POLLIN
	if (events & 4) FD_SET(socket, &writefds);  // POLLOUT
	FD_SET(socket, &exceptfds);

	struct timeval tv;
	struct timeval* ptv = NULL;
	if (timeout >= 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		ptv = &tv;
	}

	int result = select(0, &readfds, &writefds, &exceptfds, ptv);
	if (result > 0) {
		int revents = 0;
		if (FD_ISSET(socket, &readfds)) revents |= 1;
		if (FD_ISSET(socket, &writefds)) revents |= 4;
		if (FD_ISSET(socket, &exceptfds)) revents |= 8;
		return revents;
	}
	return result;
}

// =============================================================================
// Event/Signal functions (Windows)
// =============================================================================

PlatformEvent* Platform_CreateEvent(void) {
	PlatformEvent* event = new PlatformEvent;
	event->handle = CreateEventA(NULL, FALSE, FALSE, NULL);
	event->signalType = PLATFORM_SIGNAL_NONE;
	InitializeCriticalSection(&event->cs);

	if (event->handle == NULL) {
		DeleteCriticalSection(&event->cs);
		delete event;
		return NULL;
	}

	// Register event for current thread
	ThreadID tid = Platform_GetThreadID();
	std::lock_guard<std::mutex> lock(g_threadEventsMutex);
	g_threadEvents[tid] = event;

	return event;
}

void Platform_DestroyEvent(PlatformEvent* event) {
	if (event) {
		// Unregister event
		ThreadID tid = Platform_GetThreadID();
		{
			std::lock_guard<std::mutex> lock(g_threadEventsMutex);
			g_threadEvents.erase(tid);
		}

		CloseHandle(event->handle);
		DeleteCriticalSection(&event->cs);
		delete event;
	}
}

bool Platform_SetEvent(PlatformEvent* event, int signalType) {
	if (!event) return false;
	EnterCriticalSection(&event->cs);
	event->signalType = signalType;
	LeaveCriticalSection(&event->cs);
	return SetEvent(event->handle) != 0;
}

int Platform_WaitForEvent(PlatformEvent* event, int timeout_ms) {
	if (!event) return PLATFORM_SIGNAL_NONE;

	DWORD timeout = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
	DWORD result = WaitForSingleObject(event->handle, timeout);

	if (result == WAIT_OBJECT_0) {
		EnterCriticalSection(&event->cs);
		int sig = event->signalType;
		event->signalType = PLATFORM_SIGNAL_NONE;
		LeaveCriticalSection(&event->cs);
		return sig;
	}

	return PLATFORM_SIGNAL_NONE;
}

bool Platform_ResetEvent(PlatformEvent* event) {
	if (!event) return false;
	return ResetEvent(event->handle) != 0;
}

// =============================================================================
// Timer functions (Windows)
// =============================================================================

static DWORD WINAPI TimerThreadProc(LPVOID param) {
	PlatformTimer* timer = (PlatformTimer*)param;

	while (timer->running) {
		DWORD result = WaitForSingleObject(timer->handle, INFINITE);
		if (result == WAIT_OBJECT_0 && timer->running) {
			InterlockedIncrement((volatile LONG*)&timer->overrunCount);
		}
	}

	return 0;
}

PlatformTimer* Platform_CreateTimer(void) {
	PlatformTimer* timer = new PlatformTimer;
	timer->handle = CreateWaitableTimerA(NULL, FALSE, NULL);
	timer->overrunCount = 0;
	timer->repeating = false;
	timer->interval_ms = 0;
	timer->threadHandle = NULL;
	timer->running = false;

	if (timer->handle == NULL) {
		delete timer;
		return NULL;
	}

	return timer;
}

void Platform_DestroyTimer(PlatformTimer* timer) {
	if (timer) {
		timer->running = false;
		if (timer->threadHandle) {
			SetEvent(timer->handle); // Wake up thread
			WaitForSingleObject(timer->threadHandle, 1000);
			CloseHandle(timer->threadHandle);
		}
		if (timer->handle) {
			CancelWaitableTimer(timer->handle);
			CloseHandle(timer->handle);
		}
		delete timer;
	}
}

bool Platform_SetTimer(PlatformTimer* timer, int interval_ms, bool repeating) {
	if (!timer) return false;

	timer->interval_ms = interval_ms;
	timer->repeating = repeating;
	timer->overrunCount = 0;

	LARGE_INTEGER dueTime;
	dueTime.QuadPart = -((LONGLONG)interval_ms * 10000); // Negative = relative time

	LONG period = repeating ? interval_ms : 0;

	if (!SetWaitableTimer(timer->handle, &dueTime, period, NULL, NULL, FALSE)) {
		return false;
	}

	return true;
}

bool Platform_CancelTimer(PlatformTimer* timer) {
	if (!timer) return false;
	return CancelWaitableTimer(timer->handle) != 0;
}

int Platform_GetTimerOverrun(PlatformTimer* timer) {
	if (!timer) return 0;
	return InterlockedExchange((volatile LONG*)&timer->overrunCount, 0);
}

// =============================================================================
// Mutex functions (Windows)
// =============================================================================

void Platform_InitMutex(PlatformMutex* mutex) {
	InitializeCriticalSection(mutex);
}

void Platform_DestroyMutex(PlatformMutex* mutex) {
	DeleteCriticalSection(mutex);
}

void Platform_LockMutex(PlatformMutex* mutex) {
	EnterCriticalSection(mutex);
}

void Platform_UnlockMutex(PlatformMutex* mutex) {
	LeaveCriticalSection(mutex);
}

// =============================================================================
// Condition Variable functions (Windows)
// =============================================================================

void Platform_InitCondVar(PlatformCondVar* cond) {
	InitializeConditionVariable(cond);
}

void Platform_DestroyCondVar(PlatformCondVar* cond) {
	// Windows condition variables don't need explicit destruction
	(void)cond;
}

void Platform_WaitCondVar(PlatformCondVar* cond, PlatformMutex* mutex) {
	SleepConditionVariableCS(cond, mutex, INFINITE);
}

void Platform_SignalCondVar(PlatformCondVar* cond) {
	WakeConditionVariable(cond);
}

// =============================================================================
// Shared Memory / IPC functions (Windows)
// =============================================================================

FileMapHandle Platform_CreateSharedMemory(const char* name, size_t size) {
	char mappingName[256];
	snprintf(mappingName, sizeof(mappingName), "Local\\TibiaServer_%s", name);

	HANDLE handle = CreateFileMappingA(
		INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		(DWORD)(size >> 32),
		(DWORD)(size & 0xFFFFFFFF),
		mappingName
	);

	return handle;
}

FileMapHandle Platform_OpenSharedMemory(const char* name) {
	char mappingName[256];
	snprintf(mappingName, sizeof(mappingName), "Local\\TibiaServer_%s", name);

	return OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
}

void* Platform_MapSharedMemory(FileMapHandle handle, size_t size) {
	if (handle == INVALID_FILEMAP_HANDLE) return NULL;
	return MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
}

void Platform_UnmapSharedMemory(void* ptr, size_t size) {
	(void)size;
	if (ptr) {
		UnmapViewOfFile(ptr);
	}
}

void Platform_CloseSharedMemory(FileMapHandle handle) {
	if (handle != INVALID_FILEMAP_HANDLE) {
		CloseHandle(handle);
	}
}

void Platform_DeleteSharedMemory(const char* name) {
	// Windows shared memory is automatically deleted when all handles are closed
	(void)name;
}

// =============================================================================
// Directory operations (Windows)
// =============================================================================

PlatformDir* Platform_OpenDir(const char* path) {
	PlatformDir* dir = new PlatformDir;

	snprintf(dir->path, sizeof(dir->path), "%s\\*", path);

	dir->handle = FindFirstFileA(dir->path, &dir->findData);
	if (dir->handle == INVALID_HANDLE_VALUE) {
		delete dir;
		return NULL;
	}

	dir->firstRead = true;
	return dir;
}

bool Platform_ReadDir(PlatformDir* dir, PlatformDirEntry* entry) {
	if (!dir || !entry) return false;

	if (dir->firstRead) {
		dir->firstRead = false;
	} else {
		if (!FindNextFileA(dir->handle, &dir->findData)) {
			return false;
		}
	}

	strncpy(entry->name, dir->findData.cFileName, sizeof(entry->name) - 1);
	entry->name[sizeof(entry->name) - 1] = 0;
	entry->isDirectory = (dir->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

	return true;
}

void Platform_CloseDir(PlatformDir* dir) {
	if (dir) {
		if (dir->handle != INVALID_HANDLE_VALUE) {
			FindClose(dir->handle);
		}
		delete dir;
	}
}

// =============================================================================
// File operations (Windows)
// =============================================================================

int Platform_Unlink(const char* path) {
	return DeleteFileA(path) ? 0 : -1;
}

int Platform_Chdir(const char* path) {
	return SetCurrentDirectoryA(path) ? 0 : -1;
}

int Platform_Mkdir(const char* path) {
	return CreateDirectoryA(path, NULL) ? 0 : -1;
}

bool Platform_FileExists(const char* path) {
	DWORD attrs = GetFileAttributesA(path);
	return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// =============================================================================
// Error handling (Windows)
// =============================================================================

int Platform_GetLastError(void) {
	return GetLastError();
}

const char* Platform_GetErrorString(int error) {
	static char buffer[256];
	FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		buffer,
		sizeof(buffer),
		NULL
	);
	// Remove trailing newline
	size_t len = strlen(buffer);
	while (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
		buffer[--len] = 0;
	}
	return buffer;
}

// =============================================================================
// Misc functions (Windows)
// =============================================================================

void Platform_Sleep(int milliseconds) {
	Sleep(milliseconds);
}

void Platform_Yield(void) {
	SwitchToThread();
}

static ConsoleCtrlHandler g_consoleHandler = NULL;

static BOOL WINAPI ConsoleCtrlHandlerProc(DWORD ctrlType) {
	if (g_consoleHandler) {
		int signal = PLATFORM_SIGNAL_NONE;
		switch (ctrlType) {
			case CTRL_C_EVENT:
				signal = PLATFORM_SIGNAL_INT;
				break;
			case CTRL_BREAK_EVENT:
			case CTRL_CLOSE_EVENT:
				signal = PLATFORM_SIGNAL_TERM;
				break;
			default:
				return FALSE;
		}
		g_consoleHandler(signal);
		return TRUE;
	}
	return FALSE;
}

bool Platform_SetConsoleCtrlHandler(ConsoleCtrlHandler handler) {
	g_consoleHandler = handler;
	return SetConsoleCtrlHandler(ConsoleCtrlHandlerProc, handler != NULL) != 0;
}

bool Platform_SignalThread(ThreadID threadId, int signalType) {
	std::lock_guard<std::mutex> lock(g_threadEventsMutex);
	auto it = g_threadEvents.find(threadId);
	if (it != g_threadEvents.end()) {
		return Platform_SetEvent(it->second, signalType);
	}
	return false;
}

// =============================================================================
// Linux Implementation
// =============================================================================

#elif OS_LINUX

#include <sys/syscall.h>

// =============================================================================
// Platform Event Structure (Linux)
// =============================================================================

struct PlatformEvent {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	volatile int signalType;
	volatile bool signaled;
};

struct PlatformTimer {
	timer_t handle;
	int overrunCount;
};

struct PlatformDir {
	DIR* handle;
};

// =============================================================================
// Process and Thread functions (Linux)
// =============================================================================

ProcessID Platform_GetProcessID(void) {
	return getpid();
}

ThreadID Platform_GetThreadID(void) {
	return syscall(SYS_gettid);
}

// =============================================================================
// Socket functions (Linux)
// =============================================================================

bool Platform_InitSockets(void) {
	return true; // No initialization needed on Linux
}

void Platform_CleanupSockets(void) {
	// No cleanup needed on Linux
}

SocketHandle Platform_CreateSocket(int af, int type, int protocol) {
	return socket(af, type, protocol);
}

int Platform_CloseSocket(SocketHandle socket) {
	return close(socket);
}

int Platform_SetSocketOption(SocketHandle socket, int level, int optname, const void* optval, int optlen) {
	return setsockopt(socket, level, optname, optval, optlen);
}

int Platform_Bind(SocketHandle socket, const struct sockaddr* addr, int addrlen) {
	return bind(socket, addr, addrlen);
}

int Platform_Listen(SocketHandle socket, int backlog) {
	return listen(socket, backlog);
}

SocketHandle Platform_Accept(SocketHandle socket, struct sockaddr* addr, int* addrlen) {
	socklen_t len = addrlen ? (socklen_t)*addrlen : 0;
	SocketHandle result = accept(socket, addr, addrlen ? &len : NULL);
	if (addrlen) *addrlen = (int)len;
	return result;
}

int Platform_Send(SocketHandle socket, const void* buf, int len, int flags) {
	return send(socket, buf, len, flags);
}

int Platform_Recv(SocketHandle socket, void* buf, int len, int flags) {
	return recv(socket, buf, len, flags);
}

int Platform_SetNonBlocking(SocketHandle socket, bool nonBlocking) {
	int flags = fcntl(socket, F_GETFL, 0);
	if (flags == -1) return -1;

	if (nonBlocking) {
		flags |= O_NONBLOCK;
	} else {
		flags &= ~O_NONBLOCK;
	}

	return fcntl(socket, F_SETFL, flags);
}

int Platform_GetLastSocketError(void) {
	return errno;
}

const char* Platform_GetSocketErrorString(int error) {
	return strerror(error);
}

int Platform_Poll(SocketHandle socket, int events, int timeout) {
	struct pollfd pfd = {};
	pfd.fd = socket;
	pfd.events = (short)events;

	int result = poll(&pfd, 1, timeout);
	if (result > 0) {
		return pfd.revents;
	}
	return result;
}

// =============================================================================
// Event/Signal functions (Linux)
// =============================================================================

PlatformEvent* Platform_CreateEvent(void) {
	PlatformEvent* event = new PlatformEvent;

	if (pthread_mutex_init(&event->mutex, NULL) != 0) {
		delete event;
		return NULL;
	}

	if (pthread_cond_init(&event->cond, NULL) != 0) {
		pthread_mutex_destroy(&event->mutex);
		delete event;
		return NULL;
	}

	event->signalType = PLATFORM_SIGNAL_NONE;
	event->signaled = false;
	return event;
}

void Platform_DestroyEvent(PlatformEvent* event) {
	if (event) {
		pthread_cond_destroy(&event->cond);
		pthread_mutex_destroy(&event->mutex);
		delete event;
	}
}

bool Platform_SetEvent(PlatformEvent* event, int signalType) {
	if (!event) return false;

	pthread_mutex_lock(&event->mutex);
	event->signalType = signalType;
	event->signaled = true;
	pthread_cond_signal(&event->cond);
	pthread_mutex_unlock(&event->mutex);

	return true;
}

int Platform_WaitForEvent(PlatformEvent* event, int timeout_ms) {
	if (!event) return PLATFORM_SIGNAL_NONE;

	pthread_mutex_lock(&event->mutex);

	if (timeout_ms < 0) {
		while (!event->signaled) {
			pthread_cond_wait(&event->cond, &event->mutex);
		}
	} else {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout_ms / 1000;
		ts.tv_nsec += (timeout_ms % 1000) * 1000000;
		if (ts.tv_nsec >= 1000000000) {
			ts.tv_sec += 1;
			ts.tv_nsec -= 1000000000;
		}

		while (!event->signaled) {
			if (pthread_cond_timedwait(&event->cond, &event->mutex, &ts) != 0) {
				break;
			}
		}
	}

	int sig = event->signaled ? event->signalType : PLATFORM_SIGNAL_NONE;
	event->signaled = false;
	event->signalType = PLATFORM_SIGNAL_NONE;
	pthread_mutex_unlock(&event->mutex);

	return sig;
}

bool Platform_ResetEvent(PlatformEvent* event) {
	if (!event) return false;

	pthread_mutex_lock(&event->mutex);
	event->signaled = false;
	event->signalType = PLATFORM_SIGNAL_NONE;
	pthread_mutex_unlock(&event->mutex);

	return true;
}

// =============================================================================
// Timer functions (Linux)
// =============================================================================

PlatformTimer* Platform_CreateTimer(void) {
	PlatformTimer* timer = new PlatformTimer;
	timer->handle = 0;
	timer->overrunCount = 0;
	return timer;
}

void Platform_DestroyTimer(PlatformTimer* timer) {
	if (timer) {
		if (timer->handle != 0) {
			timer_delete(timer->handle);
		}
		delete timer;
	}
}

bool Platform_SetTimer(PlatformTimer* timer, int interval_ms, bool repeating) {
	if (!timer) return false;

	if (timer->handle == 0) {
		struct sigevent sev = {};
		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGALRM;

		if (timer_create(CLOCK_MONOTONIC, &sev, &timer->handle) == -1) {
			return false;
		}
	}

	struct itimerspec its = {};
	its.it_value.tv_sec = interval_ms / 1000;
	its.it_value.tv_nsec = (interval_ms % 1000) * 1000000;

	if (repeating) {
		its.it_interval = its.it_value;
	}

	return timer_settime(timer->handle, 0, &its, NULL) == 0;
}

bool Platform_CancelTimer(PlatformTimer* timer) {
	if (!timer || timer->handle == 0) return false;

	struct itimerspec its = {};
	return timer_settime(timer->handle, 0, &its, NULL) == 0;
}

int Platform_GetTimerOverrun(PlatformTimer* timer) {
	if (!timer || timer->handle == 0) return 0;
	return timer_getoverrun(timer->handle);
}

// =============================================================================
// Mutex functions (Linux)
// =============================================================================

void Platform_InitMutex(PlatformMutex* mutex) {
	pthread_mutex_init(mutex, NULL);
}

void Platform_DestroyMutex(PlatformMutex* mutex) {
	pthread_mutex_destroy(mutex);
}

void Platform_LockMutex(PlatformMutex* mutex) {
	pthread_mutex_lock(mutex);
}

void Platform_UnlockMutex(PlatformMutex* mutex) {
	pthread_mutex_unlock(mutex);
}

// =============================================================================
// Condition Variable functions (Linux)
// =============================================================================

void Platform_InitCondVar(PlatformCondVar* cond) {
	pthread_cond_init(cond, NULL);
}

void Platform_DestroyCondVar(PlatformCondVar* cond) {
	pthread_cond_destroy(cond);
}

void Platform_WaitCondVar(PlatformCondVar* cond, PlatformMutex* mutex) {
	pthread_cond_wait(cond, mutex);
}

void Platform_SignalCondVar(PlatformCondVar* cond) {
	pthread_cond_signal(cond);
}

// =============================================================================
// Shared Memory / IPC functions (Linux)
// =============================================================================

FileMapHandle Platform_CreateSharedMemory(const char* name, size_t size) {
	(void)name; // Linux uses key, not name directly
	key_t key = ftok("/tmp", 'T');
	return shmget(key, size, IPC_CREAT | IPC_EXCL | 0777);
}

FileMapHandle Platform_OpenSharedMemory(const char* name) {
	(void)name;
	key_t key = ftok("/tmp", 'T');
	return shmget(key, 0, 0);
}

void* Platform_MapSharedMemory(FileMapHandle handle, size_t size) {
	(void)size;
	if (handle == INVALID_FILEMAP_HANDLE) return NULL;
	void* ptr = shmat(handle, NULL, 0);
	return (ptr == (void*)-1) ? NULL : ptr;
}

void Platform_UnmapSharedMemory(void* ptr, size_t size) {
	(void)size;
	if (ptr) {
		shmdt(ptr);
	}
}

void Platform_CloseSharedMemory(FileMapHandle handle) {
	// Nothing to do for Linux, handle is just the shmid
	(void)handle;
}

void Platform_DeleteSharedMemory(const char* name) {
	(void)name;
	key_t key = ftok("/tmp", 'T');
	int shmid = shmget(key, 0, 0);
	if (shmid != -1) {
		shmctl(shmid, IPC_RMID, NULL);
	}
}

// =============================================================================
// Directory operations (Linux)
// =============================================================================

PlatformDir* Platform_OpenDir(const char* path) {
	PlatformDir* dir = new PlatformDir;
	dir->handle = opendir(path);
	if (dir->handle == NULL) {
		delete dir;
		return NULL;
	}
	return dir;
}

bool Platform_ReadDir(PlatformDir* dir, PlatformDirEntry* entry) {
	if (!dir || !entry) return false;

	struct dirent* de = readdir(dir->handle);
	if (de == NULL) return false;

	strncpy(entry->name, de->d_name, sizeof(entry->name) - 1);
	entry->name[sizeof(entry->name) - 1] = 0;
	entry->isDirectory = (de->d_type == DT_DIR);

	return true;
}

void Platform_CloseDir(PlatformDir* dir) {
	if (dir) {
		if (dir->handle) {
			closedir(dir->handle);
		}
		delete dir;
	}
}

// =============================================================================
// File operations (Linux)
// =============================================================================

int Platform_Unlink(const char* path) {
	return unlink(path);
}

int Platform_Chdir(const char* path) {
	return chdir(path);
}

int Platform_Mkdir(const char* path) {
	return mkdir(path, 0755);
}

bool Platform_FileExists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// =============================================================================
// Error handling (Linux)
// =============================================================================

int Platform_GetLastError(void) {
	return errno;
}

const char* Platform_GetErrorString(int error) {
	return strerror(error);
}

// =============================================================================
// Misc functions (Linux)
// =============================================================================

void Platform_Sleep(int milliseconds) {
	usleep(milliseconds * 1000);
}

void Platform_Yield(void) {
	sched_yield();
}

static ConsoleCtrlHandler g_consoleHandler = NULL;

static void SignalHandlerProc(int signum) {
	if (g_consoleHandler) {
		int signal = PLATFORM_SIGNAL_NONE;
		switch (signum) {
			case SIGINT: signal = PLATFORM_SIGNAL_INT; break;
			case SIGTERM: signal = PLATFORM_SIGNAL_TERM; break;
			case SIGHUP: signal = PLATFORM_SIGNAL_HUP; break;
			default: return;
		}
		g_consoleHandler(signal);
	}
}

bool Platform_SetConsoleCtrlHandler(ConsoleCtrlHandler handler) {
	g_consoleHandler = handler;

	struct sigaction sa = {};
	sa.sa_handler = handler ? SignalHandlerProc : SIG_DFL;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	return true;
}

bool Platform_SignalThread(ThreadID threadId, int signalType) {
	int signum;
	switch (signalType) {
		case PLATFORM_SIGNAL_IO: signum = SIGIO; break;
		case PLATFORM_SIGNAL_USR1: signum = SIGUSR1; break;
		case PLATFORM_SIGNAL_USR2: signum = SIGUSR2; break;
		case PLATFORM_SIGNAL_ALARM: signum = SIGALRM; break;
		case PLATFORM_SIGNAL_HUP: signum = SIGHUP; break;
		case PLATFORM_SIGNAL_PIPE: signum = SIGPIPE; break;
		case PLATFORM_SIGNAL_TERM: signum = SIGTERM; break;
		case PLATFORM_SIGNAL_INT: signum = SIGINT; break;
		default: return false;
	}

	return tgkill(Platform_GetProcessID(), threadId, signum) == 0;
}

#endif // OS_LINUX
