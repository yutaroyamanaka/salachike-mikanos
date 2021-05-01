#pragma once
#include <vector>
#include <optional>
#include "graphics.hpp"
#include "frame_buffer.hpp"


class Window {
  public:
    class WindowWriter: public PixelWriter {
      public:
        WindowWriter(Window& window) : window_{window} {}
        virtual void Write(Vector2D<int> pos, const PixelColor& c) override {
          window_.Write(pos, c);
        }

        virtual int Width() const override { return window_.Width(); }
        virtual int Height() const override { return window_.Height(); }
     private:
        Window& window_;
    };

    Window(int width, int height, PixelFormat shadow_format);
    ~Window() = default;
    Window(const Window& rhs) = delete;
    Window& operator = (const Window& rhs) = delete;

    void DrawTo(FrameBuffer& dst, Vector2D<int> pos, const Rectangle<int>& area);
    void SetTransparentColor(std::optional<PixelColor> c);
    WindowWriter* Writer();

    const PixelColor& At(Vector2D<int> pos) const;
    void Write(Vector2D<int> pos, PixelColor c);

    int Width() const;
    int Height() const;
    Vector2D<int> Size() const;

    void Move(Vector2D<int> det_pos, const Rectangle<int>& src);
    
 private:
    int width_, height_;
    std::vector<std::vector<PixelColor>> data_{};
    WindowWriter writer_{*this};
    std::optional<PixelColor> transparent_color_{std::nullopt};

    FrameBuffer shadow_buffer_{};
};

void DrawWindow(PixelWriter& writer, const char* title);
void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size);
