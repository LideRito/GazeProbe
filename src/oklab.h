#pragma once

#include "plane.h"

// Oklab is three channels; chroma and hue are just (a, b) in polar form, derived where needed.
struct PreparedImage {
    Plane L, a, b;

    int Rows() const { return L.rows; }
    int Cols() const { return L.cols; }
};

double SrgbToLinear(double x);

void SrgbToOklab(double r, double g, double b, double& outL, double& outA, double& outB);

PreparedImage ToOklab(const Plane& r, const Plane& g, const Plane& b);
