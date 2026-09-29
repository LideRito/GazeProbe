#pragma once

#include "plane.h"

enum class Border { Reflect, Nearest };
enum class Align { Corners, Centres };

int WindowSize(int rows, int cols);

Plane BoxFilter(const Plane& in, int size, Border border);
Plane GaussianFilter(const Plane& in, double sigma, Border border);
Plane ResizeLinear(const Plane& in, int rows, int cols, Align align);
Plane Blur(const Plane& in, double sigma);

Plane LocalStd(const Plane& in, int size);
