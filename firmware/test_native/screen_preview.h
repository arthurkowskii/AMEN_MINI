#ifndef SCREEN_PREVIEW_H
#define SCREEN_PREVIEW_H

class ScreenUi;

class ScreenPreview {
public:
    ScreenPreview();
    ~ScreenPreview();

    ScreenPreview(const ScreenPreview&) = delete;
    ScreenPreview& operator=(const ScreenPreview&) = delete;

    bool open();
    bool pumpEvents();
    void draw(const ScreenUi& screen);

private:
    struct Impl;
    Impl* impl_;
};

#endif
