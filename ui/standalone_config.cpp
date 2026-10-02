#include <windows.h>
#include <commctrl.h>
#include "property_dialog.h"
#include "../config/encoder_config.h"

HINSTANCE g_hInst = NULL;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInst = hInstance;
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TAB_CLASSES | ICC_UPDOWN_CLASS;
    InitCommonControlsEx(&icex);

    int initialTab = 0;
    if (lpCmdLine) {
        if (wcsstr(lpCmdLine, L"--tab 1")) {
            initialTab = 1;
        } else if (wcsstr(lpCmdLine, L"--tab 2")) {
            initialTab = 2;
        }
    }

    EncoderConfig config;
    config.Load();
    ShowEncoderSettingsDialog(NULL, config, initialTab);
    return 0;
}
