#include "image_codec.h"
#include "com_ptr.h"
#include <wincodec.h>
#include <cstdio>
#include <cstring>
using namespace cronq;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            std::printf("FAIL %d: %s\n", __LINE__, #x); \
            return 1;                                   \
        }                                               \
    } while (0)
int Run() {
    const uint8_t pixels[] = {0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 123, 45, 67, 255};
    std::vector<uint8_t> png;
    CHECK(!image_codec::EncodePng(0, 2, pixels, &png));
    CHECK(image_codec::EncodePng(2, 2, pixels, &png));
    CHECK(png.size() > 8 && std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     __uuidof(IWICImagingFactory),
                                     reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()))));
    CHECK(SUCCEEDED(factory->CreateStream(stream.ReleaseAndGetAddressOf())));
    CHECK(SUCCEEDED(stream->InitializeFromMemory(png.data(), static_cast<DWORD>(png.size()))));
    CHECK(SUCCEEDED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                                     decoder.ReleaseAndGetAddressOf())));
    CHECK(SUCCEEDED(decoder->GetFrame(0, frame.ReleaseAndGetAddressOf())));
    UINT width = 0, height = 0;
    CHECK(SUCCEEDED(frame->GetSize(&width, &height)) && width == 2 && height == 2);
    CHECK(SUCCEEDED(factory->CreateFormatConverter(converter.ReleaseAndGetAddressOf())));
    CHECK(SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                          nullptr, 0, WICBitmapPaletteTypeCustom)));
    uint8_t decoded[sizeof(pixels)]{};
    CHECK(SUCCEEDED(converter->CopyPixels(nullptr, 8, sizeof(decoded), decoded)));
    CHECK(std::memcmp(decoded, pixels, sizeof(pixels)) == 0);
    std::puts("image_codec_test: all checks passed");
    return 0;
}
int main() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    int result = Run();
    CoUninitialize();
    return result;
}
