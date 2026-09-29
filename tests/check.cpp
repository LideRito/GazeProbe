#include "check.h"

#include <cmath>
#include <cstdio>

void Checker::Close(const std::string& what, double got, double want, double tol) {
    const double diff = std::fabs(got - want);
    char note[160];
    std::snprintf(note, sizeof note, "got %.17g want %.17g (diff %.3g)", got, want, diff);
    Record(diff <= tol, what, note);
}

// Reports the worst pixel, which is what you want when a filter is slightly off everywhere.
void Checker::Close(const std::string& what, const Plane& got, const Plane& want, double tol) {
    if (got.rows != want.rows || got.cols != want.cols) {
        Record(false, what, "shape mismatch");
        return;
    }
    double worst = 0.0;
    std::size_t at = 0;
    for (std::size_t i = 0; i < got.Size(); ++i) {
        const double diff = std::fabs(got.v[i] - want.v[i]);
        if (diff > worst) {
            worst = diff;
            at = i;
        }
    }
    char note[200];
    std::snprintf(note, sizeof note, "max |diff| %.3g at (%d,%d): %.17g vs %.17g",
                  worst, int(at / got.cols), int(at % got.cols), got.v[at], want.v[at]);
    Record(worst <= tol, what, note);
}

void Checker::Require(const std::string& what, bool condition) {
    Record(condition, what, condition ? "" : "false");
}

int Checker::Report(const char* suite) const {
    std::printf("%s: %d passed, %d failed\n", suite, passed_, failed_);
    return failed_ == 0 ? 0 : 1;
}

void Checker::Record(bool good, const std::string& what, const std::string& note) {
    std::printf("  %-5s %-34s %s\n", good ? "ok" : "FAIL", what.c_str(), note.c_str());
    if (good) ++passed_;
    else ++failed_;
}
