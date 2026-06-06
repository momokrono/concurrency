/// Mandelbrot set renderer using the concurrency library.
/// Each row is computed in parallel via async_with_future.
///
/// Build:  g++ -std=c++26 -O3 -o mandelbrot examples/mandelbrot.cpp -Iinclude
/// Run:    ./mandelbrot > mandelbrot.pgm
/// View:   eog mandelbrot.pgm  (or any PGM viewer)

#include <concurrency/task_system.hpp>
#include <cmath>
#include <cstdio>
#include <future>
#include <vector>

using concurrency::task_system;

constexpr int WIDTH   = 4096;
constexpr int HEIGHT  = 4096;
constexpr double XMIN = -2.0;
constexpr double XMAX =  1.0;
constexpr double YMIN = -1.5;
constexpr double YMAX =  1.5;
constexpr int MAX_ITER = 500;

struct Color {
    unsigned char r, g, b;
    static auto from_iter(int n) -> Color {
        if (n >= MAX_ITER) return {0, 0, 0};
        double t = std::sqrt(static_cast<double>(n) / MAX_ITER);
        return {
            static_cast<unsigned char>(t * 255),
            static_cast<unsigned char>(t * t * 255),
            static_cast<unsigned char>(t * t * t * 255),
        };
    }
};

auto compute_row(int y) -> std::vector<Color> {
    double cy = YMIN + (YMAX - YMIN) * y / (HEIGHT - 1);
    std::vector<Color> row(WIDTH);
    for (int x = 0; x < WIDTH; ++x) {
        double cx = XMIN + (XMAX - XMIN) * x / (WIDTH - 1);
        double zx = 0, zy = 0;
        int iter = 0;
        while (zx * zx + zy * zy < 4.0 && iter < MAX_ITER) {
            double nx = zx * zx - zy * zy + cx;
            zy = 2.0 * zx * zy + cy;
            zx = nx;
            ++iter;
        }
        row[x] = Color::from_iter(iter);
    }
    return row;
}

int main() {
    task_system ts;  // uses all cores
    std::vector<std::future<std::vector<Color>>> rows(HEIGHT);

    for (int y = 0; y < HEIGHT; ++y) {
        rows[y] = ts.async_with_future([=] { return compute_row(y); });
    }
    ts.sync_point();  // all rows done

    // PGM output
    std::printf("P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (auto& f : rows) {
        for (auto const& c : f.get()) {
            std::putchar(c.r);
            std::putchar(c.g);
            std::putchar(c.b);
        }
    }
}
