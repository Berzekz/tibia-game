#ifndef TIBIA_COMPAT_HH_
#define TIBIA_COMPAT_HH_ 1

// =============================================================================
// Cross-platform Compatibility Layer
// =============================================================================
// This header provides compatibility wrappers for platform-specific APIs

#include "common.hh"

// =============================================================================
// Directory Operations
// =============================================================================

#if OS_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <windows.h>

// Emulate POSIX dirent for Windows
struct dirent {
	char d_name[260];
	unsigned char d_type;
};

// File type constants
#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4

struct DIR {
	HANDLE handle;
	WIN32_FIND_DATAA findData;
	struct dirent entry;
	bool firstRead;
	char path[MAX_PATH];
};

inline DIR* opendir(const char* path) {
	DIR* dir = new DIR;

	snprintf(dir->path, sizeof(dir->path), "%s\\*", path);

	dir->handle = FindFirstFileA(dir->path, &dir->findData);
	if (dir->handle == INVALID_HANDLE_VALUE) {
		delete dir;
		return NULL;
	}

	dir->firstRead = true;
	return dir;
}

inline struct dirent* readdir(DIR* dir) {
	if (dir == NULL) return NULL;

	if (dir->firstRead) {
		dir->firstRead = false;
	} else {
		if (!FindNextFileA(dir->handle, &dir->findData)) {
			return NULL;
		}
	}

	strncpy(dir->entry.d_name, dir->findData.cFileName, sizeof(dir->entry.d_name) - 1);
	dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = 0;

	if (dir->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
		dir->entry.d_type = DT_DIR;
	} else {
		dir->entry.d_type = DT_REG;
	}

	return &dir->entry;
}

inline int closedir(DIR* dir) {
	if (dir) {
		if (dir->handle != INVALID_HANDLE_VALUE) {
			FindClose(dir->handle);
		}
		delete dir;
	}
	return 0;
}

#elif OS_LINUX

#include <dirent.h>

#endif // OS_WINDOWS

// =============================================================================
// String functions compatibility
// =============================================================================

#if OS_WINDOWS

// Windows doesn't have strerror variants with the same names
inline const char* strerrordesc_np(int errnum) {
	static char buffer[256];
	strerror_s(buffer, sizeof(buffer), errnum);
	return buffer;
}

inline const char* sigdescr_np(int sig) {
	static char buffer[64];
	snprintf(buffer, sizeof(buffer), "signal %d", sig);
	return buffer;
}

// rand_r replacement (Windows doesn't have it)
inline int rand_r(unsigned int* seedp) {
	*seedp = *seedp * 1103515245 + 12345;
	return (*seedp >> 16) & 0x7fff;
}

// alloca is different on Windows
#include <malloc.h>
#define alloca _alloca

#endif // OS_WINDOWS

#endif // TIBIA_COMPAT_HH_
