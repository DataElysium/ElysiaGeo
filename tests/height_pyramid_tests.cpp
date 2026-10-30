#include "../src/height_pyramid.hpp"
#include <atomic>
#include <random>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok) {
    if (!ok)
        throw std::runtime_error(
            "Height pyramid differs from source scan or exceeds its fixed storage");
}
} // namespace
void height_pyramid_tests() {
    using elysia::geo::HeightPyramid;
    std::mt19937 rng(20261029);
    for (auto [columns, rows] :
         {std::pair{1u, 1u}, {1u, 65u}, {65u, 1u}, {7u, 5u}, {64u, 64u}, {65u, 49u}}) {
        std::vector<float> source(std::size_t(columns) * rows);
        for (auto &value : source)
            value = float(int(rng() % 20000) - 10000);
        for (bool missing : {false, true}) {
            if (missing)
                source[std::size_t(rows / 2) * columns + columns / 2] = NAN;
            const auto read = [&](auto x, auto y) { return source[std::size_t(y) * columns + x]; };
            HeightPyramid pyramid(columns, rows, read);
            std::size_t expected_bytes = 0;
            for (auto nx = columns, ny = rows; nx > 1 || ny > 1;) {
                nx = nx / 2 + nx % 2;
                ny = ny / 2 + ny % 2;
                expected_bytes += std::size_t(nx) * ny * sizeof(float);
            }
            check(pyramid.bytes() == expected_bytes);
            struct Query {
                unsigned left, top, right, bottom;
                float maximum;
            };
            std::vector<Query> queries;
            for (int i = 0; i < 1500; ++i) {
                unsigned left = rng() % columns, right = rng() % columns;
                unsigned top = rng() % rows, bottom = rng() % rows;
                if (left > right)
                    std::swap(left, right);
                if (top > bottom)
                    std::swap(top, bottom);
                if (i == 0) {
                    left = top = 0;
                    right = columns - 1;
                    bottom = rows - 1;
                }
                float expected = -INFINITY;
                for (auto y = top; y <= bottom; ++y)
                    for (auto x = left; x <= right; ++x)
                        expected =
                            std::max(expected, std::isfinite(read(x, y)) ? read(x, y) : INFINITY);
                queries.push_back({left, top, right, bottom, expected});
            }
            std::atomic<bool> equal = true;
            {
                std::vector<std::jthread> workers;
                for (int i = 0; i < 4; ++i)
                    workers.emplace_back([&] {
                        for (auto q : queries) {
                            if (pyramid.maximum(q.left, q.top, q.right, q.bottom, read) !=
                                q.maximum)
                                equal = false;
                            for (double ceiling : {-5000., 0., 5000., 11000.}) {
                                const auto bound = pyramid.maximum(q.left, q.top, q.right, q.bottom,
                                                                   read, ceiling);
                                if (bound < q.maximum || (q.maximum < ceiling) != (bound < ceiling))
                                    equal = false;
                            }
                        }
                    });
            }
            check(equal && pyramid.bytes() == expected_bytes);
        }
    }
}
