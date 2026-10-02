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

    const int kAdvancedControls[] = {
        IDC_STATIC_BIT_DEPTH, IDC_COMBO_BIT_DEPTH,
        IDC_STATIC_COLORSPACE, IDC_COMBO_COLORSPACE,
        IDC_STATIC_CHROMA, IDC_COMBO_CHROMA,
        IDC_STATIC_COLOR_RANGE, IDC_COMBO_COLOR_RANGE,
        IDC_CHECK_GOP_AUTO, IDC_STATIC_GOP, IDC_EDIT_GOP,
        IDC_STATIC_BFRAMES, IDC_EDIT_BFRAMES,
        IDC_STATIC_LOOKAHEAD, IDC_EDIT_LOOKAHEAD,
        IDC_STATIC_EXTRA_ARGS, IDC_EDIT_EXTRA_ARGS
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
        English = 2,
        Chinese = 3
    };

    UILanguage ResolveLanguage(int ui_language) {
        if (ui_language == 1) return UILanguage::Japanese;
        if (ui_language == 2) return UILanguage::English;
        if (ui_language == 3) return UILanguage::Chinese;
        LANGID lang = PRIMARYLANGID(GetUserDefaultUILanguage());
        if (lang == LANG_CHINESE) return UILanguage::Chinese;
        if (lang == LANG_JAPANESE) return UILanguage::Japanese;
        return UILanguage::English;
    }

    void UpdateDialogLanguage(HWND hwnd, UILanguage lang, const EncoderCapabilities& caps) {
        auto tr = [lang](const wchar_t* ja, const wchar_t* en, const wchar_t* zh) -> const wchar_t* {
            if (lang == UILanguage::Chinese) return zh;
            if (lang == UILanguage::English) return en;
            return ja;
        };

        SetWindowTextW(hwnd, tr(L"MMDirect Encoder 設定", L"MMDirect Encoder Settings", L"MMDirect Encoder 设置"));

        HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
        TCITEMW tie;
        ZeroMemory(&tie, sizeof(tie));
        tie.mask = TCIF_TEXT;
        tie.pszText = const_cast<LPWSTR>(tr(L"基本設定", L"Basic Settings", L"基本设置"));
        TabCtrl_SetItem(hTab, 0, &tie);
        tie.pszText = const_cast<LPWSTR>(tr(L"詳細設定", L"Advanced Settings", L"高级设置"));
        TabCtrl_SetItem(hTab, 1, &tie);
        tie.pszText = const_cast<LPWSTR>(tr(L"その他の設定", L"Other Settings", L"其他设置"));
        TabCtrl_SetItem(hTab, 2, &tie);

        SetDlgItemTextW(hwnd, IDC_STATIC_PRESET, tr(L"プリセット:", L"Preset:", L"预设:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_FORMAT, tr(L"形式 (コーデック):", L"Format (Codec):", L"格式 (编码):"));
        SetDlgItemTextW(hwnd, IDC_STATIC_BACKEND, tr(L"エンコーダー:", L"Encoder:", L"编码器:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_QUALITY, tr(L"品質 (CRF/QP):", L"Quality (CRF/QP):", L"质量 (CRF/QP):"));
        SetDlgItemTextW(hwnd, IDC_CHECK_ALPHA, tr(L"透過 (アルファ)", L"Alpha Output", L"透明 (Alpha)"));
        SetDlgItemTextW(hwnd, IDC_CHECK_AUDIO, tr(L"音声を含める (AVI音声を自動結合)", L"Include Audio (Merge AVI Audio)", L"包含音频 (自动合并AVI音频)"));

        SetDlgItemTextW(hwnd, IDC_STATIC_BIT_DEPTH, tr(L"ビット深度:", L"Bit Depth:", L"位深度:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_COLORSPACE, tr(L"色空間:", L"Color Space:", L"色彩空间:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_CHROMA, tr(L"クロマサンプリング:", L"Chroma:", L"色度抽样:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_COLOR_RANGE, tr(L"出力レンジ:", L"Color Range:", L"颜色范围:"));
        SetDlgItemTextW(hwnd, IDC_CHECK_GOP_AUTO, tr(L"GOP自動", L"Auto GOP", L"自动GOP"));
        SetDlgItemTextW(hwnd, IDC_STATIC_GOP, tr(L"GOP/I間隔:", L"GOP/Keyframe:", L"GOP/关键帧:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_BFRAMES, tr(L"Bフレーム:", L"B-Frames:", L"B帧数量:"));
        SetDlgItemTextW(hwnd, IDC_STATIC_LOOKAHEAD, L"Lookahead:");
        SetDlgItemTextW(hwnd, IDC_STATIC_EXTRA_ARGS, tr(L"FFmpeg 追加引数:", L"FFmpeg Args:", L"FFmpeg 附加参数:"));

        SetDlgItemTextW(hwnd, IDC_STATIC_LANGUAGE, tr(L"表示言語:", L"Language:", L"显示语言:"));

        SetDlgItemTextW(hwnd, IDC_BUTTON_TEST, tr(L"エンコーダーテスト", L"Encoder Test", L"编码器测试"));
        SetDlgItemTextW(hwnd, IDC_BUTTON_OPEN_LOG, tr(L"ログを開く", L"Open Log", L"打开日志"));
        SetDlgItemTextW(hwnd, IDOK, tr(L"OK", L"OK", L"确定"));
        SetDlgItemTextW(hwnd, IDCANCEL, tr(L"キャンセル", L"Cancel", L"取消"));

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
            L"編集用 (ProRes 422 MOV)",
            L"透過動画 (アルファ付き)",
            L"ロスレス (可逆圧縮)",
            L"静止画連番 (PNG)",
            L"カスタム"
        };
        const wchar_t* presetsEn[] = {
            L"High Quality H.264 (MP4)",
            L"High Quality HEVC (MP4)",
            L"High Quality AV1 (MP4)",
            L"YouTube (Recommended)",
            L"Editing (ProRes 422 MOV)",
            L"Transparent Video (Alpha)",
            L"Lossless",
            L"Image Sequence (PNG)",
            L"Custom"
        };
        const wchar_t* presetsZh[] = {
            L"高质量 H.264 (MP4)",
            L"高质量 HEVC (MP4)",
            L"高质量 AV1 (MP4)",
            L"YouTube (推荐设置)",
            L"剪辑专用 (ProRes 422 MOV)",
            L"透明视频 (含Alpha)",
            L"无损 (Lossless)",
            L"图片序列 (PNG)",
            L"自定义"
        };
        RepopulateCombo(hwnd, IDC_COMBO_PRESET, (lang == UILanguage::Chinese ? presetsZh : (lang == UILanguage::English ? presetsEn : presetsJa)), 9);

        const wchar_t* formatsJa[] = { L"H.264 (.mp4)", L"HEVC H.265 (.mp4)", L"AV1 (.mp4)", L"VP9 (.webm)", L"ProRes (.mov)", L"PNG 静止画連番 (.png)", L"JPEG 静止画連番 (.jpg)" };
        const wchar_t* formatsEn[] = { L"H.264 (.mp4)", L"HEVC H.265 (.mp4)", L"AV1 (.mp4)", L"VP9 (.webm)", L"ProRes (.mov)", L"PNG Image Sequence (.png)", L"JPEG Image Sequence (.jpg)" };
        const wchar_t* formatsZh[] = { L"H.264 (.mp4)", L"HEVC H.265 (.mp4)", L"AV1 (.mp4)", L"VP9 (.webm)", L"ProRes (.mov)", L"PNG 图片序列 (.png)", L"JPEG 图片序列 (.jpg)" };
        RepopulateCombo(hwnd, IDC_COMBO_FORMAT, (lang == UILanguage::Chinese ? formatsZh : (lang == UILanguage::English ? formatsEn : formatsJa)), 7);

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
        addBackend(tr(L"自動 (Auto)", L"Auto", L"自动 (Auto)"), BACKEND_AUTO);
        if (caps.has_nvidia && (caps.nvenc_h264 || caps.nvenc_hevc || caps.nvenc_av1)) {
            addBackend(L"NVIDIA (NVENC)", BACKEND_NVIDIA);
        }
        if (caps.has_intel && (caps.qsv_h264 || caps.qsv_hevc || caps.qsv_av1)) {
            addBackend(L"Intel (QSV)", BACKEND_INTEL);
        }
        if (caps.has_amd && (caps.amf_h264 || caps.amf_hevc || caps.amf_av1)) {
            addBackend(L"AMD (AMF)", BACKEND_AMD);
        }
        addBackend(tr(L"CPU (ソフトウェア)", L"CPU (Software)", L"CPU (软件编码)"), BACKEND_CPU);

        int bkCount = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETCOUNT, 0, 0));
        int selBk = 0;
        for (int i = 0; i < bkCount; ++i) {
            if (static_cast<BackendItemData>(SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_GETITEMDATA, i, 0)) == curBkData) {
                selBk = i;
                break;
            }
        }
        SendDlgItemMessageW(hwnd, IDC_COMBO_BACKEND, CB_SETCURSEL, selBk, 0);

        const wchar_t* alphaFormatsJa[] = { L"ProRes 4444 (.mov)", L"VP9 (.webm)", L"PNG 静止画連番 (.png)", L"FFV1 (.mkv)" };
        const wchar_t* alphaFormatsEn[] = { L"ProRes 4444 (.mov)", L"VP9 (.webm)", L"PNG Image Sequence (.png)", L"FFV1 (.mkv)" };
        const wchar_t* alphaFormatsZh[] = { L"ProRes 4444 (.mov)", L"VP9 (.webm)", L"PNG 图片序列 (.png)", L"FFV1 (.mkv)" };
        RepopulateCombo(hwnd, IDC_COMBO_ALPHA_FORMAT, (lang == UILanguage::Chinese ? alphaFormatsZh : (lang == UILanguage::English ? alphaFormatsEn : alphaFormatsJa)), 4);

        const wchar_t* depths[] = { L"8 bit", L"10 bit" };
        RepopulateCombo(hwnd, IDC_COMBO_BIT_DEPTH, depths, 2);

        const wchar_t* chromas[] = { L"4:2:0", L"4:2:2", L"4:4:4" };
        RepopulateCombo(hwnd, IDC_COMBO_CHROMA, chromas, 3);

        const wchar_t* spacesJa[] = { L"BT.709 (標準HD)", L"BT.601 (SD)", L"BT.2020 (広色域)" };
        const wchar_t* spacesEn[] = { L"BT.709 (Standard HD)", L"BT.601 (SD)", L"BT.2020 (Wide Gamut)" };
        const wchar_t* spacesZh[] = { L"BT.709 (标准HD)", L"BT.601 (SD)", L"BT.2020 (广色域)" };
        RepopulateCombo(hwnd, IDC_COMBO_COLORSPACE, (lang == UILanguage::Chinese ? spacesZh : (lang == UILanguage::English ? spacesEn : spacesJa)), 3);

        const wchar_t* rangesJa[] = { L"TV (Limited)", L"PC (Full)" };
        const wchar_t* rangesEn[] = { L"TV (Limited)", L"PC (Full)" };
        const wchar_t* rangesZh[] = { L"TV (Limited/受限)", L"PC (Full/全范围)" };
        RepopulateCombo(hwnd, IDC_COMBO_COLOR_RANGE, (lang == UILanguage::Chinese ? rangesZh : (lang == UILanguage::English ? rangesEn : rangesJa)), 2);

        const wchar_t* langs[] = { tr(L"自動 (System)", L"Auto (System)", L"自动 (System)"), L"日本語", L"English", L"简体中文" };
        RepopulateCombo(hwnd, IDC_COMBO_LANGUAGE, langs, 4);
    }

    void UpdateControlsState(HWND hwnd) {
        bool isAlpha = IsDlgButtonChecked(hwnd, IDC_CHECK_ALPHA) == BST_CHECKED;
        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_FORMAT), !isAlpha);
        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_ALPHA_FORMAT), isAlpha);

        bool isSeq = false;
        if (isAlpha) {
            int aFmtIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_ALPHA_FORMAT, CB_GETCURSEL, 0, 0));
            if (aFmtIdx == 2) isSeq = true;
        } else {
            int fmtIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_FORMAT, CB_GETCURSEL, 0, 0));
            if (fmtIdx == 5 || fmtIdx == 6) isSeq = true;
        }

        if (isSeq) {
            EnableWindow(GetDlgItem(hwnd, IDC_COMBO_BACKEND), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_EDIT_CRF), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_SPIN_CRF), FALSE);
        } else {
            EnableWindow(GetDlgItem(hwnd, IDC_COMBO_BACKEND), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_EDIT_CRF), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_SPIN_CRF), TRUE);
        }

        bool autoGop = IsDlgButtonChecked(hwnd, IDC_CHECK_GOP_AUTO) == BST_CHECKED;
        EnableWindow(GetDlgItem(hwnd, IDC_EDIT_GOP), !autoGop);
    }

    void PopulateUIFromConfig(HWND hwnd, const EncoderConfig& cfg) {
        SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_SETCURSEL, static_cast<WPARAM>(cfg.preset), 0);

        int fmtIdx = 0;
        if (cfg.format == L"hevc") fmtIdx = 1;
        else if (cfg.format == L"av1") fmtIdx = 2;
        else if (cfg.format == L"vp9") fmtIdx = 3;
        else if (cfg.format == L"prores") fmtIdx = 4;
        else if (cfg.format == L"png") fmtIdx = 5;
        else if (cfg.format == L"jpg" || cfg.format == L"jpeg") fmtIdx = 6;
        SendDlgItemMessageW(hwnd, IDC_COMBO_FORMAT, CB_SETCURSEL, fmtIdx, 0);

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

        int aFmtIdx = 0;
        if (cfg.alpha_format == L"vp9") aFmtIdx = 1;
        else if (cfg.alpha_format == L"png") aFmtIdx = 2;
        else if (cfg.alpha_format == L"ffv1") aFmtIdx = 3;
        SendDlgItemMessageW(hwnd, IDC_COMBO_ALPHA_FORMAT, CB_SETCURSEL, aFmtIdx, 0);

        SendDlgItemMessageW(hwnd, IDC_COMBO_BIT_DEPTH, CB_SETCURSEL, cfg.bit_depth == 10 ? 1 : 0, 0);

        int chIdx = 0;
        if (cfg.chroma == L"yuv422p" || cfg.chroma == L"yuv422p10le") chIdx = 1;
        else if (cfg.chroma == L"yuv444p" || cfg.chroma == L"yuv444p10le") chIdx = 2;
        SendDlgItemMessageW(hwnd, IDC_COMBO_CHROMA, CB_SETCURSEL, chIdx, 0);

        int csIdx = 0;
        if (cfg.colorspace == L"bt601") csIdx = 1;
        else if (cfg.colorspace == L"bt2020") csIdx = 2;
        SendDlgItemMessageW(hwnd, IDC_COMBO_COLORSPACE, CB_SETCURSEL, csIdx, 0);

        SendDlgItemMessageW(hwnd, IDC_COMBO_COLOR_RANGE, CB_SETCURSEL, cfg.color_range == L"pc" ? 1 : 0, 0);

        CheckDlgButton(hwnd, IDC_CHECK_GOP_AUTO, cfg.gop_auto ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(hwnd, IDC_EDIT_GOP, cfg.gop_size, FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_EDIT_GOP), !cfg.gop_auto);

        SetDlgItemInt(hwnd, IDC_EDIT_BFRAMES, cfg.b_frames, FALSE);
        SetDlgItemInt(hwnd, IDC_EDIT_LOOKAHEAD, cfg.lookahead, FALSE);

        SetDlgItemTextW(hwnd, IDC_EDIT_EXTRA_ARGS, cfg.extra_args.c_str());

        SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_SETCURSEL, cfg.ui_language, 0);
        UpdateControlsState(hwnd);
    }

    void ReadConfigFromUI(HWND hwnd, EncoderConfig& cfg) {
        cfg.preset = static_cast<PresetType>(SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_GETCURSEL, 0, 0));

        int fmtIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_FORMAT, CB_GETCURSEL, 0, 0));
        switch (fmtIdx) {
        case 1: cfg.format = L"hevc"; break;
        case 2: cfg.format = L"av1"; break;
        case 3: cfg.format = L"vp9"; break;
        case 4: cfg.format = L"prores"; break;
        case 5: cfg.format = L"png"; break;
        case 6: cfg.format = L"jpg"; break;
        default: cfg.format = L"h264"; break;
        }

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

        int aFmtIdx = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_ALPHA_FORMAT, CB_GETCURSEL, 0, 0));
        switch (aFmtIdx) {
        case 1: cfg.alpha_format = L"vp9"; break;
        case 2: cfg.alpha_format = L"png"; break;
        case 3: cfg.alpha_format = L"ffv1"; break;
        default: cfg.alpha_format = L"prores"; break;
        }

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
        case 2: cfg.colorspace = L"bt2020"; break;
        default: cfg.colorspace = L"bt709"; break;
        }

        cfg.color_range = SendDlgItemMessageW(hwnd, IDC_COMBO_COLOR_RANGE, CB_GETCURSEL, 0, 0) == 1 ? L"pc" : L"tv";

        cfg.gop_auto = IsDlgButtonChecked(hwnd, IDC_CHECK_GOP_AUTO) == BST_CHECKED;
        cfg.gop_size = GetDlgItemInt(hwnd, IDC_EDIT_GOP, NULL, FALSE);
        cfg.b_frames = GetDlgItemInt(hwnd, IDC_EDIT_BFRAMES, NULL, FALSE);
        cfg.lookahead = GetDlgItemInt(hwnd, IDC_EDIT_LOOKAHEAD, NULL, FALSE);

        std::vector<wchar_t> largeBuf(4096);
        GetDlgItemTextW(hwnd, IDC_EDIT_EXTRA_ARGS, largeBuf.data(), 4096);
        cfg.extra_args = largeBuf.data();

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
                    if (sel >= 0 && sel < 8) {
                        state->config->ApplyPreset(static_cast<PresetType>(sel));
                        state->initializing = true;
                        PopulateUIFromConfig(hwnd, *(state->config));
                        state->initializing = false;
                    }
                } else if (code == CBN_SELCHANGE || code == EN_CHANGE || code == BN_CLICKED) {
                    if (id == IDC_CHECK_ALPHA || id == IDC_COMBO_ALPHA_FORMAT ||
                        id == IDC_COMBO_FORMAT || id == IDC_CHECK_GOP_AUTO) {
                        UpdateControlsState(hwnd);
                    } else if (id == IDC_COMBO_LANGUAGE && code == CBN_SELCHANGE) {
                        int selLang = static_cast<int>(SendDlgItemMessageW(hwnd, IDC_COMBO_LANGUAGE, CB_GETCURSEL, 0, 0));
                        EncoderConfig curCfg;
                        ReadConfigFromUI(hwnd, curCfg);
                        curCfg.ui_language = selLang;
                        state->config->ui_language = selLang;
                        UILanguage newLang = ResolveLanguage(selLang);
                        EncoderCapabilities caps = EncoderController::GetCachedCapabilities(curCfg.ffmpeg_path);
                        state->initializing = true;
                        UpdateDialogLanguage(hwnd, newLang, caps);
                        PopulateUIFromConfig(hwnd, curCfg);
                        HWND hTab = GetDlgItem(hwnd, IDC_TAB_MAIN);
                        TabCtrl_SetCurSel(hTab, state->activeTab);
                        UpdateTabVisibility(hwnd, state->activeTab);
                        state->initializing = false;
                    }

                    if (id != IDC_COMBO_PRESET && id != IDC_COMBO_LANGUAGE && id != IDC_BUTTON_TEST && id != IDC_BUTTON_OPEN_LOG && id != IDOK && id != IDCANCEL) {
                        SendDlgItemMessageW(hwnd, IDC_COMBO_PRESET, CB_SETCURSEL, static_cast<WPARAM>(PresetType::Custom), 0);
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
                std::wstring testTitle = (curLang == UILanguage::Chinese) ? L"编码器测试" : (curLang == UILanguage::English ? L"Encoder Test" : L"エンコーダーテスト");
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
