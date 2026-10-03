#include "property_dialog.h"
#include "resource.h"
#include "../encoder/encoder_controller.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <vector>
#include <string>

extern HINSTANCE g_hInst;

class CThemeActivationContext {
public:
    CThemeActivationContext() : m_hActCtx(INVALID_HANDLE_VALUE), m_cookie(0) {
        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hInst, modulePath, MAX_PATH) > 0) {
            ACTCTXW actCtx = { sizeof(ACTCTXW) };
            actCtx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
            actCtx.lpResourceName = MAKEINTRESOURCEW(2);
            actCtx.hModule = g_hInst;
            actCtx.lpSource = modulePath;
            m_hActCtx = CreateActCtxW(&actCtx);
            if (m_hActCtx != INVALID_HANDLE_VALUE) {
                ActivateActCtx(m_hActCtx, &m_cookie);
            }
        }
    }
    ~CThemeActivationContext() {
        if (m_hActCtx != INVALID_HANDLE_VALUE) {
            DeactivateActCtx(0, m_cookie);
            ReleaseActCtx(m_hActCtx);
        }
    }
private:
    HANDLE m_hActCtx;
    ULONG_PTR m_cookie;
};

namespace {
    const UINT kLanguageChangedMessage = WM_APP + 1;

    struct DialogState {
        EncoderConfig* config = nullptr;
        int activeTab = 0;
        bool initializing = true;
    };

    enum BackendItemData {
        BACKEND_AUTO = 0,
        BACKEND_NVIDIA = 1,
        BACKEND_INTEL = 2,
        BACKEND_AMD = 3,
        BACKEND_CPU = 4
    };

    const int kBasicControls[] = {
        IDC_STATIC_PRESET, IDC_COMBO_PRESET,
        IDC_STATIC_FORMAT, IDC_COMBO_FORMAT,
        IDC_CHECK_ALPHA, IDC_COMBO_ALPHA_FORMAT,
        IDC_STATIC_BACKEND, IDC_COMBO_BACKEND,
        IDC_STATIC_QUALITY, IDC_EDIT_CRF, IDC_SPIN_CRF,
        IDC_CHECK_AUDIO
    };

    const PresetType kPresetOrder[] = {
        PresetType::HighQualityH264,
        PresetType::HighQualityHEVC,
        PresetType::HighQualityAV1,
        PresetType::YouTube,
        PresetType::Editing,
        PresetType::Transparent,
        PresetType::PNGSequence,
        PresetType::Custom
    };
    const int kPresetCount = static_cast<int>(sizeof(kPresetOrder) / sizeof(kPresetOrder[0]));

    int PresetToIndex(PresetType p) {
        for (int i = 0; i < kPresetCount; ++i) {
            if (kPresetOrder[i] == p) return i;
        }
        return kPresetCount - 1;
    }

    const wchar_t* const kVideoFormatKeys[] = { L"h264", L"hevc", L"av1", L"prores422", L"prores422hq", L"vp9", L"av1webm", L"jpg", L"png", L"exr" };
    const size_t kVideoFormatCount = sizeof(kVideoFormatKeys) / sizeof(kVideoFormatKeys[0]);
    const wchar_t* const kAlphaFormatKeys[] = { L"prores4444", L"prores4444xq", L"vp9", L"png", L"exr" };
    const size_t kAlphaFormatCount = sizeof(kAlphaFormatKeys) / sizeof(kAlphaFormatKeys[0]);

    int KeyToIndex(const wchar_t* const* keys, size_t count, const std::wstring& key) {
        for (size_t i = 0; i < count; ++i) {
            if (key == keys[i]) return static_cast<int>(i);
        }
        return 0;
    }

    std::wstring IndexToKey(const wchar_t* const* keys, size_t count, int index) {
        if (index < 0 || static_cast<size_t>(index) >= count) return keys[0];
        return keys[index];
    }

    PresetType IndexToPreset(int index) {
        if (index < 0 || index >= kPresetCount) return PresetType::Custom;
        return kPresetOrder[index];
    }

    const int kAdvancedControls[] = {
        IDC_STATIC_BIT_DEPTH, IDC_COMBO_BIT_DEPTH,
        IDC_STATIC_COLORSPACE, IDC_COMBO_COLORSPACE,
        IDC_STATIC_CHROMA, IDC_COMBO_CHROMA,
        IDC_STATIC_COLOR_RANGE, IDC_COMBO_COLOR_RANGE,
        IDC_CHECK_GOP_AUTO, IDC_STATIC_GOP, IDC_EDIT_GOP,
        IDC_STATIC_BFRAMES, IDC_EDIT_BFRAMES,
        IDC_STATIC_LOOKAHEAD, IDC_EDIT_LOOKAHEAD
    };

    const int kOtherControls[] = {
        IDC_STATIC_LANGUAGE, IDC_COMBO_LANGUAGE
    };

    void SetVisibility(HWND hwnd, const int* controls, size_t count, int showCmd) {
        for (size_t i = 0; i < count; ++i) {
            HWND hCtrl = GetDlgItem(hwnd, controls[i]);
            if (hCtrl) {
                ShowWindow(hCtrl, showCmd);
            }
        }
    }

    void UpdateTabVisibility(HWND hwnd, int activeTab) {
        SetVisibility(hwnd, kBasicControls, sizeof(kBasicControls) / sizeof(kBasicControls[0]), activeTab == 0 ? SW_SHOW : SW_HIDE);
        SetVisibility(hwnd, kAdvancedControls, sizeof(kAdvancedControls) / sizeof(kAdvancedControls[0]), activeTab == 1 ? SW_SHOW : SW_HIDE);
        SetVisibility(hwnd, kOtherControls, sizeof(kOtherControls) / sizeof(kOtherControls[0]), activeTab == 2 ? SW_SHOW : SW_HIDE);
    }

    enum class UILanguage {
        Japanese = 1,
        English = 2
    };

    UILanguage ResolveLanguage(int ui_language) {
        EncoderConfig probe;
        probe.ui_language = ui_language;
        return probe.ResolvedLanguage() == 2 ? UILanguage::English : UILanguage::Japanese;
    }

    void UpdateDialogLanguage(HWND hwnd, UILanguage lang, const EncoderCapabilities& caps) {
        auto tr = [lang](const wchar_t* ja, const wchar_t* en) -> const wchar_t* {
            if (lang == UILanguage::English) return en;
            return ja;
        };

        SetWindowTextW(hwnd, tr(L"MMDirectEncoder 設定", L"MMDirectEncoder Settings"));

        HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
        TCITEMW tie;
        ZeroMemory(&tie, sizeof(tie));
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(tr(L"基本設定", L"Basic Settings"));
        TabCtrl_SetItem(hTab, 0, &tie);
        tie.pszText = const_cast<LPWSTR>(tr(L"詳細設定", L"Advanced Settings"));
        TabCtrl_SetItem(hTab, 1, &tie);
        tie.pszText = const_cast<LPWSTR>(tr(L"その他の設定", L"Other Settings"));
        TabCtrl_SetItem(hTab, 2, &tie);

        SetDlgItemTextW(hwnd, IDC_STATIC_PRESET, tr(L"プリセット:", L"Preset:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_FORMAT, tr(L"形式 (コーデック):", L"Format (Codec):"));
        SetDlgItemTextW(hwnd, IDC_STATIC_BACKEND, tr(L"エンコーダー:", L"Encoder:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_QUALITY, tr(L"品質 (CRF/QP):", L"Quality (CRF/QP):"));
        SetDlgItemTextW(hwnd, IDC_CHECK_ALPHA, tr(L"透過 (アルファ)", L"Alpha Output"));
        SetDlgItemTextW(hwnd, IDC_CHECK_AUDIO, tr(L"音声を含める (AVI音声を自動結合)", L"Include Audio (Merge AVI Audio)"));

        SetDlgItemTextW(hwnd, IDC_STATIC_BIT_DEPTH, tr(L"ビット深度:", L"Bit Depth:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_COLORSPACE, tr(L"色空間:", L"Color Space:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_CHROMA, tr(L"クロマサンプリング:", L"Chroma:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_COLOR_RANGE, tr(L"出力レンジ:", L"Color Range:"));
        SetDlgItemTextW(hwnd, IDC_CHECK_GOP_AUTO, tr(L"GOP自動", L"Auto GOP"));
        SetDlgItemTextW(hwnd, IDC_STATIC_GOP, tr(L"GOP/I間隔:", L"GOP/Keyframe:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_BFRAMES, tr(L"Bフレーム:", L"B-Frames:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_LOOKAHEAD, L"Lookahead:");

        SetDlgItemTextW(hwnd, IDC_STATIC_LANGUAGE, tr(L"表示言語:", L"Language:"));

        SetDlgItemTextW(hwnd, IDC_BUTTON_TEST, tr(L"エンコーダーテスト", L"Encoder Test"));
        SetDlgItemTextW(hwnd, IDC_BUTTON_OPEN_LOG, tr(L"ログを開く", L"Open Log"));
        SetDlgItemTextW(hwnd, IDOK, tr(L"OK", L"OK"));
        SetDlgItemTextW(hwnd, IDCANCEL, tr(L"キャンセル", L"Cancel"));

        auto RepopulateCombo = [](HWND hDlg, int comboId, const wchar_t* const* items, size_t count) {
            int sel = static_cast<int>(SendDlgItemMessageW(hDlg, comboId, CB_GETCURSEL, 0, 0));
            SendDlgItemMessageW(hDlg, comboId, CB_RESETCONTENT, 0, 0);
            for (size_t i = 0; i < count; ++i) {
                SendDlgItemMessageW(hDlg, comboId, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
            }
            if (sel != CB_ERR && sel < static_cast<int>(count)) {
                SendDlgItemMessageW(hDlg, comboId, CB_SETCURSEL, sel, 0);
            }
        };

        const wchar_t* presetsJa[] = {
            L"高品質 H.264 (MP4)",
            L"高品質 HEVC (MP4)",
            L"高品質 AV1 (MP4)",
            L"YouTube (推奨設定)",
            L"編集用 (ProRes 422 HQ MOV)",
            L"透過動画 (アルファ付き)",
            L"静止画連番 (PNG)",
            L"カスタム"
        };
        const wchar_t* presetsEn[] = {
            L"High Quality H.264 (MP4)",
            L"High Quality HEVC (MP4)",
            L"High Quality AV1 (MP4)",
            L"YouTube (Recommended)",
            L"Editing (ProRes 422 HQ MOV)",
            L"Transparent Video (Alpha)",
            L"Image Sequence (PNG)",
            L"Custom"
        };
        RepopulateCombo(hwnd, IDC_COMBO_PRESET, (lang == UILanguage::English ? presetsEn : presetsJa), static_cast<size_t>(kPresetCount));

        const wchar_t* formatsJa[] = { L"H.264 (.mp4)", L"H.265 HEVC (.mp4)", L"AV1 (.mp4)", L"Apple ProRes 422 (.mov)", L"Apple ProRes 422 HQ (.mov)", L"VP9 (.webm)", L"AV1 (.webm)", L"JPEG 連番 (.jpg)", L"PNG 連番 (.png)", L"OpenEXR 連番 (.exr)" };
        const wchar_t* formatsEn[] = { L"H.264 (.mp4)", L"H.265 HEVC (.mp4)", L"AV1 (.mp4)", L"Apple ProRes 422 (.mov)", L"Apple ProRes 422 HQ (.mov)", L"VP9 (.webm)", L"AV1 (.webm)", L"JPEG Sequence (.jpg)", L"PNG Sequence (.png)", L"OpenEXR Sequence (.exr)" };
        RepopulateCombo(hwnd, IDC_COMBO_FORMAT, (lang == UILanguage::English ? formatsEn : formatsJa), kVideoFormatCount);

        int curBkIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETCURSEL, 0, 0));
        BackendItemData curBkData = BACKEND_AUTO;
        if (curBkIdx != CB_ERR) {
            curBkData = static_cast<BackendItemData>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETITEMDATA, curBkIdx, 0));
        }
        SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_RESETCONTENT, 0, 0);
        auto addBackend = [&](const wchar_t* text, BackendItemData id) {
            int idx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)));
            SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_SETITEMDATA, idx, static_cast<LPARAM>(id));
        };
        addBackend(tr(L"自動 (Auto)", L"Auto"), BACKEND_AUTO);
        if (caps.has_nvidia && (caps.nvenc_h264 || caps.nvenc_hevc || caps.nvenc_av1)) {
            addBackend(L"NVIDIA (NVENC)", BACKEND_NVIDIA);
        }
        if (caps.has_intel && (caps.qsv_h264 || caps.qsv_hevc || caps.qsv_av1)) {
            addBackend(L"Intel (QSV)", BACKEND_INTEL);
        }
        if (caps.has_amd && (caps.amf_h264 || caps.amf_hevc || caps.amf_av1)) {
            addBackend(L"AMD (AMF)", BACKEND_AMD);
        }
        addBackend(tr(L"CPU (ソフトウェア)", L"CPU (Software)"), BACKEND_CPU);

        int bkCount = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETCOUNT, 0, 0));
        int selBk = 0;
        for (int i = 0; i < bkCount; ++i) {
            if (static_cast<BackendItemData>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETITEMDATA, i, 0)) == curBkData) {
                selBk = i;
                break;
            }
        }
        SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_SETCURSEL, selBk, 0);

        const wchar_t* alphaFormatsJa[] = { L"Apple ProRes 4444 (.mov)", L"Apple ProRes 4444 XQ (.mov)", L"VP9 (.webm)", L"PNG 連番 (.png)", L"OpenEXR 連番 (.exr)" };
        const wchar_t* alphaFormatsEn[] = { L"Apple ProRes 4444 (.mov)", L"Apple ProRes 4444 XQ (.mov)", L"VP9 (.webm)", L"PNG Sequence (.png)", L"OpenEXR Sequence (.exr)" };
        RepopulateCombo(hwnd, IDC_COMBO_ALPHA_FORMAT, (lang == UILanguage::English ? alphaFormatsEn : alphaFormatsJa), kAlphaFormatCount);

        const wchar_t* depths[] = { L"8 bit", L"10 bit" };
        RepopulateCombo(hwnd, IDC_COMBO_BIT_DEPTH, depths, 2);

        const wchar_t* chromas[] = { L"4:2:0", L"4:2:2", L"4:4:4" };
        RepopulateCombo(hwnd, IDC_COMBO_CHROMA, chromas, 3);

        const wchar_t* spacesJa[] = { L"BT.709 (標準HD)", L"BT.601 (SD)" };
        const wchar_t* spacesEn[] = { L"BT.709 (Standard HD)", L"BT.601 (SD)" };
        RepopulateCombo(hwnd, IDC_COMBO_COLORSPACE, (lang == UILanguage::English ? spacesEn : spacesJa), 2);

        const wchar_t* rangesJa[] = { L"TV (Limited)", L"PC (Full)" };
        const wchar_t* rangesEn[] = { L"TV (Limited)", L"PC (Full)" };
        RepopulateCombo(hwnd, IDC_COMBO_COLOR_RANGE, (lang == UILanguage::English ? rangesEn : rangesJa), 2);

        const wchar_t* langs[] = { tr(L"自動 (System)", L"Auto (System)"), L"日本語", L"English" };
        if (SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_GETCOUNT, 0, 0) != 3) {
            RepopulateCombo(hwnd, IDC_COMBO_LANGUAGE, langs, 3);
        } else {
            int langSel = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_GETCURSEL, 0, 0));
            SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_DELETESTRING, 0, 0);
            SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_INSERTSTRING, 0, reinterpret_cast<LPARAM>(langs[0]));
            SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_SETCURSEL, langSel, 0);
        }
    }

    void ReadConfigFromUI(HWND hwnd, EncoderConfig& cfg);

    void UpdateControlsState(HWND hwnd) {
        EncoderConfig cfg;
        ReadConfigFromUI(hwnd, cfg);
        cfg.ValidateAndCorrect();

        bool isAlpha = cfg.alpha_enabled;
        std::wstring fmt = isAlpha ? cfg.alpha_format : cfg.format;
        bool interFrame = !isAlpha && (fmt == L"h264" || fmt == L"hevc" || fmt == L"av1" || fmt == L"av1webm" || fmt == L"vp9");
        bool interFrameAlpha = isAlpha && fmt == L"vp9";
        bool hardware = cfg.UsesHardwareBackend();

        auto enable = [hwnd](int id, bool on) { EnableWindow(GetDlgItem(hwnd, id), on ? TRUE : FALSE); };

        enable(IDC_COMBO_FORMAT, !isAlpha);
        enable(IDC_COMBO_ALPHA_FORMAT, isAlpha);
        enable(IDC_COMBO_BACKEND, hardware);
        enable(IDC_EDIT_CRF, cfg.UsesQuality());
        enable(IDC_SPIN_CRF, cfg.UsesQuality());
        SendDlgItemMessageW(hwnd, IDC_SPIN_CRF, UDM_SETRANGE, 0, MAKELPARAM(EncoderConfig::MaxQuality(fmt), 0));
        enable(IDC_CHECK_AUDIO, !cfg.IsImageSequence());

        enable(IDC_COMBO_BIT_DEPTH, interFrame);
        enable(IDC_COMBO_CHROMA, interFrame && fmt != L"av1" && fmt != L"av1webm");
        enable(IDC_COMBO_COLORSPACE, cfg.UsesYuv());
        enable(IDC_COMBO_COLOR_RANGE, cfg.UsesYuv());
        enable(IDC_CHECK_GOP_AUTO, interFrame || interFrameAlpha);
        bool autoGop = IsDlgButtonChecked(hwnd, IDC_CHECK_GOP_AUTO) == BST_CHECKED;
        enable(IDC_EDIT_GOP, (interFrame || interFrameAlpha) && !autoGop);
        enable(IDC_EDIT_BFRAMES, interFrame && (fmt == L"h264" || fmt == L"hevc"));
        enable(IDC_EDIT_LOOKAHEAD, hardware && (cfg.backend == L"auto" || cfg.backend == L"nvidia"));
    }

    void PopulateUIFromConfig(HWND hwnd, const EncoderConfig& cfg) {
        SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_SETCURSEL, static_cast<WPARAM>(PresetToIndex(cfg.preset)), 0);

        SendDlgItemMessageW(hwnd, IDC_COMBO_FORMAT, CB_SETCURSEL, KeyToIndex(kVideoFormatKeys, kVideoFormatCount, cfg.format), 0);

        BackendItemData targetBackendId = BACKEND_AUTO;
        if (cfg.backend == L"nvidia") targetBackendId = BACKEND_NVIDIA;
        else if (cfg.backend == L"intel") targetBackendId = BACKEND_INTEL;
        else if (cfg.backend == L"amd") targetBackendId = BACKEND_AMD;
        else if (cfg.backend == L"cpu") targetBackendId = BACKEND_CPU;

        int bkCount = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETCOUNT, 0, 0));
        int selBkIdx = 0;
        for (int i = 0; i < bkCount; ++i) {
            if (static_cast<BackendItemData>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETITEMDATA, i, 0)) == targetBackendId) {
                selBkIdx = i;
                break;
            }
        }
        SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_SETCURSEL, selBkIdx, 0);

        SetDlgItemInt(hwnd, IDC_EDIT_CRF, cfg.crf, FALSE);

        CheckDlgButton(hwnd, IDC_CHECK_AUDIO, cfg.audio_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_CHECK_ALPHA, cfg.alpha_enabled ? BST_CHECKED : BST_UNCHECKED);

        SendDlgItemMessageW(hwnd, IDC_COMBO_ALPHA_FORMAT, CB_SETCURSEL, KeyToIndex(kAlphaFormatKeys, kAlphaFormatCount, cfg.alpha_format), 0);

        SendDlgItemMessageW(hwnd, IDC_COMBO_BIT_DEPTH, CB_SETCURSEL, cfg.bit_depth == 10 ? 1 : 0, 0);

        int chIdx = 0;
        if (cfg.chroma == L"yuv422p" || cfg.chroma == L"yuv422p10le") chIdx = 1;
        else if (cfg.chroma == L"yuv444p" || cfg.chroma == L"yuv444p10le") chIdx = 2;
        SendDlgItemMessageW(hwnd, IDC_COMBO_CHROMA, CB_SETCURSEL, chIdx, 0);

        int csIdx = 0;
        if (cfg.colorspace == L"bt601") csIdx = 1;
        SendDlgItemMessageW(hwnd, IDC_COMBO_COLORSPACE, CB_SETCURSEL, csIdx, 0);

        SendDlgItemMessageW(hwnd, IDC_COMBO_COLOR_RANGE, CB_SETCURSEL, cfg.color_range == L"pc" ? 1 : 0, 0);

        CheckDlgButton(hwnd, IDC_CHECK_GOP_AUTO, cfg.gop_auto ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(hwnd, IDC_EDIT_GOP, cfg.gop_size, FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_EDIT_GOP), !cfg.gop_auto);

        SetDlgItemInt(hwnd, IDC_EDIT_BFRAMES, cfg.b_frames, FALSE);
        SetDlgItemInt(hwnd, IDC_EDIT_LOOKAHEAD, cfg.lookahead, FALSE);

        SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_SETCURSEL, cfg.ui_language, 0);
        UpdateControlsState(hwnd);
    }

    void ReadConfigFromUI(HWND hwnd, EncoderConfig& cfg) {
        cfg.preset = IndexToPreset(static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_GETCURSEL, 0, 0)));

        cfg.format = IndexToKey(kVideoFormatKeys, kVideoFormatCount, static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_FORMAT, CB_GETCURSEL, 0, 0)));

        int bkCur = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETCURSEL, 0, 0));
        BackendItemData bkData = BACKEND_AUTO;
        if (bkCur != CB_ERR) {
            bkData = static_cast<BackendItemData>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETITEMDATA, bkCur, 0));
        }
        switch (bkData) {
        case BACKEND_NVIDIA: cfg.backend = L"nvidia"; break;
        case BACKEND_INTEL: cfg.backend = L"intel"; break;
        case BACKEND_AMD: cfg.backend = L"amd"; break;
        case BACKEND_CPU: cfg.backend = L"cpu"; break;
        default: cfg.backend = L"auto"; break;
        }

        cfg.crf = GetDlgItemInt(hwnd, IDC_EDIT_CRF, NULL, FALSE);

        cfg.audio_enabled = IsDlgButtonChecked(hwnd, IDC_CHECK_AUDIO) == BST_CHECKED;
        cfg.alpha_enabled = IsDlgButtonChecked(hwnd, IDC_CHECK_ALPHA) == BST_CHECKED;

        cfg.alpha_format = IndexToKey(kAlphaFormatKeys, kAlphaFormatCount, static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_ALPHA_FORMAT, CB_GETCURSEL, 0, 0)));

        cfg.bit_depth = SendDlgItemMessageW(hwnd, IDC_COMBO_BIT_DEPTH, CB_GETCURSEL, 0, 0) == 1 ? 10 : 8;

        int chIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_CHROMA, CB_GETCURSEL, 0, 0));
        switch (chIdx) {
        case 1: cfg.chroma = L"yuv422p"; break;
        case 2: cfg.chroma = L"yuv444p"; break;
        default: cfg.chroma = L"yuv420p"; break;
        }

        int csIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_COLORSPACE, CB_GETCURSEL, 0, 0));
        switch (csIdx) {
        case 1: cfg.colorspace = L"bt601"; break;
        default: cfg.colorspace = L"bt709"; break;
        }

        cfg.color_range = SendDlgItemMessageW(hwnd, IDC_COMBO_COLOR_RANGE, CB_GETCURSEL, 0, 0) == 1 ? L"pc" : L"tv";

        cfg.gop_auto = IsDlgButtonChecked(hwnd, IDC_CHECK_GOP_AUTO) == BST_CHECKED;
        cfg.gop_size = GetDlgItemInt(hwnd, IDC_EDIT_GOP, NULL, FALSE);
        cfg.b_frames = GetDlgItemInt(hwnd, IDC_EDIT_BFRAMES, NULL, FALSE);
        cfg.lookahead = GetDlgItemInt(hwnd, IDC_EDIT_LOOKAHEAD, NULL, FALSE);

        cfg.ui_language = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_GETCURSEL, 0, 0));
    }

    INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (uMsg) {
        case WM_INITDIALOG: {
            state = reinterpret_cast<DialogState*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            EnableThemeDialogTexture(hwnd, ETDT_ENABLETAB);

            HICON hIconSm = reinterpret_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
            if (hIconSm) {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSm));
            }
            HICON hIconBig = reinterpret_cast<HICON>(LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
            if (hIconBig) {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
            }

            HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
            SetWindowPos(hTab, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            TCITEMW tie;
            ZeroMemory(&tie, sizeof(tie));
            tie.mask = TCIF_TEXT;
            tie.pszText = const_cast<LPWSTR>(L"");
            TabCtrl_InsertItem(hTab, 0, &tie);
            TabCtrl_InsertItem(hTab, 1, &tie);
            TabCtrl_InsertItem(hTab, 2, &tie);

            SendDlgItemMessageW(hwnd, IDC_SPIN_CRF, UDM_SETRANGE, 0, MAKELPARAM(51, 0));

            EncoderCapabilities caps = EncoderController::GetCachedCapabilities(state->config->ffmpeg_path);
            UILanguage lang = ResolveLanguage(state->config->ui_language);
            UpdateDialogLanguage(hwnd, lang, caps);

            PopulateUIFromConfig(hwnd, *(state->config));
            TabCtrl_SetCurSel(hTab, state->activeTab);
            UpdateTabVisibility(hwnd, state->activeTab);
            state->initializing = false;
            return TRUE;
        }

        case WM_NOTIFY: {
            LPNMHDR pnm = reinterpret_cast<LPNMHDR>(lParam);
            if (pnm->idFrom == IDC_TAB_MAIN && pnm->code == TCN_SELCHANGE) {
                HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
                state->activeTab = TabCtrl_GetCurSel(hTab);
                UpdateTabVisibility(hwnd, state->activeTab);
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);

            if (state && !state->initializing) {
                if (id == IDC_COMBO_PRESET && code == CBN_SELCHANGE) {
                    int sel = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && IndexToPreset(sel) != PresetType::Custom) {
                        state->config->ApplyPreset(IndexToPreset(sel));
                        state->initializing = true;
                        PopulateUIFromConfig(hwnd, *(state->config));
                        state->initializing = false;
                    }
                } else if (code == CBN_SELCHANGE || code == EN_CHANGE || code == BN_CLICKED) {
                    if (id == IDC_CHECK_ALPHA || id == IDC_COMBO_ALPHA_FORMAT ||
                        id == IDC_COMBO_FORMAT || id == IDC_CHECK_GOP_AUTO ||
                        id == IDC_COMBO_BACKEND || id == IDC_COMBO_COLORSPACE) {
                        UpdateControlsState(hwnd);
                    } else if (id == IDC_COMBO_LANGUAGE && code == CBN_SELCHANGE) {
                        PostMessageW(hwnd, kLanguageChangedMessage, 0, 0);
                    }

                    if (id != IDC_COMBO_PRESET && id != IDC_COMBO_LANGUAGE && id != IDC_BUTTON_TEST && id != IDC_BUTTON_OPEN_LOG &&
                        id != IDOK && id != IDCANCEL) {
                        SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_SETCURSEL, static_cast<WPARAM>(PresetToIndex(PresetType::Custom)), 0);
                    }
                }
            }

            if (id == IDC_BUTTON_TEST) {
                EncoderConfig temp = *(state->config);
                ReadConfigFromUI(hwnd, temp);
                temp.ValidateAndCorrect();
                std::wstring msg;
                bool ok = EncoderController::RunEncoderTest(temp, msg);
                UILanguage curLang = ResolveLanguage(temp.ui_language);
                std::wstring testTitle = (curLang == UILanguage::English) ? L"Encoder Test" : L"エンコーダーテスト";
                MessageBoxW(hwnd, msg.c_str(), testTitle.c_str(), ok ? (MB_OK | MB_ICONINFORMATION) : (MB_OK | MB_ICONWARNING));
                return TRUE;
            }

            if (id == IDC_BUTTON_OPEN_LOG) {
                std::wstring logDir = EncoderConfig::GetLogDirectoryPath();
                ShellExecuteW(hwnd, L"open", logDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return TRUE;
            }

            if (id == IDOK) {
                ReadConfigFromUI(hwnd, *(state->config));
                state->config->ValidateAndCorrect();
                state->config->Save();
                EndDialog(hwnd, IDOK);
                return TRUE;
            }

            if (id == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;

        case kLanguageChangedMessage: {
            if (!state) break;
            int selLang = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_GETCURSEL, 0, 0));
            if (selLang < 0) selLang = 0;
            EncoderConfig curCfg = *(state->config);
            ReadConfigFromUI(hwnd, curCfg);
            curCfg.ui_language = selLang;
            state->config->ui_language = selLang;
            EncoderCapabilities caps = EncoderController::GetCachedCapabilities(curCfg.ffmpeg_path);
            state->initializing = true;
            UpdateDialogLanguage(hwnd, ResolveLanguage(selLang), caps);
            PopulateUIFromConfig(hwnd, curCfg);
            TabCtrl_SetCurSel(GetDlgItem(hwnd, IDC_TAB_MAIN), state->activeTab);
            UpdateTabVisibility(hwnd, -1);
            UpdateTabVisibility(hwnd, state->activeTab);
            state->initializing = false;
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return TRUE;
        }
        }

        return FALSE;
    }
}

INT_PTR ShowEncoderSettingsDialog(HWND hParent, EncoderConfig& config, int initialTab) {
    CThemeActivationContext actCtx;
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS };
    InitCommonControlsEx(&icc);
    DialogState state;
    state.config = &config;
    state.activeTab = initialTab;
    return DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_ENCODER_CONFIG), hParent, DialogProc, reinterpret_cast<LPARAM>(&state));
}
