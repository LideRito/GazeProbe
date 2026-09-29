#pragma once

#include "plane.h"

// Oklab is three channels; chroma and hue are just (a, b) in polar form, derived where needed.
struct PreparedImage {
    Plane L, a, b;

    int Rows() const { return L.rows; }
    int Cols() const { return L.cols; }
};

// One colour in Oklab: lightness L, then a (green to red) and b (blue to yellow).
struct Lab {
    double L, a, b;
};

double SrgbToLinear(double x);
Lab SrgbToOklab(double r, double g, double b);

PreparedImage ToOklab(const Plane& r, const Plane& g, const Plane& b);
