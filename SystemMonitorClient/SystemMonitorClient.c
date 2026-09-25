#include <Windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "..\SystemMonitor\SystemMonitorCommon.h"

int main()
{
	HANDLE hDevice = CreateFileW(L"\\\\.\\SystemMonitor", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		wprintf(L"Failed to open device. Error: %lu\n", GetLastError());
		return 1;
	}

	DWORD bytesReturned;
	MonitorEvent events[0x30];
	if (!DeviceIoControl(hDevice, IOCTL_GET_EVENTS, NULL, 0, events, sizeof(events), &bytesReturned, NULL)) {
		wprintf(L"Failed to get events. Error: %lu\n", GetLastError());
	}
	CloseHandle(hDevice);

	for (DWORD i = 0; i < bytesReturned / sizeof(MonitorEvent); ++i) {
		const MonitorEvent* event = &events[i];
		switch (event->EventType) {
		case ProcessCreated:
		{
			const int commandLineLengthChars = (int)event->Data.ProcessCreated.CommandLineLength;

			wprintf(L"Process Created: PID=%lu, Parent PID=%lu, Command Line=%.*ls, Created Time=%llu\n",
				event->Data.ProcessCreated.ProcessId,
				event->Data.ProcessCreated.ParentProcessId,
				commandLineLengthChars,
				event->Data.ProcessCreated.CommandLine,
				event->Data.ProcessCreated.CreatedTime);
			break;
		}
		case ProcessExited:
			wprintf(L"Process Exited: PID=%lu, Exit Code=%lu, Exit Time=%llu\n",
				event->Data.ProcessExited.ProcessId,
				event->Data.ProcessExited.ExitCode,
				event->Data.ProcessExited.ExitTime);
			break;
		case ThreadCreated:
			wprintf(L"Thread Created: TID=%lu, PID=%lu, Created Time=%llu\n",
				event->Data.ThreadCreated.ThreadId,
				event->Data.ThreadCreated.ProcessId,
				event->Data.ThreadCreated.CreatedTime);
			break;
		case ThreadExited:
			wprintf(L"Thread Exited: TID=%lu, PID=%lu, Exit Code=%lu, Exit Time=%llu\n",
				event->Data.ThreadExited.ThreadId,
				event->Data.ThreadExited.ProcessId,
				event->Data.ThreadExited.ExitCode,
				event->Data.ThreadExited.ExitTime);
			break;
		case RegistrySetValue:
			wprintf(L"Registry Set Value: Key=%.*ls, Value=%.*ls, Data Type=%lu, Data Size=%lu, Provided Data Size=%lu, Process ID=%lu, Thread ID=%lu, Time=%llu\n",
				(event->Data.RegistrySetValue.KeyNameOffset > 0) ? (int)(event->Data.RegistrySetValue.KeyNameOffset / sizeof(WCHAR)) : 0,
				(event->Data.RegistrySetValue.KeyNameOffset > 0) ? (PWSTR)((PUCHAR)event + event->Data.RegistrySetValue.KeyNameOffset) : L"",
				(event->Data.RegistrySetValue.ValueNameOffset > 0) ? (int)(event->Data.RegistrySetValue.ValueNameOffset / sizeof(WCHAR)) : 0,
				(event->Data.RegistrySetValue.ValueNameOffset > 0) ? (PWSTR)((PUCHAR)event + event->Data.RegistrySetValue.ValueNameOffset) : L"",
				event->Data.RegistrySetValue.DataType,
				event->Data.RegistrySetValue.DataSize,
				event->Data.RegistrySetValue.ProvidedDataSize,
				event->Data.RegistrySetValue.ProcessId,
				event->Data.RegistrySetValue.ThreadId,
				event->Data.RegistrySetValue.TIME);
			break;
		default:
			wprintf(L"Unknown event type: %d\n", event->EventType);
			break;
		}
	}
}
