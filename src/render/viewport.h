#pragma once
#include <algorithm>
#include <optional>
namespace mtv {
inline constexpr int kLogicalWidth = 320, kLogicalHeight = 200, kArtScale = 6;
enum class AspectMode { Crt4x3, Square };
struct Point { double x = 0, y = 0; };
struct LogicalPoint { int x = 0, y = 0; bool operator==(const LogicalPoint&) const = default; };
struct WindowMetrics { int width = 0, height = 0, pixel_width = 0, pixel_height = 0; };
struct FittedViewport { double x = 0, y = 0, width = 0, height = 0; };
struct Viewport {
    // The 320x200 picture shown at 4:3 (as on a CRT, pixels 1.2x taller than wide) or with square pixels (16:10).
    static FittedViewport fit(int width, int height, AspectMode mode) {
        if (width <= 0 || height <= 0) return {};
        const double aspect = mode == AspectMode::Crt4x3 ? 4.0 / 3.0 : 16.0 / 10.0;
        double vw = width, vh = width / aspect;
        if (vh > height) { vh = height; vw = height * aspect; }
        return {(width - vw) / 2, (height - vh) / 2, vw, vh};
    }
    static std::optional<LogicalPoint> window_to_logical(Point p, WindowMetrics m, AspectMode mode) {
        if (m.width <= 0 || m.height <= 0 || m.pixel_width <= 0 || m.pixel_height <= 0) return std::nullopt;
        auto v = fit(m.pixel_width, m.pixel_height, mode);
        const double x = (p.x * m.pixel_width / m.width - v.x) * kLogicalWidth / v.width;
        const double y = (p.y * m.pixel_height / m.height - v.y) * kLogicalHeight / v.height;
        if (x < 0 || y < 0 || x >= kLogicalWidth || y >= kLogicalHeight) return std::nullopt;
        return LogicalPoint{int(x), int(y)};
    }
};
}
