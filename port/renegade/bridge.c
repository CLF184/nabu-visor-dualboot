// Narrow EDK2-to-GNU-EFI adapter for the upstream Reboot2PayloadApp.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>

EFI_RUNTIME_SERVICES *gRT;
static EFI_RUNTIME_SERVICES android_runtime;
EFI_STATUS EFIAPI Reboot2PayloadAppEntryPoint(EFI_HANDLE, EFI_SYSTEM_TABLE *);

/* Only the marker application's private runtime copy uses PSCI. Never replace
 * the firmware runtime table or redirect shutdown/platform-specific resets. */
static EFI_STATUS EFIAPI android_reset(EFI_RESET_TYPE type, EFI_STATUS status,
    UINTN size, CHAR16 *data)
{
    (void)type; (void)status; (void)size; (void)data;
    register UINTN x0 __asm__("x0") = 0x84000009;
    register UINTN x1 __asm__("x1") = 0;
    register UINTN x2 __asm__("x2") = 0;
    register UINTN x3 __asm__("x3") = 0;
    __asm__ volatile ("dsb sy\n\t.inst 0xd4000003"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        : : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
            "x12", "x13", "x14", "x15", "x16", "x17", "memory", "cc");
    /* If firmware rejects the call, let upstream CpuDeadLoop handle it. */
    return EFI_DEVICE_ERROR;
}

VOID *EFIAPI WriteBackInvalidateDataCacheRange(VOID *address, UINTN length)
{
    UINTN ctr, line, begin, end;
    __asm__ volatile ("mrs %0, ctr_el0" : "=r"(ctr));
    line = 4UL << ((ctr >> 16) & 15);
    begin = (UINTN)address & ~(line - 1);
    end = (UINTN)address + length;
    __asm__ volatile ("dsb sy" ::: "memory");
    for (; begin < end; begin += line)
        __asm__ volatile ("dc civac, %0" :: "r"(begin) : "memory");
    __asm__ volatile ("dsb sy\n\tisb" ::: "memory");
    return address;
}

VOID EFIAPI CpuDeadLoop(VOID)
{
    for (;;) __asm__ volatile ("wfe" ::: "memory");
}

EFI_STATUS nabu_renegade_android(EFI_HANDLE handle, EFI_SYSTEM_TABLE *table)
{
    if (!table || !table->RuntimeServices) return EFI_INVALID_PARAMETER;
    android_runtime = *table->RuntimeServices;
    android_runtime.ResetSystem = android_reset;
    gRT = &android_runtime;
    return Reboot2PayloadAppEntryPoint(handle, table);
}
