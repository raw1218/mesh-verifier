#include "triangle_intersection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "predicates.h"

namespace mesh_verifier::detail {
namespace {

Vec3 Sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(const Vec3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

// The vertex that is alone on its side of the other triangle's plane: strictly on one side, with the other two
// strictly on the other side or on the plane. `s` are the three vertices' exact sides (-1, 0, +1).
int LoneVertex(const std::array<int, 3>& s) {
    for (int k = 0; k < 3; ++k) {
        const int o1 = s[(k + 1) % 3], o2 = s[(k + 2) % 3];
        if (s[k] != 0 && o1 != s[k] && o2 != s[k]) return k;
    }
    return -1;
}

bool Straddles(const std::array<int, 3>& s) {
    const bool pos = s[0] > 0 || s[1] > 0 || s[2] > 0, neg = s[0] < 0 || s[1] < 0 || s[2] < 0;
    return pos && neg;
}

// The point where edge p -> q crosses the plane with the given normal through `o` (q may lie on the plane).
Vec3 EdgeCrossing(const Vec3& p, const Vec3& q, const Vec3& n, const Vec3& o) {
    const double dp = Dot(n, Sub(p, o)), dq = Dot(n, Sub(q, o));
    if (dp == dq) return q;
    return Add(p, Mul(Sub(q, p), dp / (dp - dq)));
}

// --- the coplanar case: both triangles in one plane, compared in 2D by dropping the normal's largest axis ---

struct P2 {
    double x, y;
};

std::array<P2, 3> Project(const Vec3& a, const Vec3& b, const Vec3& c, int drop) {
    const auto pr = [drop](const Vec3& v) { return drop == 0 ? P2{v.y, v.z} : drop == 1 ? P2{v.z, v.x} : P2{v.x, v.y}; };
    return {pr(a), pr(b), pr(c)};
}

int O2(const P2& a, const P2& b, const P2& c) { return Orient2D(a.x, a.y, b.x, b.y, c.x, c.y); }

// Whether the interiors of two counterclockwise triangles overlap (exact). Two convex polygons' interiors are
// disjoint exactly when some edge of one has the whole other polygon on its outer side or on its line.
bool InteriorsOverlap(const std::array<P2, 3>& t, const std::array<P2, 3>& u) {
    const auto separates = [](const std::array<P2, 3>& p, const std::array<P2, 3>& q) {
        for (int e = 0; e < 3; ++e) {
            const P2& a = p[e];
            const P2& b = p[(e + 1) % 3];
            if (O2(a, b, q[0]) <= 0 && O2(a, b, q[1]) <= 0 && O2(a, b, q[2]) <= 0) return true;
        }
        return false;
    };
    return !separates(t, u) && !separates(u, t);
}

// The area of the overlap of two counterclockwise 2D triangles (Sutherland-Hodgman clipping; floating point).
double OverlapArea(const std::array<P2, 3>& t, const std::array<P2, 3>& u) {
    std::vector<P2> poly(u.begin(), u.end());
    for (int e = 0; e < 3 && !poly.empty(); ++e) {
        const P2 a = t[e], b = t[(e + 1) % 3];
        const auto side = [&](const P2& p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
        std::vector<P2> out;
        for (size_t i = 0; i < poly.size(); ++i) {
            const P2 p = poly[i], q = poly[(i + 1) % poly.size()];
            const double sp = side(p), sq = side(q);
            if (sp >= 0) out.push_back(p);
            if ((sp >= 0) != (sq >= 0)) {
                const double t0 = sp / (sp - sq);
                out.push_back({p.x + (q.x - p.x) * t0, p.y + (q.y - p.y) * t0});
            }
        }
        poly = std::move(out);
    }
    double area2 = 0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const P2 p = poly[i], q = poly[(i + 1) % poly.size()];
        area2 += p.x * q.y - q.x * p.y;
    }
    return std::fabs(area2) / 2;
}

std::optional<PairResult> Coplanar(const Vec3& a0, const Vec3& a1, const Vec3& a2, const Vec3& b0, const Vec3& b1, const Vec3& b2) {
    Vec3 n = Cross(Sub(a1, a0), Sub(a2, a0));
    if (Dot(n, n) == 0) n = Cross(Sub(b1, b0), Sub(b2, b0));
    const double nx = std::fabs(n.x), ny = std::fabs(n.y), nz = std::fabs(n.z);
    if (nx == 0 && ny == 0 && nz == 0) return std::nullopt;  // both degenerate: no area to overlap
    const int drop = nx >= ny && nx >= nz ? 0 : ny >= nz ? 1 : 2;
    std::array<P2, 3> t = Project(a0, a1, a2, drop), u = Project(b0, b1, b2, drop);
    const int ot = O2(t[0], t[1], t[2]), ou = O2(u[0], u[1], u[2]);
    if (ot == 0 || ou == 0) return std::nullopt;  // a zero-area triangle has no area to overlap with
    if (ot < 0) std::swap(t[1], t[2]);
    if (ou < 0) std::swap(u[1], u[2]);
    if (!InteriorsOverlap(t, u)) return std::nullopt;
    // the area in 3D: the projected area over the share of the normal along the dropped axis
    const double along = drop == 0 ? nx : drop == 1 ? ny : nz;
    return PairResult{IntersectionType::CoplanarOverlap, OverlapArea(t, u) * Length(n) / along, {}, {}};
}

}  // namespace

std::optional<PairResult> IntersectTriangles(const Vec3& a0, const Vec3& a1, const Vec3& a2, const Vec3& b0, const Vec3& b1, const Vec3& b2) {
    // The exact side of each triangle's vertices with respect to the other's plane.
    std::array<Vec3, 3> A{a0, a1, a2}, B{b0, b1, b2};
    std::array<int, 3> sA{}, sB{};  // sA: A's vertices against B's plane; sB: B's against A's
    for (int k = 0; k < 3; ++k) sA[k] = Orient3D(B[0], B[1], B[2], A[k]);
    if (sA[0] == sA[1] && sA[1] == sA[2] && sA[0] != 0) return std::nullopt;  // A wholly on one side
    for (int k = 0; k < 3; ++k) sB[k] = Orient3D(A[0], A[1], A[2], B[k]);
    if (sB[0] == sB[1] && sB[1] == sB[2] && sB[0] != 0) return std::nullopt;
    if (sA == std::array<int, 3>{0, 0, 0} && sB == std::array<int, 3>{0, 0, 0}) return Coplanar(a0, a1, a2, b0, b1, b2);
    // Only a triangle with vertices strictly on both sides of the other's plane passes through it; anything less
    // (a vertex or an edge on the plane, the rest on one side) is touching at most.
    if (!Straddles(sA) || !Straddles(sB)) return std::nullopt;

    // Canonical form. Rotate each triangle (keeping its orientation) so its lone vertex comes first: A = (a, b, c)
    // with a alone against B's plane, B = (p, q, r) with p alone against A's plane. Then make both lone vertices
    // lie on the positive side: swapping a triangle's last two vertices flips its normal, and so the sides of
    // the OTHER triangle's vertices.
    const int la = LoneVertex(sA), lb = LoneVertex(sB);
    Vec3 a = A[la], b = A[(la + 1) % 3], c = A[(la + 2) % 3];
    Vec3 p = B[lb], q = B[(lb + 1) % 3], r = B[(lb + 2) % 3];
    if (sB[lb] < 0) std::swap(b, c);  // flips A's normal: p moves to A's positive side
    if (sA[la] < 0) std::swap(q, r);  // flips B's normal: a moves to B's positive side
    // Now A meets B's plane along [j, i] (i on edge ab, j on edge ac) and B meets A's plane along [k, l] (k on pq,
    // l on pr), both listed in the order of the line's direction (normal of A) x (normal of B). They overlap over a
    // nonzero length exactly when k comes before i and j before l, which are these two exact signs (a zero means
    // the two segments only meet at an end: touching).
    if (!(Orient3D(a, b, p, q) < 0 && Orient3D(a, c, p, r) > 0)) return std::nullopt;

    // The intersection segment, in floating point: [max(j, k), min(i, l)] along the line.
    const Vec3 nA = Cross(Sub(b, a), Sub(c, a)), nB = Cross(Sub(q, p), Sub(r, p)), dir = Cross(nA, nB);
    const Vec3 i = EdgeCrossing(a, b, nB, p), j = EdgeCrossing(a, c, nB, p);
    const Vec3 k = EdgeCrossing(p, q, nA, a), l = EdgeCrossing(p, r, nA, a);
    const Vec3 start = Dot(j, dir) >= Dot(k, dir) ? j : k;
    const Vec3 end = Dot(i, dir) <= Dot(l, dir) ? i : l;
    return PairResult{IntersectionType::Crossing, Length(Sub(end, start)), start, end};
}

}  // namespace mesh_verifier::detail
