/* SPDX-License-Identifier: MIT
 * EDK2 ABI/library bridge for the fixed upstream BootSlotDxe sources.
 * The driver's slot/GPT algorithms are compiled unchanged from the submodule.
 */
#ifndef NABU_BOOT_SLOT_UEFI_H
#define NABU_BOOT_SLOT_UEFI_H
/* EDK2 device paths use packed wire structures. GNU-EFI does not pack them. */
#if defined(__aarch64__) || defined(_M_ARM64)
#include <aarch64/efibind.h>
#elif defined(__x86_64__) || defined(_M_X64)
#include <x86_64/efibind.h>
#else
#error Unsupported BootSlot bridge architecture
#endif
#include <eficompiler.h>
#include <efidef.h>
#pragma pack(push,1)
#include <efidevp.h>
#pragma pack(pop)
#include <efi.h>
#include <efigpt.h>
#include <stddef.h>
#include <stdarg.h>
#ifndef CONST
#define CONST const
#endif
#ifndef STATIC
#define STATIC static
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_UINT64 ((UINT64)-1)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define DEBUG(args) do { } while (0)
#define ASSERT(condition) do { if (!(condition)) __builtin_trap(); } while (0)
typedef EFI_STATUS RETURN_STATUS;
_Static_assert(sizeof(VENDOR_DEVICE_PATH) == 20, "Vendor device path ABI");
_Static_assert(sizeof(HARDDRIVE_DEVICE_PATH) == 42, "Hard drive device path ABI");
_Static_assert(_Alignof(HARDDRIVE_DEVICE_PATH) == 1, "Packed device path ABI");
extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
extern EFI_SYSTEM_TABLE *gST;
extern EFI_GUID gEfiBlockIoProtocolGuid, gEfiDevicePathProtocolGuid;
extern EFI_GUID gEfiPartitionInfoProtocolGuid, gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiFileSystemInfoGuid, gEfiMemCardInfoProtocolGuid;
extern EFI_GUID gEfiEmmcUserPartitionGuid;
extern EFI_GUID gEfiUfsLU0Guid, gEfiUfsLU1Guid, gEfiUfsLU2Guid, gEfiUfsLU3Guid;
extern EFI_GUID gEfiUfsLU4Guid, gEfiUfsLU5Guid, gEfiUfsLU6Guid, gEfiUfsLU7Guid;
#pragma pack(push,1)
typedef struct {
    UINT32 Revision, Type;
    UINT8 System, Reserved[7];
    union { UINT8 Mbr[16]; EFI_PARTITION_ENTRY Gpt; } Info;
} EFI_PARTITION_INFO_PROTOCOL;
#pragma pack(pop)
_Static_assert(sizeof(EFI_PARTITION_INFO_PROTOCOL) == 144, "PartitionInfo ABI");
_Static_assert(offsetof(EFI_PARTITION_INFO_PROTOCOL, Info.Gpt.PartitionName) == 72, "GPT name ABI");
VOID *AllocatePool(UINTN size);
VOID *AllocateZeroPool(UINTN size);
VOID FreePool(VOID *buffer);
VOID *CopyMem(VOID *dest, CONST VOID *source, UINTN size);
VOID *SetMem(VOID *dest, UINTN size, UINT8 value);
INTN CompareMem(CONST VOID *left, CONST VOID *right, UINTN size);
BOOLEAN CompareGuid(CONST EFI_GUID *left, CONST EFI_GUID *right);
UINTN StrLen(CONST CHAR16 *text);
INTN StrnCmp(CONST CHAR16 *left, CONST CHAR16 *right, UINTN count);
CHAR16 *StrStr(CONST CHAR16 *text, CONST CHAR16 *needle);
RETURN_STATUS StrnCpyS(CHAR16 *dest, UINTN capacity, CONST CHAR16 *source, UINTN count);
RETURN_STATUS StrnCatS(CHAR16 *dest, UINTN capacity, CONST CHAR16 *source, UINTN count);
UINTN AsciiStrLen(CONST CHAR8 *text);
INTN AsciiStrnCmp(CONST CHAR8 *left, CONST CHAR8 *right, UINTN count);
UINTN AsciiSPrint(CHAR8 *dest, UINTN capacity, CONST CHAR8 *format, ...);
UINTN Print(CONST CHAR16 *text, ...);
#endif
