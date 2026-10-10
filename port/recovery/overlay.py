"""Keep the verified menu tree intact; overlay only the third-button adapter."""
from pathlib import Path

def prepare(root, work, port, visor=None):
    paths = ['src/visor_platform.c', 'third_party/visor/src/core/main.c',
             'third_party/visor/src/gui/gui_panels.c']
    result = {}
    for rel in paths:
        source = visor/rel.removeprefix('third_party/visor/') if visor is not None and rel.startswith('third_party/visor/') else root/rel
        text = source.read_text(encoding='utf-8')
        if rel.endswith('visor_platform.c'):
            start = text.index('/* Qualcomm ShutdownServices.h')
            end = text.index('\nEFI_STATUS nabu_gui_ready', start)
            text = text[:start] + '''/* Recovery uses the standard bootloader request; other buttons keep their reset. */
extern EFI_STATUS nabu_recovery_boot(void);
EFI_STATUS nabu_power_action(int action) {
    if(action==VISOR_ACTION_FIRMWARE) return nabu_recovery_boot();
    if(!RT || !RT->ResetSystem)return EFI_UNSUPPORTED;
    RT->ResetSystem(action==VISOR_ACTION_SHUTDOWN?EfiResetShutdown:EfiResetCold,EFI_SUCCESS,0,NULL);
    return EFI_DEVICE_ERROR;
}
''' + text[end:]
        elif rel.endswith('main.c'):
            old = 'gui.title=reset_status==EFI_UNSUPPORTED?L"Fastboot unavailable":L"Fastboot reset failed";'
            assert text.count(old) == 1, 'Unexpected Visor firmware-action branch'
            text = text.replace(old, 'extern CHAR16 *nabu_recovery_error(void);\n        (void)reset_status;gui.title=nabu_recovery_error();gui.show_title=1;')
            old = 'nabu_power_action(action);\n        force_menu=1;goto select_entry;'
            assert text.count(old) == 1, 'Unexpected console firmware-action branch'
            text = text.replace(old, 'nabu_power_action(action);\n        extern CHAR16 *nabu_recovery_error(void);\n        gui.title=nabu_recovery_error();gui.show_title=1;gui.timeout_active=0;\n        force_menu=1;goto select_entry;')
        else:
            assert text.count('L"Fastboot"') == 1, 'Unexpected third-button label'
            text = text.replace('L"Fastboot"', 'L"Recovery"')
        dest = work/'recovery-overlay'/rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding='utf-8')
        result[rel] = dest
    return result, Path(port)/'recovery.c'
