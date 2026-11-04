#include "image_codec.h"
#include "com_ptr.h"
#include "image_logic.h"
#include <wincodec.h>
#include <objbase.h>
namespace cronq {
namespace image_codec {
namespace {
bool Ok(HRESULT hr, const char* api) {
    if (FAILED(hr)) {
        LogErr("IMAGE", "%s hr=0x%08lX", api, hr);
        return false;
    }
    return true;
}
}  // namespace
bool EncodePng(int w, int h, const uint8_t* bgra, std::vector<uint8_t>* png) {
    size_t bytes = 0;
    if (!png || !bgra || !image_logic::PixelBytes(w, h, &bytes)) return false;
    png->clear();
    ComPtr<IWICImagingFactory> factory;
    if (!Ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             __uuidof(IWICImagingFactory),
                             reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf())),
            "WIC factory"))
        return false;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converted;
    if (!Ok(CreateStreamOnHGlobal(nullptr, TRUE, stream.ReleaseAndGetAddressOf()), "memory stream") ||
        !Ok(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.ReleaseAndGetAddressOf()),
            "PNG encoder") ||
        !Ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "encoder Initialize") ||
        !Ok(encoder->CreateNewFrame(frame.ReleaseAndGetAddressOf(), properties.ReleaseAndGetAddressOf()),
            "CreateNewFrame") ||
        !Ok(frame->Initialize(properties.Get()), "frame Initialize") ||
        !Ok(frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h)), "SetSize"))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (!Ok(frame->SetPixelFormat(&format), "SetPixelFormat") ||
        !Ok(factory->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, static_cast<UINT>(w) * 4,
                                            static_cast<UINT>(bytes), const_cast<BYTE*>(bgra),
                                            bitmap.ReleaseAndGetAddressOf()),
            "CreateBitmapFromMemory"))
        return false;
    IWICBitmapSource* source = bitmap.Get();
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        if (!Ok(factory->CreateFormatConverter(converted.ReleaseAndGetAddressOf()),
                "CreateFormatConverter") ||
            !Ok(converted->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                      WICBitmapPaletteTypeCustom),
                "format conversion"))
            return false;
        source = converted.Get();
    }
    if (!Ok(frame->WriteSource(source, nullptr), "WriteSource") || !Ok(frame->Commit(), "frame Commit") ||
        !Ok(encoder->Commit(), "encoder Commit"))
        return false;
    STATSTG info{};
    if (!Ok(stream->Stat(&info, STATFLAG_NONAME), "stream Stat") || info.cbSize.QuadPart == 0 ||
        info.cbSize.QuadPart > image_logic::kMaxImageBytes)
        return false;
    LARGE_INTEGER start{};
    ULONG read = 0;
    png->resize(static_cast<size_t>(info.cbSize.QuadPart));
    if (!Ok(stream->Seek(start, STREAM_SEEK_SET, nullptr), "stream Seek") ||
        !Ok(stream->Read(png->data(), static_cast<ULONG>(png->size()), &read), "stream Read") ||
        read != png->size()) {
        png->clear();
        return false;
    }
    return true;
}
}  // namespace image_codec
}  // namespace cronq
