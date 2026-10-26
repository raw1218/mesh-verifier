#include "predicates.h"

#include <cmath>
#include <vector>

// Exact arithmetic with floating-point expansions (Shewchuk 1997): a number is held as a sum of doubles that
// don't overlap, smallest first; sums and products of such sums are computed with no rounding error at all.
// This needs IEEE double arithmetic without extended precision or fused contractions (see CMakeLists.txt:
// -ffp-contract=off); the one deliberate fused multiply-add is std::fma in TwoProduct.

namespace mesh_verifier::detail {
namespace {

using Expansion = std::vector<double>;

inline void TwoSum(double a, double b, double& x, double& y) {
    x = a + b;
    const double bv = x - a, av = x - bv, br = b - bv, ar = a - av;
    y = ar + br;
}

inline void TwoProduct(double a, double b, double& x, double& y) {
    x = a * b;
    y = std::fma(a, b, -x);
}

// The exact difference a - b as an expansion.
Expansion Diff(double a, double b) {
    double x, y;
    TwoSum(a, -b, x, y);
    if (y == 0.0) return {x};
    return {y, x};
}

// e + b (Shewchuk's GROW-EXPANSION, zero components dropped).
Expansion Grow(const Expansion& e, double b) {
    Expansion h;
    h.reserve(e.size() + 1);
    double q = b;
    for (const double ei : e) {
        double hh;
        TwoSum(q, ei, q, hh);
        if (hh != 0.0) h.push_back(hh);
    }
    if (q != 0.0 || h.empty()) h.push_back(q);
    return h;
}

Expansion Add(const Expansion& e, const Expansion& f) {
    Expansion h = e;
    for (const double fi : f) h = Grow(h, fi);
    return h;
}

Expansion Negate(Expansion e) {
    for (double& x : e) x = -x;
    return e;
}

// e * b (Shewchuk's SCALE-EXPANSION, zero components dropped).
Expansion Scale(const Expansion& e, double b) {
    Expansion h;
    if (e.empty()) return h;
    h.reserve(2 * e.size());
    double q, hh;
    TwoProduct(e[0], b, q, hh);
    if (hh != 0.0) h.push_back(hh);
    for (size_t i = 1; i < e.size(); ++i) {
        double p1, p0, sum;
        TwoProduct(e[i], b, p1, p0);
        TwoSum(q, p0, sum, hh);
        if (hh != 0.0) h.push_back(hh);
        TwoSum(p1, sum, q, hh);
        if (hh != 0.0) h.push_back(hh);
    }
    if (q != 0.0 || h.empty()) h.push_back(q);
    return h;
}

Expansion Mul(const Expansion& e, const Expansion& f) {
    Expansion h{0.0};
    for (const double fi : f) h = Add(h, Scale(e, fi));
    return h;
}

// The sign of an expansion is the sign of its largest (last) nonzero component.
int Sign(const Expansion& e) {
    for (auto it = e.rbegin(); it != e.rend(); ++it)
        if (*it != 0.0) return *it > 0.0 ? 1 : -1;
    return 0;
}

int SignOf(double v) { return v > 0.0 ? 1 : v < 0.0 ? -1 : 0; }

// Shewchuk's error bounds for the first, all-floating-point stage: (3 + 16e)e and (7 + 56e)e, e = 2^-53.
constexpr double kEps = 1.1102230246251565e-16;
constexpr double kOrient2DBound = (3.0 + 16.0 * kEps) * kEps;
constexpr double kOrient3DBound = (7.0 + 56.0 * kEps) * kEps;

}  // namespace

int Orient2D(double ax, double ay, double bx, double by, double cx, double cy) {
    const double l = (bx - ax) * (cy - ay), r = (by - ay) * (cx - ax), det = l - r;
    const double bound = kOrient2DBound * (std::fabs(l) + std::fabs(r));
    if (det > bound || -det > bound) return SignOf(det);
    const Expansion d = Add(Mul(Diff(bx, ax), Diff(cy, ay)), Negate(Mul(Diff(by, ay), Diff(cx, ax))));
    return Sign(d);
}

int Orient3D(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    // det [u; v; w] with u = b - a, v = c - a, w = d - a, which is (u x v) . w
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const double wx = d.x - a.x, wy = d.y - a.y, wz = d.z - a.z;
    const double m1 = vy * wz, m2 = vz * wy, m3 = vz * wx, m4 = vx * wz, m5 = vx * wy, m6 = vy * wx;
    const double det = ux * (m1 - m2) + uy * (m3 - m4) + uz * (m5 - m6);
    const double permanent = std::fabs(ux) * (std::fabs(m1) + std::fabs(m2)) + std::fabs(uy) * (std::fabs(m3) + std::fabs(m4)) +
                             std::fabs(uz) * (std::fabs(m5) + std::fabs(m6));
    const double bound = kOrient3DBound * permanent;
    if (det > bound || -det > bound) return SignOf(det);

    const Expansion Ux = Diff(b.x, a.x), Uy = Diff(b.y, a.y), Uz = Diff(b.z, a.z);
    const Expansion Vx = Diff(c.x, a.x), Vy = Diff(c.y, a.y), Vz = Diff(c.z, a.z);
    const Expansion Wx = Diff(d.x, a.x), Wy = Diff(d.y, a.y), Wz = Diff(d.z, a.z);
    const Expansion cx = Add(Mul(Vy, Wz), Negate(Mul(Vz, Wy)));
    const Expansion cy = Add(Mul(Vz, Wx), Negate(Mul(Vx, Wz)));
    const Expansion cz = Add(Mul(Vx, Wy), Negate(Mul(Vy, Wx)));
    return Sign(Add(Add(Mul(Ux, cx), Mul(Uy, cy)), Mul(Uz, cz)));
}

}  // namespace mesh_verifier::detail
