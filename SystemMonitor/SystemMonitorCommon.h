#pragma once

#define SYSTEM_MONITOR_DEVICE 0x8000
#define IOCTL_GET_EVENTS CTL_CODE(SYSTEM_MONITOR_DEVICE, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define MAX_COMMAND_LINE_LENGTH 1024

typedef struct _ProcessCreatedInfo
{
	ULONGLONG CreatedTime;
	ULONG ProcessId;
	ULONG ParentProcessId;
	ULONG CommandLineLength;
	WCHAR CommandLine[MAX_COMMAND_LINE_LENGTH];
} ProcessCreatedInfo;

typedef struct _ProcessExitedInfo
{
	ULONGLONG ExitTime;
	ULONG ProcessId;
	ULONG ExitCode;
} ProcessExitedInfo;

typedef struct _ThreadCreatedInfo
{
	ULONGLONG CreatedTime;
	ULONG ThreadId;
	ULONG ProcessId;
} ThreadCreatedInfo;

typedef struct _ThreadExitedInfo
{
	ULONGLONG ExitTime;
	ULONG ProcessId;
	ULONG ThreadId;
	ULONG ExitCode;
} ThreadExitedInfo;

typedef enum _MonitorEventType
{
	ProcessCreated,
	ProcessExited,
	ThreadCreated,
	ThreadExited
} MonitorEventType;

typedef struct _MonitorEvent
{
	MonitorEventType EventType;
	union
	{
		ProcessCreatedInfo ProcessCreated;
		ProcessExitedInfo ProcessExited;
		ThreadCreatedInfo ThreadCreated;
		ThreadExitedInfo ThreadExited;
	} Data;
} MonitorEvent;