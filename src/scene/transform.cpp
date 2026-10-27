#include "mesh_verifier/transform.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mesh_verifier {
namespace {

constexpr double kDegree = std::numbers::pi / 180.0;

// Rotation matrix of a unit quaternion, into the 3x3 part of m.
void SetRotation(Mat4& out, const Quat& q) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    out.m[0][0] = 1 - 2 * (y * y + z * z);
    out.m[0][1] = 2 * (x * y - z * w);
    out.m[0][2] = 2 * (x * z + y * w);
    out.m[1][0] = 2 * (x * y + z * w);
    out.m[1][1] = 1 - 2 * (x * x + z * z);
    out.m[1][2] = 2 * (y * z - x * w);
    out.m[2][0] = 2 * (x * z - y * w);
    out.m[2][1] = 2 * (y * z + x * w);
    out.m[2][2] = 1 - 2 * (x * x + y * y);
    // rounding noise around 0 and +-1 (e.g. from sqrt(1/2) in a quarter turn) is removed, so axis-aligned
    // rotations give exactly axis-aligned matrices
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            double& v = out.m[r][c];
            if (std::fabs(v) < 1e-15) v = 0;
            else if (std::fabs(std::fabs(v) - 1) < 1e-15) v = v > 0 ? 1 : -1;
        }
}

// The quaternion of a rotation matrix r[row][col] (Shepperd's method).
Quat QuatFromRotation(const double r[3][3]) {
    Quat q;
    const double trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
        const double s = std::sqrt(trace + 1.0) * 2;
        q.w = 0.25 * s;
        q.x = (r[2][1] - r[1][2]) / s;
        q.y = (r[0][2] - r[2][0]) / s;
        q.z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2;
        q.w = (r[2][1] - r[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2;
        q.w = (r[0][2] - r[2][0]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2;
        q.w = (r[1][0] - r[0][1]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25 * s;
    }
    q = Normalized(q);
    if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
    return q;
}

// sin and cos of an angle in degrees, exact when it's a whole multiple of 45 degrees.
void SinCosDegrees(double degrees, double& s, double& c) {
    const double eighths = degrees / 45.0;
    if (eighths == std::round(eighths) && std::fabs(eighths) < 1e15) {
        const auto k = static_cast<int>(((static_cast<long long>(eighths) % 8) + 8) % 8);
        const double h = std::sqrt(0.5);
        static constexpr int kS[8] = {0, 1, 1, 1, 0, -1, -1, -1}, kC[8] = {1, 1, 0, -1, -1, -1, 0, 1};
        s = k % 2 ? kS[k] * h : kS[k];
        c = k % 2 ? kC[k] * h : kC[k];
        return;
    }
    s = std::sin(degrees * kDegree);
    c = std::cos(degrees * kDegree);
}

double CleanAngle(double degrees) {
    const double r = std::round(degrees * 1e9) / 1e9;
    const double cleaned = std::fabs(r - degrees) < 1e-9 ? r : degrees;
    if (cleaned <= -180.0) return cleaned + 360.0;
    return cleaned == 0 ? 0.0 : cleaned;
}

}  // namespace

double Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }

bool IsFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

Quat operator*(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat Normalized(const Quat& q) {
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 0) || !std::isfinite(n)) return {};
    if (n == 1.0) return q;
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

Quat QuatFromEuler(const Vec3& degrees) {
    double sx, cx, sy, cy, sz, cz;
    SinCosDegrees(degrees.x / 2, sx, cx);
    SinCosDegrees(degrees.y / 2, sy, cy);
    SinCosDegrees(degrees.z / 2, sz, cz);
    const Quat qx{sx, 0, 0, cx}, qy{0, sy, 0, cy}, qz{0, 0, sz, cz};
    return qz * qy * qx;
}

Vec3 EulerFromQuat(const Quat& quat) {
    Mat4 r;
    SetRotation(r, Normalized(quat));
    // R = Rz Ry Rx: r20 = -sin(y), r21 = sin(x) cos(y), r22 = cos(x) cos(y), r10 = cos(y) sin(z), r00 = cos(y) cos(z)
    const double sy = std::clamp(-r.m[2][0], -1.0, 1.0);
    Vec3 e;
    if (std::fabs(sy) < 1 - 1e-12) {
        e.x = std::atan2(r.m[2][1], r.m[2][2]);
        e.y = std::asin(sy);
        e.z = std::atan2(r.m[1][0], r.m[0][0]);
    } else {
        e.x = std::atan2(-r.m[1][2], r.m[1][1]);
        e.y = sy > 0 ? std::numbers::pi / 2 : -std::numbers::pi / 2;
        e.z = 0;
    }
    return {CleanAngle(e.x / kDegree), CleanAngle(e.y / kDegree), CleanAngle(e.z / kDegree)};
}

Mat4 Mat4::FromColumnMajor(const double* v) {
    Mat4 out;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) out.m[r][c] = v[c * 4 + r];
    return out;
}

bool Mat4::IsIdentity() const { return *this == Mat4{}; }

Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[r][k] * b.m[k][c];
            out.m[r][c] = s;
        }
    return out;
}

Vec3 TransformPoint(const Mat4& m, const Vec3& p) {
    return {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3], m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
            m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]};
}

double Determinant3(const Mat4& a) {
    const auto& m = a.m;
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

std::optional<Mat4> InverseAffine(const Mat4& a) {
    const double det = Determinant3(a);
    if (det == 0 || !std::isfinite(det)) return std::nullopt;
    const auto& m = a.m;
    Mat4 out;
    out.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    out.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    out.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    out.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    out.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    out.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    out.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    out.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    out.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    for (int r = 0; r < 3; ++r) out.m[r][3] = -(out.m[r][0] * m[0][3] + out.m[r][1] * m[1][3] + out.m[r][2] * m[2][3]);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(out.m[r][c])) return std::nullopt;
    return out;
}

Mat4 Compose(const TRS& trs) {
    Mat4 out;
    SetRotation(out, trs.rotation);
    const double s[3] = {trs.scale.x, trs.scale.y, trs.scale.z};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out.m[r][c] *= s[c];
    out.m[0][3] = trs.translation.x;
    out.m[1][3] = trs.translation.y;
    out.m[2][3] = trs.translation.z;
    return out;
}

TRS Decompose(const Mat4& a, bool* exact) {
    TRS out;
    out.translation = {a.m[0][3], a.m[1][3], a.m[2][3]};
    Vec3 col[3];
    for (int c = 0; c < 3; ++c) col[c] = {a.m[0][c], a.m[1][c], a.m[2][c]};
    double s[3] = {Length(col[0]), Length(col[1]), Length(col[2])};
    if (Determinant3(a) < 0) s[0] = -s[0];
    out.scale = {s[0], s[1], s[2]};

    // unit axes; a zero-length axis is rebuilt from the others so the rotation stays defined
    Vec3 axis[3];
    int zero = 0;
    for (int c = 0; c < 3; ++c) {
        if (s[c] != 0) axis[c] = col[c] * (1.0 / s[c]);
        else ++zero;
    }
    if (zero == 1) {
        for (int c = 0; c < 3; ++c)
            if (s[c] == 0) {
                const Vec3 v = Cross(axis[(c + 1) % 3], axis[(c + 2) % 3]);
                const double n = Length(v);
                axis[c] = n > 0 ? v * (1.0 / n) : Vec3{c == 0 ? 1.0 : 0.0, c == 1 ? 1.0 : 0.0, c == 2 ? 1.0 : 0.0};
            }
    } else if (zero > 1) {
        axis[0] = {1, 0, 0};
        axis[1] = {0, 1, 0};
        axis[2] = {0, 0, 1};
    }
    bool orthogonal = true;
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (std::fabs(Dot(axis[i], axis[j])) > 1e-9) orthogonal = false;
    if (!orthogonal) {
        // Gram-Schmidt, so the rotation is the nearest one the axes allow
        axis[0] = axis[0] * (1.0 / Length(axis[0]));
        axis[1] = axis[1] - axis[0] * Dot(axis[0], axis[1]);
        const double n1 = Length(axis[1]);
        axis[1] = n1 > 0 ? axis[1] * (1.0 / n1) : Vec3{0, 1, 0};
        axis[2] = Cross(axis[0], axis[1]);
        if (Dot(axis[2], col[2]) < 0 && s[2] > 0) axis[2] = axis[2] * -1.0;
    }
    double r[3][3];
    for (int c = 0; c < 3; ++c) {
        r[0][c] = axis[c].x;
        r[1][c] = axis[c].y;
        r[2][c] = axis[c].z;
    }
    out.rotation = QuatFromRotation(r);
    // a rotation of exactly the identity matrix stays exactly the identity quaternion
    if (r[0][0] == 1 && r[1][1] == 1 && r[2][2] == 1) out.rotation = {};
    if (exact) {
        const Mat4 back = Compose(out);
        double scaleOfMatrix = 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) scaleOfMatrix = std::max(scaleOfMatrix, std::fabs(a.m[i][j]));
        bool same = orthogonal;
        for (int i = 0; i < 3 && same; ++i)
            for (int j = 0; j < 3; ++j)
                if (std::fabs(back.m[i][j] - a.m[i][j]) > 1e-9 * std::max(1.0, scaleOfMatrix)) same = false;
        for (int j = 0; j < 4; ++j)
            if (a.m[3][j] != (j == 3 ? 1.0 : 0.0)) same = false;
        *exact = same;
    }
    return out;
}

}  // namespace mesh_verifier
