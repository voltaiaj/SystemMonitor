#include <Windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "..\SystemMonitor\SystemMonitorCommon.h"


void DisplayTime(ULONGLONG);
void DisplayData(const BYTE*, DWORD);

int main()
{
	HANDLE hDevice = CreateFileW(L"\\\\.\\SystemMonitor", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		wprintf(L"Failed to open device. Error: %lu\n", GetLastError());
		return 1;
	}

	BYTE buffer[1 << 16];
	for (;;) {
		DWORD bytesReturned;
		if (!DeviceIoControl(hDevice, IOCTL_GET_EVENTS, NULL, 0, buffer, sizeof(buffer), &bytesReturned, NULL)) {
			wprintf(L"Failed to get events. Error: %lu\n", GetLastError());
			break;
		}

		if (bytesReturned) 
			DisplayData((const BYTE*)buffer, bytesReturned);

		Sleep(1000); // Sleep for a while before polling again
	}

	CloseHandle(hDevice);
	return 0;
}

void DisplayTime(ULONGLONG time)
{
	FILETIME ft;
	SYSTEMTIME st;
	ULARGE_INTEGER uli;
	uli.QuadPart = time;
	ft.dwLowDateTime = uli.LowPart;
	ft.dwHighDateTime = uli.HighPart;
	FileTimeToSystemTime(&ft, &st);
	wprintf(L"%02d/%02d/%04d %02d:%02d:%02d.%03d",
		st.wMonth, st.wDay, st.wYear,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayData(const BYTE* buffer, DWORD size)
{

	while (size > 0) {
		const MonitorEvent* event = (const MonitorEvent*)buffer;
		DisplayTime(event->TimeStamp);
		switch (event->EventType) {
			case ProcessCreated:
			{
				const int commandLineLengthChars = (int)event->Data.ProcessCreated.CommandLineLength;

				wprintf(L"Process Created: PID=%lu, Parent PID=%lu, Command Line=%.*ls\n",
					event->Data.ProcessCreated.ProcessId,
					event->Data.ProcessCreated.ParentProcessId,
					commandLineLengthChars,
					event->Data.ProcessCreated.CommandLine);
				break;
			}
			case ProcessExited:
				wprintf(L"Process Exited: PID=%lu, Exit Code=%lu\n",
					event->Data.ProcessExited.ProcessId,
					event->Data.ProcessExited.ExitCode);
				break;
			case ThreadCreated:
				wprintf(L"Thread Created: TID=%lu, PID=%lu\n",
					event->Data.ThreadCreated.ThreadId,
					event->Data.ThreadCreated.ProcessId);
				break;
			case ThreadExited:
				wprintf(L"Thread Exited: TID=%lu, PID=%lu, Exit Code=%lu\n",
					event->Data.ThreadExited.ThreadId,
					event->Data.ThreadExited.ProcessId,
					event->Data.ThreadExited.ExitCode);
				break;
			case RegistrySetValue:
				wprintf(L"Registry Set Value: Key=%.*ls, Value=%.*ls, Data Type=%lu, Data Size=%lu, Provided Data Size=%lu, Process ID=%lu, Thread ID=%lu\n",
					(event->Data.RegistrySetValue.KeyNameOffset > 0) ? (int)(event->Data.RegistrySetValue.KeyNameOffset / sizeof(WCHAR)) : 0,
					(event->Data.RegistrySetValue.KeyNameOffset > 0) ? (PWSTR)((PUCHAR)event + event->Data.RegistrySetValue.KeyNameOffset) : L"",
					(event->Data.RegistrySetValue.ValueNameOffset > 0) ? (int)(event->Data.RegistrySetValue.ValueNameOffset / sizeof(WCHAR)) : 0,
					(event->Data.RegistrySetValue.ValueNameOffset > 0) ? (PWSTR)((PUCHAR)event + event->Data.RegistrySetValue.ValueNameOffset) : L"",
					event->Data.RegistrySetValue.DataType,
					event->Data.RegistrySetValue.DataSize,
					event->Data.RegistrySetValue.ProvidedDataSize,
					event->Data.RegistrySetValue.ProcessId,
					event->Data.RegistrySetValue.ThreadId);
				break;
			default:
				wprintf(L"Unknown event type: %d\n", event->EventType);
				break;
		}
		buffer += event->Size;
		size -= event->Size;
	}
}