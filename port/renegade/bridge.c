// Narrow EDK2-to-GNU-EFI adapter for the upstream Reboot2PayloadApp.
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>

EFI_RUNTIME_SERVICES *gRT;
EFI_STATUS EFIAPI Reboot2PayloadAppEntryPoint(EFI_HANDLE, EFI_SYSTEM_TABLE *);

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
    gRT = table->RuntimeServices;
    return Reboot2PayloadAppEntryPoint(handle, table);
}
