#include "oklab.h"

#include <cmath>

struct Mat3 {
    double m[3][3];
};

struct Vec3 {
    double x, y, z;
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

static Vec3 Apply(const Mat3& t, const Vec3& v) {
    return {t.m[0][0] * v.x + t.m[0][1] * v.y + t.m[0][2] * v.z,
            t.m[1][0] * v.x + t.m[1][1] * v.y + t.m[1][2] * v.z,
            t.m[2][0] * v.x + t.m[2][1] * v.y + t.m[2][2] * v.z};
}

// sRGB stores an encoded value rather than light, and this curve undoes the encoding.
double SrgbToLinear(double x) {
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}

Lab SrgbToOklab(double r, double g, double b) {
    const Vec3 lms = Apply(LMS_FROM_LINEAR_RGB, {SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b)});
    // cbrt and not pow(x, 1/3): out-of-gamut input makes LMS negative, where pow is NaN.
    const Vec3 lab = Apply(OKLAB_FROM_LMS_PRIME, {std::cbrt(lms.x), std::cbrt(lms.y), std::cbrt(lms.z)});
    return {lab.x, lab.y, lab.z};
}

PreparedImage ToOklab(const Plane& r, const Plane& g, const Plane& b) {
    PreparedImage out{Plane(r.rows, r.cols), Plane(r.rows, r.cols), Plane(r.rows, r.cols)};
    for (std::size_t i = 0; i < r.Size(); ++i) {
        const Lab lab = SrgbToOklab(r.v[i], g.v[i], b.v[i]);
        out.L.v[i] = lab.L;
        out.a.v[i] = lab.a;
        out.b.v[i] = lab.b;
    }
    return out;
}
