#pragma once

#include <string>

#include "plane.h"

class Checker {
public:
    void Close(const std::string& what, double got, double want, double tol);
    void Close(const std::string& what, const Plane& got, const Plane& want, double tol);
    void Require(const std::string& what, bool condition);
    int Report(const char* suite) const;

private:
    void Record(bool good, const std::string& what, const std::string& note);

    int passed_ = 0;
    int failed_ = 0;
};
