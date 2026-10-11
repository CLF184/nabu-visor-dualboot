/* SPDX-License-Identifier: MIT
 * Linux-only handoff trace. Never intercept or change firmware policy. */
#include "nabu_visor.h"
#include "efi_helpers.h"

#define TRACE_PATH L"\\EFI\\visor\\refind\\nabu-handoff.log"
#ifndef NABU_LINUX_TRACE
#define NABU_LINUX_TRACE 0
#endif
#if NABU_LINUX_TRACE
static EFI_HANDLE trace_volume;
#endif
static int linux_handoff;

/* No file or root handle survives a firmware call. Logging is best effort;
 * write failure must not change the original loader's return value. */
void nabu_linux_trace(const char *stage, EFI_STATUS status) {
    #if NABU_LINUX_TRACE
    if (!trace_volume || !visor_boot_services_active) return;
    EFI_FILE_PROTOCOL *root = efi_open_volume_root(trace_volume), *file = NULL;
    if (!root) return;
    EFI_STATUS opened = root->Open(root, &file, TRACE_PATH,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (!EFI_ERROR(opened)) {
        char line[128]; UINTN n = 0;
        while (*stage && n < 100) line[n++] = *stage++;
        line[n++] = ' '; line[n++] = '0'; line[n++] = 'x';
        for (int shift = 60; shift >= 0; shift -= 4)
            line[n++] = "0123456789abcdef"[((UINT64)status >> shift) & 15];
        line[n++] = '\r'; line[n++] = '\n';
        if (!EFI_ERROR(file->SetPosition(file, ~(UINT64)0))) {
            UINTN written = n;
            EFI_STATUS result = file->Write(file, &written, line);
            if (!EFI_ERROR(result) && written == n) file->Flush(file);
        }
        file->Close(file);
    }
    root->Close(root);
    #else
    (void)stage; (void)status;
    #endif
}

void nabu_linux_trace_begin(EFI_HANDLE volume, EFI_SYSTEM_TABLE *st) {
    linux_handoff = 1;
    #if NABU_LINUX_TRACE
    trace_volume = volume;
    EFI_FILE_PROTOCOL *root = efi_open_volume_root(volume), *file = NULL;
    if (root) {
        EFI_STATUS status = root->Open(root, &file, TRACE_PATH,
            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
        if (!EFI_ERROR(status)) {
            /* Delete closes the file, even if deletion fails. Avoid appending
             * ambiguous results to a previous test if the reset failed. */
            if (file->Delete(file) != EFI_SUCCESS) trace_volume = NULL;
        } else if (status != EFI_NOT_FOUND) trace_volume = NULL;
        root->Close(root);
    } else trace_volume = NULL;
    nabu_linux_trace("handoff.begin", EFI_SUCCESS);
    if (trace_volume && st && st->RuntimeServices) {
        EFI_GUID global = EFI_GLOBAL_VARIABLE;
        CHAR16 *names[] = {L"SecureBoot", L"SetupMode"};
        const char *stages[] = {"SecureBoot.read", "SetupMode.read"};
        const char *values[] = {"SecureBoot.value", "SetupMode.value"};
        for (UINTN i = 0; i < 2; i++) {
            UINT8 value = 0; UINTN size = sizeof(value);
            EFI_STATUS status = st->RuntimeServices->GetVariable(names[i],
                &global, NULL, &size, &value);
            nabu_linux_trace(stages[i], status);
            if (!EFI_ERROR(status) && size == sizeof(value))
                nabu_linux_trace(values[i], value);
        }
    }
    #else
    (void)volume; (void)st;
    #endif
}

void nabu_linux_trace_end(void) {
    #if NABU_LINUX_TRACE
    trace_volume = NULL;
    #endif
    linux_handoff = 0;
}

EFI_STATUS nabu_linux_load_image(EFI_BOOT_SERVICES *bs, BOOLEAN policy,
    EFI_HANDLE parent, EFI_DEVICE_PATH *path, VOID *buffer, UINTN size,
    EFI_HANDLE *image) {
    nabu_linux_trace("LoadImage.before", EFI_SUCCESS);
    EFI_STATUS status = bs->LoadImage(policy, parent, path, buffer, size, image);
    nabu_linux_trace("LoadImage.return", status);
    return status;
}

EFI_STATUS nabu_linux_start_image(EFI_BOOT_SERVICES *bs, EFI_HANDLE image,
    UINTN *exit_size, CHAR16 **exit_data) {
    EFI_STATUS status;
    int linux = linux_handoff;
    if (linux) {
        status = nabu_linux_table_begin(bs, image);
        nabu_linux_trace("SystemTable.prepare", status);
        if (EFI_ERROR(status)) return status;
    }
    nabu_linux_trace("StartImage.before", EFI_SUCCESS);
    status = bs->StartImage(image, exit_size, exit_data);
    nabu_linux_trace("StartImage.return", status);
    if (linux) nabu_linux_table_end();
    return status;
}
