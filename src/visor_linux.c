/* SPDX-License-Identifier: MIT
 * External rEFInd handoff. Its binary, drivers and config parser are unchanged. */
#include "nabu_visor.h"
#include "efi_helpers.h"
#include "linux_boot.h"

#define REFIND_IMAGE L"\\EFI\\visor\\refind\\bootaa64.efi"
#define REFIND_CONFIG L"\\EFI\\visor\\refind\\nabu-selected.conf"
#define CONFIG_CAP 4096
#define BASE_CONFIG L"\\EFI\\visor\\refind\\refind.conf"
#define BASE_LIMIT (1024 * 1024)

typedef struct { CHAR16 *text; UINTN used, capacity; } handoff_config;

static int append(handoff_config *c, const CHAR16 *s) {
    if (!s) return 1;
    while (*s) {
        if (c->used + 1 >= c->capacity) return 0;
        c->text[c->used++] = *s++;
    }
    c->text[c->used] = 0;
    return 1;
}

/* rEFInd escapes embedded quotes by doubling them, including in options. */
static int value(handoff_config *c, const CHAR16 *key, const CHAR16 *s) {
    if (!s || !*s) return 1;
    if (!append(c, key) || !append(c, L" \"")) return 0;
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n' || *s < 32) return 0;
        CHAR16 ch[2] = {*s, 0};
        if (!append(c, ch) || (*s == '"' && !append(c, ch))) return 0;
    }
    return append(c, L"\"\n");
}

/* rEFInd allows includes only from the selected primary config. Copy the
 * base text into that config, so its original theme includes stay at depth 1.
 * Decode the same encodings rEFInd accepts; its unmarked text is Latin-1. */
static int append_base(handoff_config *c, const UINT8 *p, UINTN n) {
    UINTN off = 0;
    int utf16 = 0, utf8 = 0;
    if (n >= 2 && p[0] == 0xff && p[1] == 0xfe) { utf16 = 1; off = 2; }
    else if (n >= 3 && p[0] == 0xef && p[1] == 0xbb && p[2] == 0xbf) { utf8 = 1; off = 3; }
    else if (n >= 4 && p[1] == 0 && p[3] == 0) utf16 = 1;
    if (utf16 && ((n - off) & 1)) return 0;
    while (off < n) {
        UINT32 ch = p[off++];
        if (utf16) ch |= (UINT32)p[off++] << 8;
        else if (utf8 && ch >= 0x80) {
            UINTN extra;
            UINT32 minimum;
            if (ch >= 0xc2 && ch <= 0xdf) { extra = 1; minimum = 0x80; ch &= 0x1f; }
            else if (ch >= 0xe0 && ch <= 0xef) { extra = 2; minimum = 0x800; ch &= 0xf; }
            else if (ch >= 0xf0 && ch <= 0xf4) { extra = 3; minimum = 0x10000; ch &= 7; }
            else return 0;
            if (extra > n - off) return 0;
            while (extra--) {
                UINT8 next = p[off++];
                if ((next & 0xc0) != 0x80) return 0;
                ch = (ch << 6) | (next & 0x3f);
            }
            if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff)) return 0;
        }
        if (!ch) return 0;
        CHAR16 out[3] = { (CHAR16)ch, 0, 0 };
        if (ch > 0xffff) {
            ch -= 0x10000;
            out[0] = (CHAR16)(0xd800 + (ch >> 10));
            out[1] = (CHAR16)(0xdc00 + (ch & 0x3ff));
        }
        if (!append(c, out)) return 0;
    }
    return append(c, L"\n");
}

/* Never silently truncate: a failed write must not launch stale configuration. */
static EFI_STATUS write_config(EFI_FILE_PROTOCOL *root, handoff_config *c) {
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = root->Open(root, &file, REFIND_CONFIG,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (!EFI_ERROR(status)) {
        status = file->Delete(file); /* closes the handle even on failure */
        if (status != EFI_SUCCESS) return EFI_DEVICE_ERROR;
    } else if (status != EFI_NOT_FOUND) return status;
    status = root->Open(root, &file, REFIND_CONFIG,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(status)) return status;
    UINTN size = c->used * sizeof(CHAR16), written = size;
    status = file->Write(file, &written, c->text);
    if (!EFI_ERROR(status) && written != size) status = EFI_DEVICE_ERROR;
    if (!EFI_ERROR(status)) status = file->Flush(file);
    if (!EFI_ERROR(status)) {
        status = file->SetPosition(file, 0);
        CHAR16 *check = efi_allocate_pool(size);
        UINTN read = size;
        if (!check) status = EFI_OUT_OF_RESOURCES;
        if (!EFI_ERROR(status)) status = file->Read(file, &read, check);
        if (!EFI_ERROR(status) && (read != size || !mem_equal(check, c->text, size)))
            status = EFI_DEVICE_ERROR;
        if (check) efi_free_pool(check);
    }
    EFI_STATUS closed = file->Close(file);
    return EFI_ERROR(status) ? status : closed;
}

/* Match EfiEventGroupSignal used by the original BDS BeforeConsole.
 * This only advances the selected Linux path; authentication is unchanged. */
static void EFIAPI end_dxe_notify(EFI_EVENT event, void *context) {
    (void)event; (void)context;
}

static EFI_STATUS finish_dxe(EFI_SYSTEM_TABLE *st) {
    if (!st || !st->BootServices) return EFI_INVALID_PARAMETER;
    EFI_GUID group = {0x02ce967a, 0xdd7e, 0x4ffc,
        {0x9e, 0xe7, 0x81, 0x0c, 0xf0, 0x47, 0x08, 0x80}};
    EFI_EVENT event = NULL;
    EFI_BOOT_SERVICES *bs = st->BootServices;
    EFI_STATUS status = bs->CreateEventEx(EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
        end_dxe_notify, NULL, &group, &event);
    if (EFI_ERROR(status)) return status;
    status = bs->SignalEvent(event);
    EFI_STATUS closed = bs->CloseEvent(event);
    return EFI_ERROR(status) ? status : closed;
}

EFI_STATUS nabu_boot_linux(boot_entry_t *entry, EFI_SYSTEM_TABLE *st) {
    /* These Visor-only container/security features cannot be translated to a
     * path that an unmodified rEFInd rereads. Keep the ordinary Visor route
     * available, and fail closed rather than discard those constraints. */
    if (entry->encrypted || entry->initrd_encrypted || entry->luks || entry->has_sha256) {
        efi_print(L"rEFInd action requires plain EFI files without Visor hash/encryption settings\r\n");
        return EFI_UNSUPPORTED;
    }
    if (!entry->kernel_path || !*entry->kernel_path ||
        text_eq_ci(entry->kernel_path, REFIND_IMAGE)) return EFI_INVALID_PARAMETER;

    efi_file_t *refind = efi_fopen(REFIND_IMAGE);
    if (!refind) {
        efi_print(L"Missing \\EFI\\visor\\refind\\bootaa64.efi\r\n");
        return EFI_NOT_FOUND;
    }
    EFI_HANDLE refind_volume = refind->volume;
    EFI_FILE_PROTOCOL *target_root = NULL;
    EFI_HANDLE target_volume = NULL;
    efi_file_t *target = NULL;
    if (entry->hp_volume) {
        target_volume = entry->hp_volume;
        target_root = efi_open_volume_root(target_volume);
    } else {
        target = efi_fopen_uuid(entry->kernel_path, entry->uuid);
        if (target) { target_root = target->root; target_volume = target->volume; }
    }
    EFI_STATUS status = EFI_NOT_FOUND;
    CHAR16 *volume_id = NULL;
    CHAR16 *config_text = NULL;
    efi_file_buffer_t *base = NULL;
    if (!target_root || !efi_file_exists_root(target_root, entry->kernel_path) ||
        (entry->initrd_path && !efi_file_exists_root(target_root, entry->initrd_path)))
        goto done;
    if (target_volume != refind_volume) {
        volume_id = efi_handle_partition_uuid(target_volume);
        if (!volume_id) { status = EFI_UNSUPPORTED; goto done; }
    }
    base = efi_load_file_on_handle(refind_volume, BASE_CONFIG);
    if (!base || !base->data) { status = EFI_NOT_FOUND; goto done; }
    if (base->size > BASE_LIMIT) { status = EFI_BAD_BUFFER_SIZE; goto done; }
    UINTN capacity = base->size + CONFIG_CAP;
    config_text = efi_allocate_pool(capacity * sizeof(CHAR16));
    if (!config_text) { status = EFI_OUT_OF_RESOURCES; goto done; }
    handoff_config c = {config_text, 0, capacity};
    c.text[c.used++] = 0xfeff;
    if (!append(&c, L"# Generated from refind.conf and the selected Visor Linux entry\n") ||
        !append_base(&c, base->data, base->size) ||
        !append(&c, L"log_level 0\ntimeout -1\nscanfor manual\n"
        L"default_selection \"Nabu Linux\"\nmenuentry \"Nabu Linux\" {\n") ||
        !value(&c, L"volume", volume_id) ||
        !value(&c, L"loader", entry->kernel_path) ||
        !value(&c, L"initrd", entry->initrd_path) ||
        !value(&c, L"options", entry->cmdline) || !append(&c, L"ostype Linux\n}\n")) {
        status = EFI_INVALID_PARAMETER;
        goto done;
    }
    status = write_config(refind->root, &c);
done:
    if (base) { efi_free_pool(base->data); efi_free_pool(base); }
    if (config_text) efi_free_pool(config_text);
    if (volume_id) efi_free_pool(volume_id);
    if (target) efi_fclose(target);
    else if (target_root) target_root->Close(target_root);
    efi_fclose(refind);
    if (EFI_ERROR(status)) {
        efi_print(L"Linux handoff: missing target or unable to write/verify rEFInd configuration\r\n");
        return status;
    }
    boot_entry_t bridge;
    ZeroMem(&bridge, sizeof(bridge));
    bridge.name = entry->name;
    bridge.kernel_path = REFIND_IMAGE;
    bridge.cmdline = L"refind -c nabu-selected.conf";
    bridge.hp_volume = refind_volume;
    /* Preserve Visor's LoadImage/StartImage and firmware signature checks. */
    nabu_linux_trace_begin(refind_volume, st);
    nabu_linux_trace("config.verified", EFI_SUCCESS);
    nabu_linux_trace("EndOfDxe.before", EFI_SUCCESS);
    status = finish_dxe(st);
    nabu_linux_trace("EndOfDxe.return", status);
    if (EFI_ERROR(status)) {
        nabu_linux_trace_end();
        efi_print(L"Linux handoff: unable to finish DXE stage\r\n");
        return status;
    }
    status = visor_boot(&bridge, st);
    nabu_linux_trace("visor_boot.return", status);
    nabu_linux_trace_end();
    return status;
}
