/*
GazOS Operating System
Copyright (C) 2026  Gareth Owenson <gareth.owenson@gmail.com>
Minimal UEFI definitions for the GazOS loader (x86_64, Microsoft ABI).

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

Only what the loader uses is typed; other table slots are kept as
placeholders so the offsets match the UEFI specification. Built with
x86_64-w64-mingw32-gcc, whose native calling convention is the
Microsoft x64 ABI that UEFI uses, and whose wchar_t (L"...") is 16-bit.
*/

#ifndef GAZOS_EFI_H
#define GAZOS_EFI_H

typedef unsigned char		UINT8;
typedef unsigned short		UINT16;
typedef unsigned int		UINT32;
typedef unsigned long long	UINT64;
typedef UINT64			UINTN;
typedef UINT16			CHAR16;
typedef UINTN			EFI_STATUS;
typedef void			*EFI_HANDLE;
typedef UINT64			EFI_PHYSICAL_ADDRESS;

#define EFI_SUCCESS		0
#define EFI_ERROR(s)		((long long)(s) < 0)
#define EFI_BUFFER_TOO_SMALL	(0x8000000000000000ULL | 5)

typedef struct { UINT32 d1; UINT16 d2, d3; UINT8 d4[8]; } EFI_GUID;

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
	{ 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
	{ 0x964E5B22, 0x6459, 0x11d2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
	{ 0x9042A9DE, 0x23DC, 0x4A38, { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } }

typedef struct
{
	UINT64 Signature;
	UINT32 Revision;
	UINT32 HeaderSize;
	UINT32 CRC32;
	UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ---- console output ---- */

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
	void *Reset;
	EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
	void *TestString, *QueryMode, *SetMode, *SetAttribute, *ClearScreen;
	void *SetCursorPosition, *EnableCursor, *Mode;
};

/* ---- memory ---- */

typedef enum
{
	AllocateAnyPages, AllocateMaxAddress, AllocateAddress
} EFI_ALLOCATE_TYPE;

typedef enum
{
	EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData,
	EfiBootServicesCode, EfiBootServicesData,
	EfiRuntimeServicesCode, EfiRuntimeServicesData,
	EfiConventionalMemory, EfiUnusableMemory,
	EfiACPIReclaimMemory, EfiACPIMemoryNVS,
	EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace, EfiPalCode,
	EfiPersistentMemory
} EFI_MEMORY_TYPE;

typedef struct
{
	UINT32 Type;
	UINT32 Pad;
	EFI_PHYSICAL_ADDRESS PhysicalStart;
	UINT64 VirtualStart;
	UINT64 NumberOfPages;
	UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;	/* use the firmware's DescriptorSize as stride */

/* ---- boot services ---- */

typedef struct
{
	EFI_TABLE_HEADER Hdr;
	void *RaiseTPL, *RestoreTPL;
	EFI_STATUS (*AllocatePages)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType,
				    UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
	EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
	EFI_STATUS (*GetMemoryMap)(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap,
				   UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
	EFI_STATUS (*AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, void **Buffer);
	EFI_STATUS (*FreePool)(void *Buffer);
	void *CreateEvent, *SetTimer, *WaitForEvent, *SignalEvent, *CloseEvent, *CheckEvent;
	void *InstallProtocolInterface, *ReinstallProtocolInterface, *UninstallProtocolInterface;
	EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface);
	void *Reserved, *RegisterProtocolNotify, *LocateHandle, *LocateDevicePath;
	void *InstallConfigurationTable;
	void *LoadImage, *StartImage, *Exit, *UnloadImage;
	EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
	void *GetNextMonotonicCount, *Stall;
	EFI_STATUS (*SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode,
				       UINTN DataSize, CHAR16 *WatchdogData);
	void *ConnectController, *DisconnectController;
	void *OpenProtocol, *CloseProtocol, *OpenProtocolInformation;
	void *ProtocolsPerHandle, *LocateHandleBuffer;
	EFI_STATUS (*LocateProtocol)(EFI_GUID *Protocol, void *Registration, void **Interface);
	void *InstallMultipleProtocolInterfaces, *UninstallMultipleProtocolInterfaces;
	void *CalculateCrc32, *CopyMem, *SetMem, *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct
{
	EFI_TABLE_HEADER Hdr;
	CHAR16 *FirmwareVendor;
	UINT32 FirmwareRevision;
	EFI_HANDLE ConsoleInHandle;
	void *ConIn;
	EFI_HANDLE ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
	EFI_HANDLE StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
	void *RuntimeServices;
	EFI_BOOT_SERVICES *BootServices;
	UINTN NumberOfTableEntries;
	void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* ---- loaded image ---- */

typedef struct
{
	UINT32 Revision;
	EFI_HANDLE ParentHandle;
	EFI_SYSTEM_TABLE *SystemTable;
	EFI_HANDLE DeviceHandle;	/* the volume we were loaded from */
	void *FilePath;
	void *Reserved;
	UINT32 LoadOptionsSize;
	void *LoadOptions;
	void *ImageBase;
	UINT64 ImageSize;
	EFI_MEMORY_TYPE ImageCodeType, ImageDataType;
	void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ---- files ---- */

#define EFI_FILE_MODE_READ	0x0000000000000001ULL

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL
{
	UINT64 Revision;
	EFI_STATUS (*Open)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle,
			   CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
	EFI_STATUS (*Close)(EFI_FILE_PROTOCOL *This);
	void *Delete;
	EFI_STATUS (*Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
	void *Write;
	EFI_STATUS (*GetPosition)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
	EFI_STATUS (*SetPosition)(EFI_FILE_PROTOCOL *This, UINT64 Position);
	void *GetInfo, *SetInfo, *Flush;
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
	UINT64 Revision;
	EFI_STATUS (*OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
};

/* ---- graphics ---- */

typedef enum
{
	PixelRedGreenBlueReserved8BitPerColor,
	PixelBlueGreenRedReserved8BitPerColor,
	PixelBitMask,
	PixelBltOnly
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct
{
	UINT32 Version;
	UINT32 HorizontalResolution;
	UINT32 VerticalResolution;
	EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
	UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
	UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct
{
	UINT32 MaxMode;
	UINT32 Mode;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
	UINTN SizeOfInfo;
	EFI_PHYSICAL_ADDRESS FrameBufferBase;
	UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL
{
	void *QueryMode, *SetMode, *Blt;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

#endif
