#include <windows.h>
#include <iostream>
#include <cassert>

#include "../../config/encoder_config.h"
#include "../../encoder/encoder_controller.h"

HINSTANCE g_hInst = NULL;

int main() {
    std::cout << "[Test 1] Testing EncoderConfig Presets..." << std::endl;
    EncoderConfig cfg;
    cfg.ApplyPreset(PresetType::HighQualityH264);
    assert(cfg.format == L"h264");
    assert(cfg.container == L"mp4");
    assert(cfg.crf == 18);

    cfg.ApplyPreset(PresetType::HighQualityHEVC);
    assert(cfg.format == L"hevc");
    assert(cfg.container == L"mp4");

    cfg.ApplyPreset(PresetType::HighQualityAV1);
    assert(cfg.format == L"av1");
    assert(cfg.container == L"mp4");

    cfg.ApplyPreset(PresetType::Editing);
    assert(cfg.format == L"prores");
    assert(cfg.container == L"mov");

    cfg.ApplyPreset(PresetType::Transparent);
    assert(cfg.alpha_enabled == true);
    assert(cfg.container == L"mov");

    cfg.ApplyPreset(PresetType::Lossless);
    assert(cfg.crf == 0);

    cfg.ApplyPreset(PresetType::PNGSequence);
    assert(cfg.format == L"png");
    assert(cfg.container == L"png");
    assert(cfg.chroma == L"rgb24");
    assert(cfg.crf == 0);
    std::cout << "-> Presets passed." << std::endl;

    std::wstring testIni = L"test_config.ini";
    cfg.crf = 22;
    cfg.gop_size = 120;
    cfg.Save(testIni);

    EncoderConfig loadedCfg;
    loadedCfg.Load(testIni);
    assert(loadedCfg.crf == 22);
    assert(loadedCfg.gop_size == 120);
    DeleteFileW(testIni.c_str());
    std::cout << "-> Save/Load passed." << std::endl;

    std::cout << "[Test 3] Testing EncoderCapabilities Probe..." << std::endl;
    std::wstring ffmpeg = EncoderConfig::ResolveExecutable(L"ffmpeg", L"");
    EncoderCapabilities caps = EncoderController::ProbeCapabilities(ffmpeg);
    std::cout << "-> NVENC H264: " << (caps.nvenc_h264 ? "YES" : "NO") << std::endl;
    std::cout << "-> NVENC HEVC: " << (caps.nvenc_hevc ? "YES" : "NO") << std::endl;
    std::cout << "-> NVENC AV1:  " << (caps.nvenc_av1  ? "YES" : "NO") << std::endl;
    std::cout << "-> QSV H264:   " << (caps.qsv_h264 ? "YES" : "NO") << std::endl;
    std::cout << "-> AMF H264:   " << (caps.amf_h264 ? "YES" : "NO") << std::endl;
    assert(caps.nvenc_h264 == true);
    assert(caps.nvenc_hevc == true);
    EncoderCapabilities cachedCaps = EncoderController::GetCachedCapabilities(ffmpeg);
    assert(cachedCaps.has_nvidia == caps.has_nvidia);
    assert(cachedCaps.nvenc_h264 == caps.nvenc_h264);
    assert(cachedCaps.nvenc_hevc == caps.nvenc_hevc);

    std::cout << "[Test 4] Testing ExecutionPlan generation..." << std::endl;
    cfg.ApplyPreset(PresetType::HighQualityH264);
    ExecutionPlan planH264 = EncoderController::PrepareExecutionPlan(cfg, 1920, 1080, 60.0, L"bgr24", false, L"C:\\temp\\render.avi");
    assert(!planH264.main_command.empty());
    assert(planH264.primary_output_path == L"C:\\temp\\render.mp4");
    assert(planH264.chosen_codec == L"h264_nvenc");

    cfg.ApplyPreset(PresetType::Transparent);
    ExecutionPlan planAlpha = EncoderController::PrepareExecutionPlan(cfg, 1920, 1080, 60.0, L"bgra", false, L"C:\\temp\\render.avi");
    assert(planAlpha.primary_output_path == L"C:\\temp\\render.mov");
    assert(planAlpha.chosen_codec == L"prores_ks");

    cfg.ApplyPreset(PresetType::PNGSequence);
    ExecutionPlan planPng = EncoderController::PrepareExecutionPlan(cfg, 1920, 1080, 60.0, L"bgr24", true, L"C:\\temp\\render.avi");
    assert(planPng.is_image_sequence == true);
    assert(planPng.primary_output_path == L"C:\\temp\\render_00000.png");
    assert(planPng.audio_output_path == L"C:\\temp\\render.wav");
    assert(planPng.chosen_codec == L"png");
    assert(planPng.main_command.find(L"-start_number 0") != std::wstring::npos);

    EncoderConfig cfgJpg;
    cfgJpg.format = L"jpg";
    ExecutionPlan planJpg = EncoderController::PrepareExecutionPlan(cfgJpg, 1920, 1080, 60.0, L"bgr24", true, L"C:\\temp\\render.avi");
    assert(planJpg.is_image_sequence == true);
    assert(planJpg.primary_output_path == L"C:\\temp\\render_00000.jpg");
    assert(planJpg.chosen_codec == L"mjpeg");
    assert(planJpg.main_command.find(L"-c:v mjpeg") != std::wstring::npos);
    assert(planJpg.main_command.find(L"-start_number 0") != std::wstring::npos);
    std::cout << "-> ExecutionPlan passed." << std::endl;

    std::cout << "[Test 5] Testing RunEncoderTest..." << std::endl;
    EncoderConfig testCfg;
    testCfg.ApplyPreset(PresetType::HighQualityH264);
    std::wstring testMsg;
    bool testOk = EncoderController::RunEncoderTest(testCfg, testMsg);
    assert(testOk == true);
    std::wcout << L"-> RunEncoderTest passed: " << testMsg << std::endl;

    std::cout << "ALL SUITE TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
