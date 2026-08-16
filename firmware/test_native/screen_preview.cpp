#include "screen_preview.h"

#include "screen_ui.h"

#include <array>
#include <cstdint>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
constexpr int kPreviewScale = 5;
constexpr wchar_t kWindowClass[] = L"AmenMiniScreenPreview";

struct WindowData {
    std::array<std::uint32_t, ScreenUi::kWidth * ScreenUi::kHeight> pixels{};
};

LRESULT CALLBACK previewWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* data = reinterpret_cast<WindowData*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = ScreenUi::kWidth;
            info.bmiHeader.biHeight = -ScreenUi::kHeight;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            if (data != nullptr) {
                StretchDIBits(device, 0, 0, client.right, client.bottom,
                              0, 0, ScreenUi::kWidth, ScreenUi::kHeight,
                              data->pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}
}  // namespace

struct ScreenPreview::Impl {
    HWND window = nullptr;
    WindowData data;
};

ScreenPreview::ScreenPreview() : impl_(new Impl) {}

ScreenPreview::~ScreenPreview() {
    if (impl_->window != nullptr && IsWindow(impl_->window)) {
        DestroyWindow(impl_->window);
    }
    delete impl_;
}

bool ScreenPreview::open() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = previewWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&windowClass);

    RECT bounds{0, 0, ScreenUi::kWidth * kPreviewScale,
                ScreenUi::kHeight * kPreviewScale};
    AdjustWindowRect(&bounds, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    impl_->window = CreateWindowExW(
        0, kWindowClass, L"AMEN_MINI OLED 128x32",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, &impl_->data);
    if (impl_->window == nullptr) {
        return false;
    }
    ShowWindow(impl_->window, SW_SHOW);
    return true;
}

bool ScreenPreview::pumpEvents() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            impl_->window = nullptr;
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

void ScreenPreview::draw(const ScreenUi& screen) {
    for (int y = 0; y < ScreenUi::kHeight; ++y) {
        for (int x = 0; x < ScreenUi::kWidth; ++x) {
            impl_->data.pixels[y * ScreenUi::kWidth + x] =
                screen.pixel(x, y) ? 0x00FFFFFFU : 0x00000000U;
        }
    }
    if (impl_->window != nullptr) {
        InvalidateRect(impl_->window, nullptr, FALSE);
    }
}

#else
struct ScreenPreview::Impl {};

ScreenPreview::ScreenPreview() : impl_(new Impl) {}
ScreenPreview::~ScreenPreview() { delete impl_; }

bool ScreenPreview::open() {
    std::fprintf(stderr, "apercu OLED graphique disponible sous Windows uniquement\n");
    return true;
}

bool ScreenPreview::pumpEvents() {
    return true;
}

void ScreenPreview::draw(const ScreenUi&) {}
#endif
