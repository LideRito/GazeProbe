#include "filters.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "pixel_utils.h"

// Wider Gaussians run on a shrunk copy, which is off by a few percent at worst but far cheaper.
static const double MAX_DIRECT_SIGMA = 4.0;

static const double WINDOW_FRACTION = 0.02;
static const int MIN_WINDOW = 3;

static int BorderIndex(int i, int n, Border border) {
    if (border == Border::Nearest) return std::clamp(i, 0, n - 1);
    if (n == 1) return 0;
    // Reflect repeats the edge sample: d c b a | a b c d | d c b a
    const int period = 2 * n;
    i %= period;
    if (i < 0) i += period;
    return i < n ? i : period - 1 - i;
}

// Correlates every line along one axis with the kernel; its first tap sits `left` samples before the output.
static Plane FilterAxis(const Plane& in, int axis, const std::vector<double>& kernel, int left,
                        Border border) {
    Plane out(in.rows, in.cols);
    const int n = axis == 0 ? in.rows : in.cols;
    const int lines = axis == 0 ? in.cols : in.rows;
    const int taps = int(kernel.size());
    const int right = taps - 1 - left;

    std::vector<double> line(n + taps - 1);
    for (int l = 0; l < lines; ++l) {
        for (int k = -left; k < n + right; ++k) {
            const int src = BorderIndex(k, n, border);
            line[k + left] = axis == 0 ? in.At(src, l) : in.At(l, src);
        }
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            for (int t = 0; t < taps; ++t) sum += kernel[t] * line[i + t];
            if (axis == 0) out.At(i, l) = sum;
            else out.At(l, i) = sum;
        }
    }
    return out;
}

static Plane ResizeAxis(const Plane& in, int axis, int nOut, Align align) {
    const int nIn = axis == 0 ? in.rows : in.cols;
    Plane out(axis == 0 ? nOut : in.rows, axis == 0 ? in.cols : nOut);
    const double cornerStep = nOut > 1 ? double(nIn - 1) / double(nOut - 1) : 1.0;
    const double centreStep = double(nIn) / nOut;
    const int lines = axis == 0 ? in.cols : in.rows;

    for (int j = 0; j < nOut; ++j) {
        // Corners: first and last samples coincide. Centres: pixel j's centre, j + 0.5, maps to the input's.
        const double coord = align == Align::Corners
                                 ? j * cornerStep
                                 : std::clamp((j + 0.5) * centreStep - 0.5, 0.0, double(nIn - 1));
        const int i0 = std::min(int(std::floor(coord)), nIn - 1);
        const int i1 = std::min(i0 + 1, nIn - 1);
        const double t = coord - i0;
        for (int l = 0; l < lines; ++l) {
            const double a = axis == 0 ? in.At(i0, l) : in.At(l, i0);
            const double b = axis == 0 ? in.At(i1, l) : in.At(l, i1);
            const double v = (1.0 - t) * a + t * b;
            if (axis == 0) out.At(j, l) = v;
            else out.At(l, j) = v;
        }
    }
    return out;
}

int WindowSize(int rows, int cols) {
    int size = RoundHalfEven(std::max(rows, cols) * WINDOW_FRACTION);
    size = std::max(MIN_WINDOW, size);
    return size % 2 == 0 ? size + 1 : size;
}

Plane BoxFilter(const Plane& in, int size, Border border) {
    const std::vector<double> ones(size, 1.0);
    Plane out = FilterAxis(in, 0, ones, size / 2, border);
    for (double& x : out.v) x /= size;
    out = FilterAxis(out, 1, ones, size / 2, border);
    for (double& x : out.v) x /= size;
    return out;
}

Plane GaussianFilter(const Plane& in, double sigma, Border border) {
    const int radius = int(4.0 * sigma + 0.5);
    std::vector<double> kernel(2 * radius + 1);
    double sum = 0.0;
    for (int x = -radius; x <= radius; ++x) {
        const double w = (-0.5 / (sigma * sigma)) * (double(x) * double(x));
        kernel[x + radius] = std::exp(w);
        sum += kernel[x + radius];
    }
    for (double& w : kernel) w /= sum;
    return FilterAxis(FilterAxis(in, 0, kernel, radius, border), 1, kernel, radius, border);
}

Plane ResizeLinear(const Plane& in, int rows, int cols, Align align) {
    return ResizeAxis(ResizeAxis(in, 0, rows, align), 1, cols, align);
}

Plane Blur(const Plane& in, double sigma) {
    if (sigma <= MAX_DIRECT_SIGMA) return GaussianFilter(in, sigma, Border::Nearest);

    const double scale = MAX_DIRECT_SIGMA / sigma;
    // A box pass first, or fine detail aliases into the shrunk copy.
    const int box = std::max(2, RoundHalfEven(1.0 / scale));
    const Plane filtered = BoxFilter(in, box, Border::Nearest);
    const Plane shrunk =
        ResizeLinear(filtered, ScaledSize(in.rows, scale), ScaledSize(in.cols, scale), Align::Corners);
    return ResizeLinear(GaussianFilter(shrunk, MAX_DIRECT_SIGMA, Border::Nearest), in.rows, in.cols,
                        Align::Corners);
}

// Standard deviation as sqrt(E[x^2] - E[x]^2), which leaves ~1e-8 rather than 0 on flat regions.
Plane LocalStd(const Plane& in, int size) {
    Plane sq(in.rows, in.cols);
    for (std::size_t i = 0; i < in.Size(); ++i) sq.v[i] = in.v[i] * in.v[i];
    const Plane mean = BoxFilter(in, size, Border::Reflect);
    const Plane meanSq = BoxFilter(sq, size, Border::Reflect);

    Plane out(in.rows, in.cols);
    for (std::size_t i = 0; i < out.Size(); ++i)
        out.v[i] = std::sqrt(std::max(meanSq.v[i] - mean.v[i] * mean.v[i], 0.0));
    return out;
}
