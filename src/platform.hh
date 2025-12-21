#ifndef TIBIA_PLATFORM_HH_
#define TIBIA_PLATFORM_HH_ 1

// =============================================================================
// Platform Abstraction Layer
// =============================================================================
// This header provides cross-platform abstractions for:
// - Threading (threads, mutexes, condition variables)
// - Sockets (BSD sockets / Winsock)
// - Signals (Unix signals / Windows events)
// - Timers (POSIX timers / Windows timers)
// - Process/Thread IDs
// - Directory operations
// - Shared memory / IPC

#include "common.hh"

// Platform detection is already done in common.hh
// OS_WINDOWS and OS_LINUX are defined there

// =============================================================================
// Platform-specific includes
// =============================================================================

#if OS_WINDOWS
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	include <io.h>
#	include <direct.h>
#	pragma comment(lib, "ws2_32.lib")
#elif OS_LINUX
#	include <unistd.h>
#	include <errno.h>
#	include <signal.h>
#	include <sys/socket.h>
#	include <sys/stat.h>
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <netdb.h>
#	include <poll.h>
#	include <fcntl.h>
#	include <dirent.h>
#	include <sys/shm.h>
#	include <pthread.h>
#endif

// =============================================================================
// Type definitions
// =============================================================================

#if OS_WINDOWS
	typedef DWORD ProcessID;
	typedef DWORD ThreadID;
	typedef SOCKET SocketHandle;
	typedef HANDLE EventHandle;
	typedef HANDLE TimerHandle;
	typedef HANDLE FileMapHandle;
	typedef CRITICAL_SECTION PlatformMutex;
	typedef CONDITION_VARIABLE PlatformCondVar;

	#define INVALID_SOCKET_HANDLE INVALID_SOCKET
	#define INVALID_EVENT_HANDLE NULL
	#define INVALID_TIMER_HANDLE NULL
	#define INVALID_FILEMAP_HANDLE NULL

	// Socket error codes mapping
	#define PLATFORM_EAGAIN WSAEWOULDBLOCK
	#define PLATFORM_EINTR WSAEINTR
	#define PLATFORM_ECONNRESET WSAECONNRESET
	#define PLATFORM_EPIPE WSAECONNABORTED

#elif OS_LINUX
	typedef pid_t ProcessID;
	typedef pid_t ThreadID;
	typedef int SocketHandle;
	typedef int EventHandle;   // For compatibility, will use different mechanism
	typedef timer_t TimerHandle;
	typedef int FileMapHandle; // shmid
	typedef pthread_mutex_t PlatformMutex;
	typedef pthread_cond_t PlatformCondVar;

	#define INVALID_SOCKET_HANDLE (-1)
	#define INVALID_EVENT_HANDLE (-1)
	#define INVALID_TIMER_HANDLE ((timer_t)0)
	#define INVALID_FILEMAP_HANDLE (-1)

	#define PLATFORM_EAGAIN EAGAIN
	#define PLATFORM_EINTR EINTR
	#define PLATFORM_ECONNRESET ECONNRESET
	#define PLATFORM_EPIPE EPIPE
#endif

// =============================================================================
// Process and Thread functions
// =============================================================================

ProcessID Platform_GetProcessID(void);
ThreadID Platform_GetThreadID(void);

// =============================================================================
// Socket functions
// =============================================================================

bool Platform_InitSockets(void);
void Platform_CleanupSockets(void);

SocketHandle Platform_CreateSocket(int af, int type, int protocol);
int Platform_CloseSocket(SocketHandle socket);
int Platform_SetSocketOption(SocketHandle socket, int level, int optname, const void* optval, int optlen);
int Platform_Bind(SocketHandle socket, const struct sockaddr* addr, int addrlen);
int Platform_Listen(SocketHandle socket, int backlog);
SocketHandle Platform_Accept(SocketHandle socket, struct sockaddr* addr, int* addrlen);
int Platform_Send(SocketHandle socket, const void* buf, int len, int flags);
int Platform_Recv(SocketHandle socket, void* buf, int len, int flags);
int Platform_SetNonBlocking(SocketHandle socket, bool nonBlocking);
int Platform_GetLastSocketError(void);
const char* Platform_GetSocketErrorString(int error);
int Platform_Poll(SocketHandle socket, int events, int timeout);

// =============================================================================
// Event/Signal functions (for thread communication)
// =============================================================================

// Events replace Unix signals for inter-thread communication
struct PlatformEvent;

PlatformEvent* Platform_CreateEvent(void);
void Platform_DestroyEvent(PlatformEvent* event);
bool Platform_SetEvent(PlatformEvent* event, int signalType);
int Platform_WaitForEvent(PlatformEvent* event, int timeout_ms);
bool Platform_ResetEvent(PlatformEvent* event);

// Signal types (cross-platform)
enum PlatformSignal {
	PLATFORM_SIGNAL_NONE = 0,
	PLATFORM_SIGNAL_IO = 1,        // SIGIO equivalent
	PLATFORM_SIGNAL_USR1 = 2,      // SIGUSR1 equivalent
	PLATFORM_SIGNAL_USR2 = 3,      // SIGUSR2 equivalent
	PLATFORM_SIGNAL_ALARM = 4,     // SIGALRM equivalent
	PLATFORM_SIGNAL_HUP = 5,       // SIGHUP equivalent
	PLATFORM_SIGNAL_PIPE = 6,      // SIGPIPE equivalent
	PLATFORM_SIGNAL_TERM = 7,      // SIGTERM equivalent
	PLATFORM_SIGNAL_INT = 8,       // SIGINT equivalent
};

// =============================================================================
// Timer functions
// =============================================================================

struct PlatformTimer;

PlatformTimer* Platform_CreateTimer(void);
void Platform_DestroyTimer(PlatformTimer* timer);
bool Platform_SetTimer(PlatformTimer* timer, int interval_ms, bool repeating);
bool Platform_CancelTimer(PlatformTimer* timer);
int Platform_GetTimerOverrun(PlatformTimer* timer);

// =============================================================================
// Mutex functions
// =============================================================================

void Platform_InitMutex(PlatformMutex* mutex);
void Platform_DestroyMutex(PlatformMutex* mutex);
void Platform_LockMutex(PlatformMutex* mutex);
void Platform_UnlockMutex(PlatformMutex* mutex);

// =============================================================================
// Condition Variable functions
// =============================================================================

void Platform_InitCondVar(PlatformCondVar* cond);
void Platform_DestroyCondVar(PlatformCondVar* cond);
void Platform_WaitCondVar(PlatformCondVar* cond, PlatformMutex* mutex);
void Platform_SignalCondVar(PlatformCondVar* cond);

// =============================================================================
// Shared Memory / IPC functions
// =============================================================================

FileMapHandle Platform_CreateSharedMemory(const char* name, size_t size);
FileMapHandle Platform_OpenSharedMemory(const char* name);
void* Platform_MapSharedMemory(FileMapHandle handle, size_t size);
void Platform_UnmapSharedMemory(void* ptr, size_t size);
void Platform_CloseSharedMemory(FileMapHandle handle);
void Platform_DeleteSharedMemory(const char* name);

// =============================================================================
// Directory operations
// =============================================================================

struct PlatformDir;
struct PlatformDirEntry {
	char name[260];
	bool isDirectory;
};

PlatformDir* Platform_OpenDir(const char* path);
bool Platform_ReadDir(PlatformDir* dir, PlatformDirEntry* entry);
void Platform_CloseDir(PlatformDir* dir);

// =============================================================================
// File operations
// =============================================================================

int Platform_Unlink(const char* path);
int Platform_Chdir(const char* path);
int Platform_Mkdir(const char* path);
bool Platform_FileExists(const char* path);

// =============================================================================
// Error handling
// =============================================================================

int Platform_GetLastError(void);
const char* Platform_GetErrorString(int error);

// =============================================================================
// Misc functions
// =============================================================================

void Platform_Sleep(int milliseconds);
void Platform_Yield(void);

// Console control handler (for Ctrl+C, etc.)
typedef void (*ConsoleCtrlHandler)(int signal);
bool Platform_SetConsoleCtrlHandler(ConsoleCtrlHandler handler);

// =============================================================================
// Thread signaling (send signal/event to specific thread)
// =============================================================================

bool Platform_SignalThread(ThreadID threadId, int signalType);

#endif //TIBIA_PLATFORM_HH_
