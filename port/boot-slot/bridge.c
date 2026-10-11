/* SPDX-License-Identifier: MIT
 * Narrow EDK2-to-GNU-EFI ABI bridge, separate from upstream slot algorithms.
 */
#include <Uefi.h>
EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;
EFI_SYSTEM_TABLE *gST;
EFI_GUID gEfiBlockIoProtocolGuid = EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gEfiDevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiFileSystemInfoGuid = EFI_FILE_SYSTEM_INFO_ID;
EFI_GUID gEfiPartitionInfoProtocolGuid = {0x8cf2f62c,0xbc9b,0x4821,{0x80,0x8d,0xec,0x9e,0xc4,0x21,0xa1,0xa0}};
EFI_GUID gEfiMemCardInfoProtocolGuid = {0x85c1f7d2,0xbce6,0x4f31,{0x8f,0x4d,0xd3,0x7e,0x03,0xd0,0x5e,0xaa}};
#include "qcom_guids.h"

EFI_STATUS NabuBootSlotInitialize(EFI_SYSTEM_TABLE *table) {
    if (!table || !table->BootServices) return EFI_INVALID_PARAMETER;
    gST = table; gBS = table->BootServices; gRT = table->RuntimeServices;
    return EFI_SUCCESS;
}
VOID *AllocatePool(UINTN size) {
    VOID *buffer = NULL;
    if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, size, &buffer))) return NULL;
    return buffer;
}
VOID *SetMem(VOID *dest, UINTN size, UINT8 value) {
    UINT8 *p = dest;
    for (UINTN i = 0; i < size; i++) p[i] = value;
    return dest;
}
VOID *AllocateZeroPool(UINTN size) {
    VOID *buffer = AllocatePool(size);
    if (buffer) SetMem(buffer, size, 0);
    return buffer;
}
VOID FreePool(VOID *buffer) { if (buffer) gBS->FreePool(buffer); }
VOID *CopyMem(VOID *dest, CONST VOID *source, UINTN size) {
    UINT8 *d = dest; CONST UINT8 *s = source;
    if ((UINTN)d < (UINTN)s) for (UINTN i = 0; i < size; i++) d[i] = s[i];
    else for (UINTN i = size; i > 0; i--) d[i-1] = s[i-1];
    return dest;
}
INTN CompareMem(CONST VOID *left, CONST VOID *right, UINTN size) {
    CONST UINT8 *a = left, *b = right;
    for (UINTN i = 0; i < size; i++) if (a[i] != b[i]) return (INTN)a[i] - b[i];
    return 0;
}
BOOLEAN CompareGuid(CONST EFI_GUID *left, CONST EFI_GUID *right) {
    return CompareMem(left, right, sizeof(*left)) == 0;
}
UINTN StrLen(CONST CHAR16 *text) { UINTN n = 0; while (text[n]) n++; return n; }
INTN StrnCmp(CONST CHAR16 *left, CONST CHAR16 *right, UINTN count) {
    for (UINTN i = 0; i < count; i++) {
        if (left[i] != right[i]) return (INTN)left[i] - right[i];
        if (!left[i]) break;
    }
    return 0;
}
CHAR16 *StrStr(CONST CHAR16 *text, CONST CHAR16 *needle) {
    UINTN n = StrLen(needle);
    if (!n) return (CHAR16 *)text;
    for (; *text; text++) if (!StrnCmp(text, needle, n)) return (CHAR16 *)text;
    return NULL;
}
RETURN_STATUS StrnCpyS(CHAR16 *dest, UINTN capacity, CONST CHAR16 *source, UINTN count) {
    if (!dest || !source || !capacity) return EFI_INVALID_PARAMETER;
    UINTN n = 0;
    while (n < count && source[n]) n++;
    if (n >= capacity) { dest[0] = 0; return EFI_BUFFER_TOO_SMALL; }
    for (UINTN i = 0; i < n; i++) dest[i] = source[i];
    dest[n] = 0; return EFI_SUCCESS;
}
RETURN_STATUS StrnCatS(CHAR16 *dest, UINTN capacity, CONST CHAR16 *source, UINTN count) {
    if (!dest || !source || !capacity) return EFI_INVALID_PARAMETER;
    UINTN n = 0; while (n < capacity && dest[n]) n++;
    if (n == capacity) return EFI_INVALID_PARAMETER;
    return StrnCpyS(dest+n, capacity-n, source, count);
}
UINTN AsciiStrLen(CONST CHAR8 *text) { UINTN n = 0; while (text[n]) n++; return n; }
INTN AsciiStrnCmp(CONST CHAR8 *left, CONST CHAR8 *right, UINTN count) {
    for (UINTN i = 0; i < count; i++) {
        if ((UINT8)left[i] != (UINT8)right[i]) return (INTN)(UINT8)left[i] - (UINT8)right[i];
        if (!left[i]) break;
    }
    return 0;
}
UINTN AsciiSPrint(CHAR8 *dest, UINTN capacity, CONST CHAR8 *format, ...) {
    /* The pinned library uses PrintLib once, with exactly "%a". */
    if (!dest || !capacity || !format || format[0]!='%' || format[1]!='a' || format[2]) return 0;
    va_list args; va_start(args, format); CONST CHAR8 *source = va_arg(args, CONST CHAR8 *); va_end(args);
    UINTN i = 0; while (i+1 < capacity && source[i]) { dest[i] = source[i]; i++; }
    dest[i] = 0; return i;
}
UINTN Print(CONST CHAR16 *text, ...) {
    /* EFIUtils supplies a literal Message with no format arguments. */
    if (!gST || !gST->ConOut || EFI_ERROR(gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)text))) return 0;
    return StrLen(text);
}
