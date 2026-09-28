#include "oklab.h"

#include <cmath>

struct Mat3 {
    double m[3][3];
};

// Oklab is defined through the LMS cone responses: linear RGB -> LMS, cube root, LMS' -> Lab.
static const Mat3 LMS_FROM_LINEAR_RGB = {{
    {0.4122214708, 0.5363325363, 0.0514459929},
    {0.2119034982, 0.6806995451, 0.1073969566},
    {0.0883024619, 0.2817188376, 0.6299787005},
}};

static const Mat3 OKLAB_FROM_LMS_PRIME = {{
    {0.2104542553, 0.7936177850, -0.0040720468},
    {1.9779984951, -2.4285922050, 0.4505937099},
    {0.0259040371, 0.7827717662, -0.8086757660},
}};

static Mat3 Inverse(const Mat3& s) {
    const double det =
        s.m[0][0] * (s.m[1][1] * s.m[2][2] - s.m[1][2] * s.m[2][1]) -
        s.m[0][1] * (s.m[1][0] * s.m[2][2] - s.m[1][2] * s.m[2][0]) +
        s.m[0][2] * (s.m[1][0] * s.m[2][1] - s.m[1][1] * s.m[2][0]);
    Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const int i1 = (i + 1) % 3, i2 = (i + 2) % 3;
            const int j1 = (j + 1) % 3, j2 = (j + 2) % 3;
            out.m[i][j] = (s.m[j1][i1] * s.m[j2][i2] - s.m[j1][i2] * s.m[j2][i1]) / det;
        }
    }
    return out;
}

static const Mat3 LMS_PRIME_FROM_OKLAB = Inverse(OKLAB_FROM_LMS_PRIME);
static const Mat3 LINEAR_RGB_FROM_LMS = Inverse(LMS_FROM_LINEAR_RGB);

static void Apply(const Mat3& t, double x, double y, double z,
                  double& ox, double& oy, double& oz) {
    ox = t.m[0][0] * x + t.m[0][1] * y + t.m[0][2] * z;
    oy = t.m[1][0] * x + t.m[1][1] * y + t.m[1][2] * z;
    oz = t.m[2][0] * x + t.m[2][1] * y + t.m[2][2] * z;
}

// sRGB stores an encoded value rather than light, and this curve undoes the encoding.
double SrgbToLinear(double x) {
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}

double LinearToSrgb(double x) {
    return x <= 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}

void SrgbToOklab(double r, double g, double b,
                   double& outL, double& outA, double& outB) {
    double l, m, s;
    Apply(LMS_FROM_LINEAR_RGB, SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b),
          l, m, s);
    // cbrt and not pow(x, 1/3): out-of-gamut input makes LMS negative, where pow is NaN.
    Apply(OKLAB_FROM_LMS_PRIME, std::cbrt(l), std::cbrt(m), std::cbrt(s),
          outL, outA, outB);
}

void OklabToSrgb(double L, double a, double b,
                   double& outR, double& outG, double& outB) {
    double l, m, s;
    Apply(LMS_PRIME_FROM_OKLAB, L, a, b, l, m, s);
    double lr, lg, lb;
    Apply(LINEAR_RGB_FROM_LMS, l * l * l, m * m * m, s * s * s, lr, lg, lb);
    outR = LinearToSrgb(lr);
    outG = LinearToSrgb(lg);
    outB = LinearToSrgb(lb);
}

PreparedImage ToOklab(const Plane& r, const Plane& g, const Plane& b) {
    PreparedImage out;
    out.L = Plane(r.rows, r.cols);
    out.a = Plane(r.rows, r.cols);
    out.b = Plane(r.rows, r.cols);
    for (std::size_t i = 0; i < r.Size(); ++i) {
        double L, a, bb;
        SrgbToOklab(r.v[i], g.v[i], b.v[i], L, a, bb);
        out.L.v[i] = L;
        out.a.v[i] = a;
        out.b.v[i] = bb;
    }
    return out;
}
