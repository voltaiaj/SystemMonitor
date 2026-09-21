#include <ntifs.h>
#include "SystemMonitorCommon.h"

#define DRIVER_PREFIX "SystemMonitor: "

#define MAX_EVENTS 0x1024

FAST_MUTEX g_Mutex;

typedef struct _Globals
{	
	LIST_ENTRY ItemsHead;
	int ItemCount;
} Globals;

Globals g_Globals;

LARGE_INTEGER GetCurrentTime()
{
	LARGE_INTEGER currentTime;
	KeQuerySystemTime(&currentTime);
	return currentTime;
}

typedef struct _MonitorEventFull
{
	LIST_ENTRY ListEntry;
	MonitorEventType EventType;
	union
	{
		ProcessCreatedInfo ProcessCreated;
		ProcessExitedInfo ProcessExited;
		ThreadCreatedInfo ThreadCreated;
		ThreadExitedInfo ThreadExited;
	} Data;
} MonitorEventFull;

void DriverUnload(PDRIVER_OBJECT);
NTSTATUS SystemMonitorCreateClose(PDEVICE_OBJECT, PIRP);
NTSTATUS SystemMonitorDeviceControl(PDEVICE_OBJECT, PIRP);
void ProcessNotifyCallback(PEPROCESS, HANDLE, PPS_CREATE_NOTIFY_INFO);
void ThreadNotifyCallback(HANDLE, HANDLE, BOOLEAN);
void PushToEventQueue(MonitorEventFull*);
MonitorEvent* PopFromEventQueue();

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	KdPrint((DRIVER_PREFIX "DriverEntry called\n"));
	// Set up the driver unload routine
	DriverObject->DriverUnload = DriverUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = SystemMonitorCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = SystemMonitorCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SystemMonitorDeviceControl;
	// Additional initialization code can go here

	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\SystemMonitor");
	PDEVICE_OBJECT deviceObject = NULL;
	NTSTATUS status = IoCreateDevice(
		DriverObject,
		0,
		&deviceName,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&deviceObject
	);	

	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "Failed to create device (0x%08X)\n", status));
		return status;
	}

	UNICODE_STRING symbolicLinkName = RTL_CONSTANT_STRING(L"\\??\\SystemMonitor");
	status = IoCreateSymbolicLink(&symbolicLinkName, &deviceName);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(deviceObject);
		return status;
	}

	deviceObject->Flags |= DO_BUFFERED_IO;

	// Register process and thread notify callbacks
	status = PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, FALSE);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "Failed to set process notify routine (0x%08X)\n", status));
		IoDeleteSymbolicLink(&symbolicLinkName);
		IoDeleteDevice(deviceObject);
		return status;
	}
	status = PsSetCreateThreadNotifyRoutine(ThreadNotifyCallback);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "Failed to set thread notify routine (0x%08X)\n", status));
		PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, TRUE);
		IoDeleteSymbolicLink(&symbolicLinkName);
		IoDeleteDevice(deviceObject);
		return status;
	}

	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.ItemCount = 0;

	ExInitializeFastMutex(&g_Mutex);

	return STATUS_SUCCESS;
}

void DriverUnload(PDRIVER_OBJECT DriverObject)
{
	KdPrint((DRIVER_PREFIX "DriverUnload called\n"));

	PsRemoveCreateThreadNotifyRoutine(ThreadNotifyCallback);
	PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, TRUE);

	while (!IsListEmpty(&g_Globals.ItemsHead)) {
		PLIST_ENTRY entry = RemoveHeadList(&g_Globals.ItemsHead);
		MonitorEventFull* event = CONTAINING_RECORD(entry, MonitorEventFull, ListEntry);
		ExFreePool2(event, 'evnt', NULL, 0);
		g_Globals.ItemCount--;
	}

	UNICODE_STRING symbolicLinkName = RTL_CONSTANT_STRING(L"\\??\\SystemMonitor");
	IoDeleteSymbolicLink(&symbolicLinkName);
	IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS SystemMonitorCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	KdPrint((DRIVER_PREFIX "CreateClose called\n"));
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS SystemMonitorDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	KdPrint((DRIVER_PREFIX "DeviceControl called\n"));
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	ULONG written = 0;

	switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_GET_EVENTS: {
		KdPrint((DRIVER_PREFIX "IOCTL_GET_EVENTS called\n"));
		ULONG outputBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
		
		while (outputBufferLength >= sizeof(MonitorEvent)) {
			MonitorEvent* event = PopFromEventQueue();
			if (!event) {
				KdPrint((DRIVER_PREFIX "No more events to return\n"));
				break;
			}
			RtlCopyMemory((PUCHAR)Irp->AssociatedIrp.SystemBuffer + written, event, sizeof(MonitorEvent));
			written += sizeof(MonitorEvent);
			outputBufferLength -= sizeof(MonitorEvent);
			ExFreePool2(event, 'evnt', NULL, 0);
		}
		break;
	}
	default: {
		KdPrint((DRIVER_PREFIX "Unknown IOCTL (0x%08X) called\n", irpSp->Parameters.DeviceIoControl.IoControlCode));
		Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
		Irp->IoStatus.Information = 0;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_INVALID_DEVICE_REQUEST;
	}
	}

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = written;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

void ProcessNotifyCallback(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	KdPrint((DRIVER_PREFIX "ProcessNotifyCallback called\n"));
	SIZE_T extraSize = 0;
	if (CreateInfo && CreateInfo->CommandLine) {
		extraSize = CreateInfo->CommandLine->Length;
	}
	
	MonitorEventFull* event = (MonitorEventFull*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MonitorEventFull) + extraSize, 'evnt');
	if (!event) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory for Process event\n"));
		return;
	}

	if (CreateInfo) {
		KdPrint((DRIVER_PREFIX "Process created. PID=%p\n", ProcessId));	
		event->EventType = ProcessCreated;
		event->Data.ProcessCreated.CreatedTime = GetCurrentTime().QuadPart;
		event->Data.ProcessCreated.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
		event->Data.ProcessCreated.ParentProcessId = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;
		if (CreateInfo->CommandLine && CreateInfo->CommandLine->Length > 0) {
			SIZE_T srcChars = CreateInfo->CommandLine->Length / sizeof(WCHAR);
			if (srcChars >= MAX_COMMAND_LINE_LENGTH) {
				srcChars = MAX_COMMAND_LINE_LENGTH - 1;
			}
			RtlCopyMemory(event->Data.ProcessCreated.CommandLine, CreateInfo->CommandLine->Buffer, srcChars * sizeof(WCHAR));
			event->Data.ProcessCreated.CommandLine[srcChars] = L'\0';
			event->Data.ProcessCreated.CommandLineLength = (ULONG)srcChars;
		}
		else {
			event->Data.ProcessCreated.CommandLine[0] = L'\0';
			event->Data.ProcessCreated.CommandLineLength = 0;
		}
		PushToEventQueue(event);
	}
	else {
		KdPrint((DRIVER_PREFIX "Process exited. PID=%p\n", ProcessId));
		event->EventType = ProcessExited;
		event->Data.ProcessExited.ExitTime = GetCurrentTime().QuadPart;
		event->Data.ProcessExited.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
		event->Data.ProcessExited.ExitCode = (ULONG)(ULONG_PTR)PsGetProcessExitStatus(Process);
		PushToEventQueue(event);
	}
}

void ThreadNotifyCallback(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create)
{
	UNREFERENCED_PARAMETER(ProcessId);
	KdPrint((DRIVER_PREFIX "ThreadNotifyCallback called\n"));

	MonitorEventFull* event = (MonitorEventFull*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MonitorEventFull), 'evnt');
	if (!event) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory for Thread event\n"));
		return;
	}

	if (Create) {
		KdPrint((DRIVER_PREFIX "Thread created. TID=%p\n", ThreadId));
		event->EventType = ThreadCreated;
		event->Data.ThreadCreated.CreatedTime = GetCurrentTime().QuadPart;
		event->Data.ThreadCreated.ThreadId = (ULONG)(ULONG_PTR)ThreadId;
		event->Data.ThreadCreated.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
		PushToEventQueue(event);
	}
	else {
		KdPrint((DRIVER_PREFIX "Thread exited. TID=%p\n", ThreadId));
		event->EventType = ThreadExited;
		event->Data.ThreadExited.ExitTime = GetCurrentTime().QuadPart;
		event->Data.ThreadExited.ThreadId = (ULONG)(ULONG_PTR)ThreadId;
		event->Data.ThreadExited.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
		event->Data.ThreadExited.ExitCode = (ULONG)(ULONG_PTR)PsGetThreadExitStatus(PsGetCurrentThread());
		PushToEventQueue(event);
	}
}

void PushToEventQueue(MonitorEventFull* event)
{
	KeWaitForSingleObject(&g_Mutex, Executive, KernelMode, FALSE, NULL);

	if (g_Globals.ItemCount >= MAX_EVENTS) {
		KdPrint((DRIVER_PREFIX "Event queue is full. Making room by removing oldest event.\n"));
		PLIST_ENTRY entry = RemoveHeadList(&g_Globals.ItemsHead);
		MonitorEventFull* oldEvent = CONTAINING_RECORD(entry, MonitorEventFull, ListEntry);
		ExFreePoolWithTag(oldEvent, 'evnt');
		g_Globals.ItemCount--;
	}

	InsertTailList(&g_Globals.ItemsHead, &event->ListEntry);
	g_Globals.ItemCount++;
	
	ExReleaseFastMutex(&g_Mutex);
}

MonitorEvent* ConvertMonitorEventFullToMonitorEvent(MonitorEventFull* fullEvent)
{
	MonitorEvent* event = (MonitorEvent*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MonitorEvent), 'evnt');
	if (!event) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory for MonitorEvent\n"));
		return NULL;
	}
	event->EventType = fullEvent->EventType;
	switch (fullEvent->EventType) {
	case ProcessCreated:
		event->Data.ProcessCreated = fullEvent->Data.ProcessCreated;
		break;
	case ProcessExited:
		event->Data.ProcessExited = fullEvent->Data.ProcessExited;
		break;
	case ThreadCreated:
		event->Data.ThreadCreated = fullEvent->Data.ThreadCreated;
		break;
	case ThreadExited:
		event->Data.ThreadExited = fullEvent->Data.ThreadExited;
		break;
	default:
		KdPrint((DRIVER_PREFIX "Unknown event type: %d\n", fullEvent->EventType));
		ExFreePoolWithTag(event, 'evnt');
		return NULL;
	}
	return event;
}

MonitorEvent* PopFromEventQueue()
{
	KeWaitForSingleObject(&g_Mutex, Executive, KernelMode, FALSE, NULL);
	MonitorEvent* event = NULL;
	if (!IsListEmpty(&g_Globals.ItemsHead)) {
		PLIST_ENTRY entry = RemoveHeadList(&g_Globals.ItemsHead);
		MonitorEventFull* fullEvent = CONTAINING_RECORD(entry, MonitorEventFull, ListEntry);
		event = ConvertMonitorEventFullToMonitorEvent(fullEvent);
		ExFreePoolWithTag(fullEvent, 'evnt');
		g_Globals.ItemCount--;
	}
	ExReleaseFastMutex(&g_Mutex);
	return event;
}