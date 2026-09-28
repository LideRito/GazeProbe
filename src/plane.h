#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

// A rows x cols grid of doubles, one per pixel: a colour channel or any map derived from one.
struct Plane {
    int rows = 0;
    int cols = 0;
    std::vector<double> v;

    Plane() = default;
    Plane(int r, int c, double fill = 0.0) : rows(r), cols(c), v(std::size_t(r) * c, fill) {}

    std::size_t Size() const { return v.size(); }
    bool Empty() const { return v.empty(); }
    bool SameShape(const Plane& o) const { return rows == o.rows && cols == o.cols; }

    double& At(int r, int c) {
        assert(r >= 0 && r < rows && c >= 0 && c < cols);
        return v[std::size_t(r) * cols + c];
    }
    double At(int r, int c) const {
        assert(r >= 0 && r < rows && c >= 0 && c < cols);
        return v[std::size_t(r) * cols + c];
    }
};
