#include <windows.h>
#include <iostream>
#include <cassert>
#include <vector>

#include "../../config/encoder_config.h"
#include "../../encoder/encoder_controller.h"
#include "../../encoder/exr_writer.h"

HINSTANCE g_hInst = NULL;

static bool Has(const std::wstring& s, const wchar_t* part) {
    return s.find(part) != std::wstring::npos;
}

struct Combo {
    bool alpha;
    const wchar_t* key;
    const wchar_t* ext;
    const wchar_t* codec;
    const wchar_t* marker;
};

int main() {
    std::cout << "[Test 1] Presets and validation..." << std::endl;
    EncoderConfig cfg;
    cfg.ApplyPreset(PresetType::HighQualityH264);
    assert(cfg.format == L"h264" && cfg.container == L"mp4");
    cfg.ApplyPreset(PresetType::HighQualityHEVC);
    assert(cfg.format == L"hevc");
    cfg.ApplyPreset(PresetType::HighQualityAV1);
    assert(cfg.format == L"av1");
    cfg.ApplyPreset(PresetType::Editing);
    assert(cfg.format == L"prores422hq" && cfg.container == L"mov");
    cfg.ApplyPreset(PresetType::Transparent);
    assert(cfg.alpha_enabled && cfg.alpha_format == L"prores4444" && cfg.container == L"mov");
    cfg.ApplyPreset(PresetType::PNGSequence);
    assert(cfg.format == L"png" && cfg.container == L"png");

    EncoderConfig legacy;
    legacy.format = L"ffv1";
    legacy.alpha_format = L"utvideo";
    legacy.colorspace = L"bt2020";
    legacy.ValidateAndCorrect();
    assert(legacy.format == L"h264");
    assert(legacy.alpha_format == L"prores4444");
    assert(legacy.colorspace == L"bt709");
    legacy.format = L"prores";
    legacy.ValidateAndCorrect();
    assert(legacy.format == L"prores422hq");

    std::wstring testIni = L"test_config.ini";
    EncoderConfig saved;
    saved.format = L"exr";
    saved.alpha_enabled = true;
    saved.alpha_format = L"prores4444xq";
    saved.crf = 22;
    saved.Save(testIni);
    EncoderConfig loaded;
    loaded.Load(testIni);
    assert(loaded.format == L"exr");
    assert(loaded.alpha_format == L"prores4444xq");
    assert(loaded.crf == 22);
    DeleteFileW(testIni.c_str());
    assert(EncoderController::DigitCount(150) == 3);
    std::cout << "-> passed." << std::endl;

    std::cout << "[Test 2] Fifteen output combinations..." << std::endl;
    const Combo combos[] = {
        { false, L"h264", L"mp4", nullptr, L"" },
        { false, L"hevc", L"mp4", nullptr, L"-tag:v hvc1" },
        { false, L"av1", L"mp4", nullptr, L"" },
        { false, L"prores422", L"mov", L"prores_ks", L"-profile:v 2 -vendor apl0" },
        { false, L"prores422hq", L"mov", L"prores_ks", L"-profile:v 3 -vendor apl0" },
        { false, L"vp9", L"webm", L"libvpx-vp9", L"-pix_fmt yuv420p" },
        { false, L"av1webm", L"webm", nullptr, L"" },
        { true, L"prores4444", L"mov", L"prores_ks", L"-profile:v 4 -vendor apl0" },
        { true, L"prores4444xq", L"mov", L"prores_ks", L"-profile:v 5 -vendor apl0" },
        { true, L"vp9", L"webm", L"libvpx-vp9", L"-pix_fmt yuva420p" },
        { false, L"jpg", L"jpg", L"mjpeg", L"-c:v mjpeg" },
        { false, L"png", L"png", L"png", L"-pix_fmt rgb24" },
        { false, L"exr", L"exr", L"exr", L"" },
        { true, L"png", L"png", L"png", L"-pix_fmt rgba" },
        { true, L"exr", L"exr", L"exr", L"" }
    };
    MmdOutputInfo mmd;
    mmd.valid = true;
    mmd.start_frame = 120;
    mmd.end_frame = 150;
    mmd.fps = 30.0;
    const std::wstring avi = L"C:\\temp\\render.avi";
    for (const Combo& c : combos) {
        EncoderConfig e;
        e.ui_language = 2;
        e.alpha_enabled = c.alpha;
        if (c.alpha) e.alpha_format = c.key; else e.format = c.key;
        e.ValidateAndCorrect();
        ExecutionPlan plan = EncoderController::PrepareExecutionPlan(e, 1920, 1080, 333333, c.alpha ? L"bgra" : L"bgr24", true, avi, mmd);
        assert(plan.output_ext == c.ext);
        if (c.codec) assert(plan.chosen_codec == c.codec);
        if (c.marker[0]) assert(Has(plan.main_command, c.marker));
        assert(Has(plan.main_command, L"-f h264 pipe:1"));
        if (plan.is_image_sequence) {
            assert(plan.audio_codec.empty());
            assert(plan.primary_output_path == std::wstring(L"C:\\temp\\render_120.") + c.ext);
        } else {
            assert(plan.primary_output_path == std::wstring(L"C:\\temp\\render.") + c.ext);
        }
        if (plan.native_exr) {
            assert(!Has(plan.main_command, L"render_%03d"));
        }
        std::wstring msg;
        bool ok = EncoderController::RunEncoderTest(e, msg);
        std::wcout << L"-> " << (c.alpha ? L"alpha " : L"") << c.key << L": " << msg << std::endl;
        assert(ok);
    }
    const wchar_t* cpuKeys[] = { L"h264", L"hevc", L"av1", L"av1webm" };
    const wchar_t* cpuCodecs[] = { L"libx264", L"libx265", L"libsvtav1", L"libsvtav1" };
    for (int i = 0; i < 4; ++i) {
        EncoderConfig e;
        e.ui_language = 2;
        e.format = cpuKeys[i];
        e.backend = L"cpu";
        e.ValidateAndCorrect();
        ExecutionPlan plan = EncoderController::PrepareExecutionPlan(e, 1920, 1080, 333333, L"bgr24", true, avi, mmd);
        assert(plan.chosen_codec == cpuCodecs[i]);
        std::wstring msg;
        bool ok = EncoderController::RunEncoderTest(e, msg);
        std::wcout << L"-> cpu " << cpuKeys[i] << L": " << msg << std::endl;
        assert(ok);
    }
    {
        EncoderConfig e;
        e.format = L"png";
        e.ValidateAndCorrect();
        ExecutionPlan plan = EncoderController::PrepareExecutionPlan(e, 64, 64, 333333, L"bgr24", true, L"C:\\temp\\100%_take.avi", mmd);
        assert(Has(plan.main_command, L"100%%_take_%03d.png"));
        assert(plan.primary_output_path == L"C:\\temp\\100%_take_120.png");
    }
    std::cout << "-> passed." << std::endl;

    std::cout << "[Test 3] EXR writer..." << std::endl;
    std::vector<BYTE> frame(4 * 4 * 4);
    for (int i = 0; i < 16; ++i) {
        frame[i * 4 + 0] = static_cast<BYTE>(i * 16);
        frame[i * 4 + 1] = 128;
        frame[i * 4 + 2] = 255;
        frame[i * 4 + 3] = static_cast<BYTE>(i * 17);
    }
    assert(WriteExrFrame(L"exr_test_rgba.exr", frame.data(), 4, 4, 16, L"bgra", false, true));
    assert(WriteExrFrame(L"exr_test_rgb.exr", frame.data(), 4, 4, 16, L"bgra", true, false));
    std::cout << "-> passed." << std::endl;

    std::cout << "ALL SUITE TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
