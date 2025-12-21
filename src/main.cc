#include "common.hh"
#include "communication.hh"
#include "config.hh"
#include "houses.hh"
#include "info.hh"
#include "map.hh"
#include "magic.hh"
#include "moveuse.hh"
#include "objects.hh"
#include "operate.hh"
#include "query.hh"
#include "reader.hh"
#include "writer.hh"
#include "compat.hh"

#if OS_WINDOWS
#	include <winsock2.h>
#	include <io.h>
#	include <direct.h>
#elif OS_LINUX
#	include <signal.h>
#	include <sys/stat.h>
#endif

#include <fstream>

// Forward declarations for functions that may not be implemented yet
static void ProcessDecay(void) {
	// TODO: Implement decay processing for items
}

static void ProcessBuddy(void) {
	// TODO: Implement buddy list processing
}

static int GetAmbienteValue(void) {
	int brightness = 0, color = 0;
	GetAmbiente(&brightness, &color);
	return brightness; // Return brightness as the ambiente value
}

static bool BeADaemon = false;
static bool Reboot = false;
static bool SaveMapOn = false;

#if OS_WINDOWS
static HANDLE BeatTimer = NULL;
static volatile LONG SigAlarmCounter = 0;
static volatile LONG SigUsr1Counter = 0;
static volatile bool ShutdownRequested = false;
#elif OS_LINUX
static timer_t BeatTimer;
static int SigAlarmCounter = 0;
static int SigUsr1Counter = 0;

static sighandler_t SigHandler(int SigNr, sighandler_t Handler){
	struct sigaction Action;
	struct sigaction OldAction;

	Action.sa_handler = Handler;

	// TODO(fusion): I feel we should probably use `sigfillset` specially for
	// signals that share the same handler.
	sigemptyset(&Action.sa_mask);

	if(SigNr == SIGALRM){
		Action.sa_flags = SA_INTERRUPT;
	}else{
		Action.sa_flags = SA_RESTART;
	}

	if(sigaction(SigNr, &Action, &OldAction) == 0){
		return OldAction.sa_handler;
	}else{
		return SIG_ERR;
	}
}

static void SigBlock(int SigNr){
	sigset_t Set;
	sigemptyset(&Set);
	sigaddset(&Set, SigNr);
	if(sigprocmask(SIG_BLOCK, &Set, NULL) == -1){
		error("SigBlock: Failed to block signal %d (%s): (%d) %s\n",
				SigNr, sigdescr_np(SigNr), errno, strerrordesc_np(errno));
	}
}

static void SigWaitAny(void){
	sigset_t Set;
	sigemptyset(&Set);
	sigsuspend(&Set);
}

static void SigHupHandler(int signr){
	// no-op (?)
}

static void SigAbortHandler(int signr){
	print(1, "SigAbortHandler: schalte Writer-Thread ab.\n");
	AbortWriter();
}

static void DefaultHandler(int signr){
	print(1, "DefaultHandler: Beende Game-Server (SigNr. %d: %s).\n",
			signr, sigdescr_np(signr));

	SigHandler(SIGINT, SIG_IGN);
	SigHandler(SIGQUIT, SIG_IGN);
	SigHandler(SIGTERM, SIG_IGN);
	SigHandler(SIGXCPU, SIG_IGN);
	SigHandler(SIGXFSZ, SIG_IGN);
	SigHandler(SIGPWR, SIG_IGN);

	SaveMapOn = (signr == SIGQUIT) || (signr == SIGTERM) || (signr == SIGPWR);
	if(signr == SIGTERM){
		int Hour, Minute;
		GetRealTime(&Hour, &Minute);
		RebootTime = (Hour * 60 + Minute + 6) % 1440;
		CloseGame();
	}else{
		EndGame();
	}

	Reboot = false;
}
#endif // OS_LINUX

#if OS_WINDOWS

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType){
	switch(ctrlType){
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
			print(1, "ConsoleCtrlHandler: Beende Game-Server.\n");
			ShutdownRequested = true;
			SaveMapOn = true;
			EndGame();
			return TRUE;
		default:
			return FALSE;
	}
}

static void InitSignalHandler(void){
	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	print(1, "InitSignalHandler: Console control handler installed.\n");
}

static void ExitSignalHandler(void){
	SetConsoleCtrlHandler(NULL, FALSE);
}

static VOID CALLBACK BeatTimerCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired){
	InterlockedIncrement(&SigAlarmCounter);
}

static void InitTime(void){
	ASSERT(Beat > 0);
	SigAlarmCounter = 0;

	if(!CreateTimerQueueTimer(&BeatTimer, NULL, BeatTimerCallback, NULL,
			Beat, Beat, WT_EXECUTEDEFAULT)){
		error("InitTime: Failed to create beat timer: %d\n", GetLastError());
		throw "cannot create beat timer";
	}
}

static void ExitTime(void){
	if(BeatTimer != NULL){
		DeleteTimerQueueTimer(NULL, BeatTimer, INVALID_HANDLE_VALUE);
		BeatTimer = NULL;
	}
}

#else // OS_LINUX

static void InitSignalHandler(void){
	int Count = 0;
	Count += (SigHandler(SIGHUP, SigHupHandler) != SIG_ERR);
	Count += (SigHandler(SIGINT, DefaultHandler) != SIG_ERR);
	Count += (SigHandler(SIGQUIT, DefaultHandler) != SIG_ERR);
	Count += (SigHandler(SIGABRT, SigAbortHandler) != SIG_ERR);
	Count += (SigHandler(SIGUSR1, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGUSR2, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGPIPE, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGALRM, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGTERM, DefaultHandler) != SIG_ERR);
	Count += (SigHandler(SIGSTKFLT, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGCHLD, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGTSTP, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGTTIN, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGTTOU, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGURG, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGXCPU, DefaultHandler) != SIG_ERR);
	Count += (SigHandler(SIGXFSZ, DefaultHandler) != SIG_ERR);
	Count += (SigHandler(SIGVTALRM, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGWINCH, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGPOLL, SIG_IGN) != SIG_ERR);
	Count += (SigHandler(SIGPWR, DefaultHandler) != SIG_ERR);
	print(1, "InitSignalHandler: %d Signalhandler eingerichtet (Soll=%d)\n", Count, 0x1c);
}

static void ExitSignalHandler(void){
	// no-op
}

static void SigAlarmHandler(int SigNr){
	SigAlarmCounter += (1 + timer_getoverrun(BeatTimer));
}

static void InitTime(void){
	ASSERT(Beat > 0);
	SigAlarmCounter = 0;
	SigHandler(SIGALRM, SigAlarmHandler);

	struct sigevent SigEvent = {};
	SigEvent.sigev_notify = SIGEV_THREAD_ID;
	SigEvent.sigev_signo = SIGALRM;
	SigEvent.sigev_notify_thread_id = gettid();
	if(timer_create(CLOCK_MONOTONIC, &SigEvent, &BeatTimer) == -1){
		error("InitTime: Failed to create beat timer: (%d) %s\n",
				errno, strerrordesc_np(errno));
		throw "cannot create beat timer";
	}

	struct itimerspec TimerSpec = {};
	TimerSpec.it_interval.tv_sec = Beat / 1000;
	TimerSpec.it_interval.tv_nsec = (Beat % 1000) * 1000000;
	TimerSpec.it_value = TimerSpec.it_interval;
	if(timer_settime(BeatTimer, 0, &TimerSpec, NULL) == -1){
		error("InitTime: Failed to start beat timer: (%d) %s\n",
				errno, strerrordesc_np(errno));
		throw "cannot start beat timer";
	}
}

static void ExitTime(void){
	if(timer_delete(BeatTimer) == -1){
		error("ExitTime: Failed to delete beat timer: (%d) %s\n",
				errno, strerrordesc_np(errno));
	}

	SigHandler(SIGALRM, SIG_IGN);
}

#endif // OS_LINUX

static void UnlockGame(void){
	// TODO(fusion): Probably use snprintf to format file name?
	char FileName[4096];
	strcpy(FileName, SAVEPATH);
#if OS_WINDOWS
	strcat(FileName, "\\game.pid");
#else
	strcat(FileName, "/game.pid");
#endif

	std::ifstream InputFile(FileName, std::ios_base::in);
	if(!InputFile.fail()){
		int Pid;
		InputFile >> Pid;

		if(Pid == (int)getpid()){
#if OS_WINDOWS
			_unlink(FileName);
#else
			unlink(FileName);
#endif
		}
	}
}

static void LockGame(void){
	// TODO(fusion): Probably use snprintf to format file name?
	char FileName[4096];
	strcpy(FileName, SAVEPATH);
#if OS_WINDOWS
	strcat(FileName, "\\game.pid");
#else
	strcat(FileName, "/game.pid");
#endif

	{
		std::ifstream InputFile(FileName, std::ios_base::in);
		if(!InputFile.fail()){
			int Pid;
			InputFile >> Pid;
			if(Pid != 0){
				throw "Game-Server is already running, PID file exists.";
			}
		}
	}

	{
		std::ofstream OutputFile(FileName, std::ios_base::out | std::ios_base::trunc);
		OutputFile << getpid();
	}

	atexit(UnlockGame);
}

void LoadWorldConfig(void){
	TQueryManagerConnection Connection(KB(16));
	if(!Connection.isConnected()){
		error("LoadWorldConfig: Kann nicht zum Query-Manager verbinden.\n");
		throw "cannot connect to querymanager";
	}

	int HelpWorldType;
	int HelpGameAddress[4];
	int Ret = Connection.loadWorldConfig(&HelpWorldType, &RebootTime,
			HelpGameAddress, &GamePort,
			&MaxPlayers, &PremiumPlayerBuffer,
			&MaxNewbies, &PremiumNewbieBuffer);
	if(Ret != 0){
		error("LoadWorldConfig: Kann Konfigurationsdaten nicht holen.\n");
		throw "cannot load world config";
	}

	WorldType = (TWorldType)HelpWorldType;
	snprintf(GameAddress, sizeof(GameAddress), "%d.%d.%d.%d",
			HelpGameAddress[0], HelpGameAddress[1],
			HelpGameAddress[2], HelpGameAddress[3]);
}

static void InitAll(void){
	try{
		ReadConfig();
		SetQueryManagerLoginData(1, WorldName);
		LoadWorldConfig();
		InitSHM(!BeADaemon);
		LockGame();
		InitLog("game");
		srand((unsigned int)time(NULL));
		InitSignalHandler();
		InitConnections();
		InitCommunication();
		InitStrings();
		InitWriter();
		InitReader();
		InitObjects();
		InitMap();
		InitInfo();
		InitMoveUse();
		InitMagic();
		InitCr();
		InitHouses();
		InitTime();
		ApplyPatches();
	}catch(const char *str){
		error("Initialisierungsfehler: %s\n", str);
		exit(EXIT_FAILURE);
	}
}

static void ExitAll(void){
	EndGame();
	ExitTime();
	ExitCr();
	ExitMagic();
	ExitMoveUse();
	ExitInfo();
	ExitHouses();
	ExitMap(SaveMapOn);
	ExitObjects();
	ExitReader();
	ExitWriter();
	ExitStrings();
	ExitCommunication();
	ExitConnections();
	ExitSignalHandler();
	ExitSHM();
}

static void ProcessCommand(void){
	int Command = GetCommand();
	if(Command != 0){
		char *Buffer = GetCommandBuffer();
		if(Command == 1){
			if(Buffer != NULL){
				BroadcastMessage(TALK_ADMIN_MESSAGE, "%s", Buffer);
			}else{
				error("ProcessCommand: Text für Broadcast ist NULL.\n");
			}
		}else{
			error("ProcessCommand: Unbekanntes Kommando %d.\n", Command);
		}

		SetCommand(0, NULL);
	}
}

static void AdvanceGame(int Delay){
	static int CreatureTimeCounter = 0;
	static int CronTimeCounter = 0;
	static int SkillTimeCounter = 0;
	static int OtherTimeCounter = 0;
	static int OldAmbiente = -1;
	static uint32 NextMinute = 30;
	static bool Lag = false;

	CreatureTimeCounter += Delay;
	CronTimeCounter += Delay;
	SkillTimeCounter += Delay;
	OtherTimeCounter += Delay;

	if(CreatureTimeCounter >= 1750){
		CreatureTimeCounter -= 1000;
		ProcessCreatures();
	}

	if(CronTimeCounter >= 1500){
		CronTimeCounter -= 1000;
		ProcessCronSystem();
	}

	if(SkillTimeCounter >= 1250){
		SkillTimeCounter -= 1000;
		ProcessSkills();
	}

	if(OtherTimeCounter >= 1000){
		OtherTimeCounter -= 1000;

		RoundNr += 1;
		SetRoundNr(RoundNr);

		ProcessConnections();
		ProcessMonsterhomes();
		ProcessMonsterRaids();
		ProcessCommunicationControl();
		ProcessDecay();
		ProcessBuddy();
		ProcessCommand();

		int Ambiente = GetAmbienteValue();
		if(Ambiente != OldAmbiente){
			for(TConnection *C = GetFirstConnection(); C != NULL; C = GetNextConnection()){
				if(C->InGame()){
					SendAmbiente(C);
				}
			}
			OldAmbiente = Ambiente;
		}

		if(RoundNr >= NextMinute){
			int Hour, Minute;
			GetRealTime(&Hour, &Minute);
			int RealTime = Hour * 60 + Minute;

			if((RealTime + 10) % 1440 == RebootTime){
				if(Reboot){
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is saving game in 10 minutes.\nPlease come back in 20 minutes.");
				}else{
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is going down for maintenance in 10 minutes.\nPlease log out.");
				}
			}else if((RealTime + 5) % 1440 == RebootTime){
				if(Reboot){
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is saving game in 5 minutes.\nPlease come back in 15 minutes.");
				}else{
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is going down in 5 minutes.\nPlease log out.");
				}
			}else if((RealTime + 3) % 1440 == RebootTime){
				if(Reboot){
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is saving game in 3 minutes.\nPlease come back in 10 minutes.");
				}else{
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is going down in 3 minutes.\nPlease log out.");
				}
			}else if((RealTime + 1) % 1440 == RebootTime){
				if(Reboot){
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is saving game in one minute.\nPlease log out.");
				}else{
					BroadcastMessage(TALK_ADMIN_MESSAGE,
						"Server is going down in one minute.\nPlease log out.");
				}
			}else if(RealTime == RebootTime){
				CloseGame();
				LogoutAllPlayers();
				SendAll();
				if(Reboot){
					RefreshMap();
				}
				SaveMap();
				SaveMapOn = false;
				EndGame();
			}

			NextMinute = GetRoundForNextMinute();
		}
		CleanupDynamicStrings();
	}

	if(Delay > Beat){
		Log("lag", "Verzögerung %d msec.\n", Delay);
	}

	// TODO(fusion): Why would we delay creature movement yet another beat?
	if(Delay < 1000){
		MoveCreatures(Delay);
		Lag = false;
	}else{
		if(!Lag && RoundNr > 10){
			error("AdvanceGame: Keine Kreaturbewegung wegen Lag (Verzögerung: %d msec).\n", Delay);
		}
		Lag = true;
	}

	SendAll();
}

#if OS_WINDOWS

static void LaunchGame(void){
	SaveMapOn = true;
	SigUsr1Counter = 0;
	SigAlarmCounter = 0;

	StartGame();

	print(1, "LaunchGame: Game-Server ist bereit (Pid=%d, Tid=%d).\n", getpid(), gettid());

	// Windows version uses WaitForSingleObject with GameThreadEvent
	HANDLE gameEvent = GetGameThreadEvent();

	while(GameRunning() && !ShutdownRequested){
		// Wait for either game thread event or timeout
		DWORD waitResult = WaitForSingleObject(gameEvent, 10); // 10ms timeout

		// Check for game thread signal (replaces SIGUSR1)
		if(waitResult == WAIT_OBJECT_0){
			InterlockedExchange(&SigUsr1Counter, 0);
			ReceiveData();
		}

		// Check for beat timer
		LONG numBeats = InterlockedExchange(&SigAlarmCounter, 0);
		if(numBeats > 0){
			AdvanceGame(numBeats * Beat);
		}
	}

	LogoutAllPlayers();
}

#else // OS_LINUX

static void SigUsr1Handler(int signr){
	SigUsr1Counter += 1;
}

static void LaunchGame(void){
	SaveMapOn = true;
	SigUsr1Counter = 0;
	SigAlarmCounter = 0;

	SigBlock(SIGUSR1);
	SigHandler(SIGUSR1, SigUsr1Handler);
	StartGame();

	print(1, "LaunchGame: Game-Server ist bereit (Pid=%d, Tid=%d).\n", getpid(), gettid());

	// IMPORTANT(fusion): In general signal handlers can execute on any thread in
	// the process group but the design of the server is to use signals directed
	// at different threads to communicate certain events (see `CommunicationThread`
	// for example).
	//	Even if that wasn't the case, loads/stores on x86 are always ATOMIC and when
	// there is a single writer (signal handlers), even a read-modify-write will be
	// atomic.
	//	This is to say, there should be no problem with reading from `SigUsr1Counter`,
	// `SigAlarmCounter`, or `SaveMapOn`, which may be modified from signal handlers.

	while(GameRunning()){
		while(SigUsr1Counter == 0 && SigAlarmCounter == 0){
			SigWaitAny();
		}

		if(SigUsr1Counter > 0){
			SigUsr1Counter = 0;
			ReceiveData();
		}

		int NumBeats = SigAlarmCounter;
		if(NumBeats > 0){
			SigAlarmCounter = 0;
			AdvanceGame(NumBeats * Beat);
		}
	}

	LogoutAllPlayers();
}

static bool DaemonInit(bool NoFork){
	if(!NoFork){
		pid_t Pid = fork();
		if(Pid < 0){
			return true;
		}

		if(Pid != 0){
			exit(EXIT_SUCCESS);
		}

		setsid();
	}

	umask(0177);
	chdir(SAVEPATH);

	int OpenMax = sysconf(_SC_OPEN_MAX);
	if(OpenMax < 0){
		OpenMax = 1024;
	}

	for(int fd = 0; fd < OpenMax; fd += 1){
		close(fd);
	}

	return false;
}

#endif // OS_LINUX

#if OS_WINDOWS
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS *ExceptionInfo) {
	fprintf(stderr, "\n=== CRASH DETECTED ===\n");
	fprintf(stderr, "Exception Code: 0x%08X\n", ExceptionInfo->ExceptionRecord->ExceptionCode);
	fprintf(stderr, "Exception Address: %p\n", ExceptionInfo->ExceptionRecord->ExceptionAddress);
	fflush(stderr);
	return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int main(int argc, char **argv){
#if OS_WINDOWS
	// Set up crash handler
	SetUnhandledExceptionFilter(CrashHandler);

	// Initialize Winsock early, before any socket operations
	WSADATA wsaData;
	int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if(wsaResult != 0){
		printf("WSAStartup failed: %d\n", wsaResult);
		return 1;
	}
#endif

	bool NoFork = false;
	BeADaemon = false;
	Reboot = true;

	for(int i = 1; i < argc; i += 1){
		if(strcmp(argv[i], "daemon") == 0){
			BeADaemon = true;
		}else if(strcmp(argv[i], "nofork") == 0){
			NoFork = true;
		}
	}

#if OS_LINUX
	// TODO(fusion): It doesn't make sense for `DaemonInit` to even return here.
	// It either exits the parent or child process, or let it run.
	if(BeADaemon && DaemonInit(NoFork)){
		return 2;
	}
#else
	// Daemon mode not supported on Windows
	if(BeADaemon){
		print(1, "Warning: Daemon mode not supported on Windows.\n");
	}
#endif

	puts("Tibia Game-Server\n(c) by CIP Productions, 2003.\n");

	InitAll();
	atexit(ExitAll);

	// TODO(fusion): The original binary does use exceptions but identifying
	// throw sites using a decompiler is hard work, so instead we just catch
	// any left-over here.
	try{
		LaunchGame();
	}catch(RESULT r){
		error("main: Nicht abgefangene Exception %d.\n", r);
	}catch(const char *str){
		error("main: Nicht abgefangene Exception \"%s\".\n", str);
	}catch(const std::exception &e){
		error("main: Nicht abgefangene Exception %s.\n", e.what());
	}catch(...){
		error("main: Nicht abgefangene Exception unbekannten Typs.\n");
	}

	return Reboot ? 0 : 1;
}
