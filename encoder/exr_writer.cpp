#include "exr_writer.h"

#include <ImfChannelList.h>
#include <ImfCompression.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfOutputFile.h>
#include <ImfThreading.h>
#include <half.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace {
    struct Tables {
        float linear[256];
        float alpha[256];
        Tables() {
            for (int i = 0; i < 256; ++i) {
                double c = i / 255.0;
                linear[i] = static_cast<float>((c <= 0.04045) ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
                alpha[i] = static_cast<float>(c);
            }
        }
    };

    const Tables& GetTables() {
        static const Tables tables;
        return tables;
    }

    void EnsureThreadPool() {
        static std::once_flag once;
        std::call_once(once, []() {
            unsigned count = (std::max)(1u, (std::min)(std::thread::hardware_concurrency(), 16u));
            Imf::setGlobalThreadCount(static_cast<int>(count));
        });
    }

    void ReadPixel(const BYTE* row, int x, const std::wstring& pixfmt, BYTE& r, BYTE& g, BYTE& b, BYTE& a) {
        if (pixfmt == L"bgr24") {
            const BYTE* p = row + x * 3;
            b = p[0]; g = p[1]; r = p[2]; a = 255;
        } else if (pixfmt == L"rgb565le" || pixfmt == L"rgb555le") {
            uint16_t v = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
            if (pixfmt == L"rgb565le") {
                uint32_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
                r = static_cast<BYTE>((r5 << 3) | (r5 >> 2));
                g = static_cast<BYTE>((g6 << 2) | (g6 >> 4));
                b = static_cast<BYTE>((b5 << 3) | (b5 >> 2));
            } else {
                uint32_t r5 = (v >> 10) & 0x1F, g5 = (v >> 5) & 0x1F, b5 = v & 0x1F;
                r = static_cast<BYTE>((r5 << 3) | (r5 >> 2));
                g = static_cast<BYTE>((g5 << 3) | (g5 >> 2));
                b = static_cast<BYTE>((b5 << 3) | (b5 >> 2));
            }
            a = 255;
        } else {
            const BYTE* p = row + x * 4;
            b = p[0]; g = p[1]; r = p[2]; a = p[3];
        }
    }

    std::string WideToUtf8(const std::wstring& w) {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
        std::string s(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, NULL, NULL);
        return s;
    }
}

bool WriteExrFrame(const std::wstring& path,
                   const BYTE* data,
                   int width,
                   int height,
                   long stride,
                   const std::wstring& pixfmt,
                   bool bottomUp,
                   bool withAlpha) {
    if (!data || width <= 0 || height <= 0 || stride <= 0) return false;
    EnsureThreadPool();
    const Tables& tables = GetTables();
    const int channels = withAlpha ? 4 : 3;

    std::vector<half> pixels(static_cast<size_t>(width) * height * channels);
    for (int y = 0; y < height; ++y) {
        int srcY = bottomUp ? (height - 1 - y) : y;
        const BYTE* row = data + static_cast<size_t>(srcY) * stride;
        half* dst = pixels.data() + static_cast<size_t>(y) * width * channels;
        for (int x = 0; x < width; ++x) {
            BYTE r, g, b, a;
            ReadPixel(row, x, pixfmt, r, g, b, a);
            float scale = withAlpha ? tables.alpha[a] : 1.0f;
            dst[0] = half(tables.linear[r] * scale);
            dst[1] = half(tables.linear[g] * scale);
            dst[2] = half(tables.linear[b] * scale);
            if (withAlpha) dst[3] = half(tables.alpha[a]);
            dst += channels;
        }
    }

    std::wstring temp = path + L".partial";
    try {
        Imf::Header header(width, height);
        header.compression() = Imf::ZIP_COMPRESSION;
        header.channels().insert("R", Imf::Channel(Imf::HALF));
        header.channels().insert("G", Imf::Channel(Imf::HALF));
        header.channels().insert("B", Imf::Channel(Imf::HALF));
        if (withAlpha) header.channels().insert("A", Imf::Channel(Imf::HALF));

        const size_t xStride = sizeof(half) * channels;
        const size_t yStride = xStride * width;
        char* base = reinterpret_cast<char*>(pixels.data());
        Imf::FrameBuffer frameBuffer;
        frameBuffer.insert("R", Imf::Slice(Imf::HALF, base, xStride, yStride));
        frameBuffer.insert("G", Imf::Slice(Imf::HALF, base + sizeof(half), xStride, yStride));
        frameBuffer.insert("B", Imf::Slice(Imf::HALF, base + sizeof(half) * 2, xStride, yStride));
        if (withAlpha) frameBuffer.insert("A", Imf::Slice(Imf::HALF, base + sizeof(half) * 3, xStride, yStride));

        Imf::OutputFile file(WideToUtf8(temp).c_str(), header, Imf::globalThreadCount());
        file.setFrameBuffer(frameBuffer);
        file.writePixels(height);
    } catch (const std::exception&) {
        DeleteFileW(temp.c_str());
        return false;
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}
