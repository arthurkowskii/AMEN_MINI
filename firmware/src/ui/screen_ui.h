#ifndef SCREEN_UI_H
#define SCREEN_UI_H

#include <array>
#include <cstddef>
#include <cstdint>

enum class PlaybackMode {
    OneShot,
    Loop,
    Granular,
    SliceSync,
};

class ScreenUi {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 32;
    static constexpr int kBufferSize = kWidth * kHeight / 8;
    static constexpr std::uint64_t kOverlayDurationMs = 1000;

    using Buffer = std::array<std::uint8_t, kBufferSize>;

    struct BrowserLine {
        const char* name;
        bool directory;
    };

    void setPerformance(const char* breakName, int bpm, PlaybackMode mode);
    void showParameter(const char* name, int value, int minimum, int maximum,
                       std::uint64_t nowMs, const char* suffix = nullptr);
    void showBrowser(const char* folderName, const BrowserLine* lines,
                     std::size_t count, std::size_t selectedIndex);
    void showPerformance();
    void render(std::uint64_t nowMs);

    const Buffer& buffer() const;
    bool pixel(int x, int y) const;

private:
    void clear();
    void setPixel(int x, int y, bool on = true);
    void drawHorizontalLine(int x, int y, int width);
    void drawVerticalLine(int x, int y, int height);
    void drawRect(int x, int y, int width, int height);
    void fillRect(int x, int y, int width, int height);
    void drawChar(int x, int y, char character, int scale = 1);
    void drawText(int x, int y, const char* text, int scale = 1);
    void drawNumberRight(int right, int y, int value, int scale,
                         const char* suffix = nullptr);
    void drawPerformance();
    void drawParameter();
    void drawBrowser();

    struct StoredBrowserLine {
        std::array<char, 31> name{};
        bool directory = false;
    };

    Buffer buffer_{};
    std::array<char, 22> breakName_{};
    std::array<char, 17> parameterName_{};
    std::array<char, 4> parameterSuffix_{};
    std::array<char, 32> browserFolder_{};
    std::array<StoredBrowserLine, 3> browserLines_{};
    int bpm_ = 120;
    PlaybackMode mode_ = PlaybackMode::OneShot;
    int parameterValue_ = 0;
    int parameterMinimum_ = 0;
    int parameterMaximum_ = 10;
    std::uint64_t overlayUntilMs_ = 0;
    std::size_t browserLineCount_ = 0;
    std::size_t browserSelectedLine_ = 0;
    bool browserActive_ = false;
};

#endif
