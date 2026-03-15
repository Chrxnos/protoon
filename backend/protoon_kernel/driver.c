/*
 * Protoon Kernel Driver
 * Kernel-level memory reader for Roblox DataModel extraction
 * Operates beneath Hyperion to read game memory undetected
 * 
 * BUILD REQUIREMENTS:
 * - Windows Driver Kit (WDK) 10
 * - Visual Studio 2022
 * - Code signing certificate OR test signing mode
 * 
 * USAGE:
 * 1. Enable test signing: bcdedit /set testsigning on
 * 2. Reboot
 * 3. sc create ProtoonDrv type= kernel binpath= "C:\path\to\ProtoonDriver.sys"
 * 4. sc start ProtoonDrv
 */

#include <ntddk.h>
#include <wdm.h>

#define PROTOON_DEVICE_NAME L"\\Device\\ProtoonDriver"
#define PROTOON_SYMLINK_NAME L"\\DosDevices\\ProtoonDriver"

// IOCTL codes
#define IOCTL_PROTOON_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOON_GET_MODULE_BASE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOON_GET_PID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Data structures for communication
typedef struct _MEMORY_READ_REQUEST {
    ULONG ProcessId;
    ULONG_PTR Address;
    SIZE_T Size;
    PVOID Buffer;
} MEMORY_READ_REQUEST, *PMEMORY_READ_REQUEST;

typedef struct _MODULE_BASE_REQUEST {
    ULONG ProcessId;
    ULONG_PTR BaseAddress;
} MODULE_BASE_REQUEST, *PMODULE_BASE_REQUEST;

// Global device object
PDEVICE_OBJECT g_DeviceObject = NULL;

// Function to read memory from another process
NTSTATUS ReadProcessMemory(ULONG ProcessId, ULONG_PTR Address, PVOID Buffer, SIZE_T Size) {
    NTSTATUS status = STATUS_SUCCESS;
    PEPROCESS targetProcess = NULL;
    SIZE_T bytesRead = 0;
    
    // Get EPROCESS for target process
    status = PsLookupProcessByProcessId((HANDLE)ProcessId, &targetProcess);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // Copy memory from target process
    status = MmCopyVirtualMemory(
        targetProcess,          // Source process
        (PVOID)Address,         // Source address
        PsGetCurrentProcess(),  // Target process (our driver)
        Buffer,                 // Target buffer
        Size,                   // Size to copy
        KernelMode,             // Access mode
        &bytesRead              // Bytes copied
    );
    
    ObDereferenceObject(targetProcess);
    return status;
}

// Find process by name
ULONG FindProcessByName(PCWSTR ProcessName) {
    ULONG pid = 0;
    PEPROCESS process = NULL;
    
    for (ULONG i = 4; i < 0x100000; i += 4) {
        if (NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)i, &process))) {
            PUNICODE_STRING imageName = NULL;
            
            if (NT_SUCCESS(SeLocateProcessImageName(process, &imageName))) {
                if (imageName && imageName->Buffer) {
                    // Check if process name contains our target
                    if (wcsstr(imageName->Buffer, ProcessName) != NULL) {
                        pid = i;
                        ExFreePool(imageName);
                        ObDereferenceObject(process);
                        break;
                    }
                    ExFreePool(imageName);
                }
            }
            ObDereferenceObject(process);
        }
    }
    
    return pid;
}

// Get module base address
ULONG_PTR GetModuleBase(ULONG ProcessId, PCWSTR ModuleName) {
    ULONG_PTR baseAddress = 0;
    PEPROCESS process = NULL;
    
    if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)ProcessId, &process))) {
        return 0;
    }
    
    // Get PEB from EPROCESS
    PPEB peb = PsGetProcessPeb(process);
    if (peb) {
        // Read PEB->Ldr
        KAPC_STATE apcState;
        KeStackAttachProcess(process, &apcState);
        
        __try {
            PPEB_LDR_DATA ldr = peb->Ldr;
            if (ldr) {
                PLIST_ENTRY listHead = &ldr->InMemoryOrderModuleList;
                PLIST_ENTRY listEntry = listHead->Flink;
                
                while (listEntry != listHead) {
                    PLDR_DATA_TABLE_ENTRY entry = CONTAINING_RECORD(
                        listEntry, 
                        LDR_DATA_TABLE_ENTRY, 
                        InMemoryOrderLinks
                    );
                    
                    if (entry->BaseDllName.Buffer) {
                        if (wcsstr(entry->BaseDllName.Buffer, ModuleName) != NULL) {
                            baseAddress = (ULONG_PTR)entry->DllBase;
                            break;
                        }
                    }
                    
                    listEntry = listEntry->Flink;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            baseAddress = 0;
        }
        
        KeUnstackDetachProcess(&apcState);
    }
    
    ObDereferenceObject(process);
    return baseAddress;
}

// Device control handler
NTSTATUS DeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    
    NTSTATUS status = STATUS_SUCCESS;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG controlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inputLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
    SIZE_T bytesReturned = 0;
    
    switch (controlCode) {
        case IOCTL_PROTOON_READ_MEMORY: {
            if (inputLength >= sizeof(MEMORY_READ_REQUEST)) {
                PMEMORY_READ_REQUEST request = (PMEMORY_READ_REQUEST)buffer;
                status = ReadProcessMemory(
                    request->ProcessId,
                    request->Address,
                    request->Buffer,
                    request->Size
                );
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }
        
        case IOCTL_PROTOON_GET_MODULE_BASE: {
            if (inputLength >= sizeof(MODULE_BASE_REQUEST)) {
                PMODULE_BASE_REQUEST request = (PMODULE_BASE_REQUEST)buffer;
                request->BaseAddress = GetModuleBase(request->ProcessId, L"RobloxPlayerBeta.exe");
                bytesReturned = sizeof(MODULE_BASE_REQUEST);
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }
        
        case IOCTL_PROTOON_GET_PID: {
            if (outputLength >= sizeof(ULONG)) {
                *(PULONG)buffer = FindProcessByName(L"RobloxPlayerBeta");
                bytesReturned = sizeof(ULONG);
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }
        
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return status;
}

// Create/Close handlers
NTSTATUS CreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// Driver unload
VOID DriverUnload(PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(PROTOON_SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlinkName);
    
    if (g_DeviceObject) {
        IoDeleteDevice(g_DeviceObject);
    }
    
    DbgPrint("[Protoon] Driver unloaded\n");
}

// Driver entry point
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    
    NTSTATUS status = STATUS_SUCCESS;
    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(PROTOON_DEVICE_NAME);
    UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(PROTOON_SYMLINK_NAME);
    
    DbgPrint("[Protoon] Driver loading...\n");
    
    // Create device object
    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject
    );
    
    if (!NT_SUCCESS(status)) {
        DbgPrint("[Protoon] Failed to create device: 0x%X\n", status);
        return status;
    }
    
    // Create symbolic link
    status = IoCreateSymbolicLink(&symlinkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        DbgPrint("[Protoon] Failed to create symlink: 0x%X\n", status);
        return status;
    }
    
    // Set up dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE] = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControl;
    DriverObject->DriverUnload = DriverUnload;
    
    DbgPrint("[Protoon] Driver loaded successfully\n");
    return STATUS_SUCCESS;
}
