#include "mesh_verifier/intersection_verifier.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include "triangle_intersection.h"

namespace mesh_verifier {
namespace {

struct Box {
    Vec3 lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
    void Grow(const Vec3& p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    void Grow(const Box& b) { Grow(b.lo); Grow(b.hi); }
};

// Closed boxes: touching boxes count as overlapping, so no candidate pair is ever missed.
bool Overlap(const Box& a, const Box& b) {
    return a.lo.x <= b.hi.x && b.lo.x <= a.hi.x && a.lo.y <= b.hi.y && b.lo.y <= a.hi.y && a.lo.z <= b.hi.z && b.lo.z <= a.hi.z;
}

struct Item {
    TriangleRef ref;
    Box box;
    Vec3 centre;
};

// A bounding volume hierarchy over every triangle of every mesh, split at the median of the widest axis.
struct Bvh {
    struct Node {
        Box box;
        int left = -1, right = -1;  // children; -1 for a leaf
        int begin = 0, end = 0;     // a leaf's items
    };
    std::vector<Node> nodes;
    std::vector<Item> items;

    int Build(int begin, int end) {
        Node node;
        node.begin = begin;
        node.end = end;
        Box centres;
        for (int i = begin; i < end; ++i) {
            node.box.Grow(items[i].box);
            centres.Grow(items[i].centre);
        }
        const int index = static_cast<int>(nodes.size());
        nodes.push_back(node);
        if (end - begin <= 4) return index;
        const double ex = centres.hi.x - centres.lo.x, ey = centres.hi.y - centres.lo.y, ez = centres.hi.z - centres.lo.z;
        const int axis = ex >= ey && ex >= ez ? 0 : ey >= ez ? 1 : 2;
        const auto key = [axis](const Item& it) { return axis == 0 ? it.centre.x : axis == 1 ? it.centre.y : it.centre.z; };
        const int mid = (begin + end) / 2;
        std::nth_element(items.begin() + begin, items.begin() + mid, items.begin() + end, [&](const Item& p, const Item& q) {
            const double kp = key(p), kq = key(q);
            return kp != kq ? kp < kq : p.ref < q.ref;  // (ties broken by triangle, so the tree is deterministic)
        });
        const int left = Build(begin, mid);
        const int right = Build(mid, end);
        nodes[index].left = left;
        nodes[index].right = right;
        return index;
    }

    // Every pair of items whose boxes overlap, each once.
    template <class F>
    void SelfPairs(int n, F&& visit) const {
        const Node& node = nodes[n];
        if (node.left < 0) {
            for (int i = node.begin; i < node.end; ++i)
                for (int j = i + 1; j < node.end; ++j)
                    if (Overlap(items[i].box, items[j].box)) visit(items[i], items[j]);
            return;
        }
        SelfPairs(node.left, visit);
        SelfPairs(node.right, visit);
        CrossPairs(node.left, node.right, visit);
    }

    template <class F>
    void CrossPairs(int m, int n, F&& visit) const {
        const Node& a = nodes[m];
        const Node& b = nodes[n];
        if (!Overlap(a.box, b.box)) return;
        if (a.left < 0 && b.left < 0) {
            for (int i = a.begin; i < a.end; ++i)
                for (int j = b.begin; j < b.end; ++j)
                    if (Overlap(items[i].box, items[j].box)) visit(items[i], items[j]);
        } else if (b.left < 0 || (a.left >= 0 && a.end - a.begin >= b.end - b.begin)) {
            CrossPairs(a.left, n, visit);
            CrossPairs(a.right, n, visit);
        } else {
            CrossPairs(m, b.left, visit);
            CrossPairs(m, b.right, visit);
        }
    }
};

void Validate(const GeometryData& geometry) {
    for (size_t m = 0; m < geometry.meshes.size(); ++m) {
        const Mesh& mesh = geometry.meshes[m];
        for (size_t v = 0; v < mesh.vertices.size(); ++v) {
            const Vec3& p = mesh.vertices[v];
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
                std::ostringstream e;
                e << "mesh " << m << " ('" << mesh.name << "') vertex " << v << " has a coordinate that is not finite";
                throw ValidationError(e.str());
            }
        }
        for (size_t t = 0; t < mesh.triangles.size(); ++t) {
            const Triangle& tri = mesh.triangles[t];
            for (const uint32_t i : {tri.a, tri.b, tri.c})
                if (i >= mesh.vertices.size()) {
                    std::ostringstream e;
                    e << "mesh " << m << " ('" << mesh.name << "') triangle " << t << " uses vertex " << i << " but the mesh has "
                      << mesh.vertices.size() << " vertices";
                    throw ValidationError(e.str());
                }
        }
    }
}

}  // namespace

VerificationReport VerifyIntersections(const GeometryData& geometry) {
    Validate(geometry);
    Bvh bvh;
    for (size_t m = 0; m < geometry.meshes.size(); ++m) {
        const Mesh& mesh = geometry.meshes[m];
        for (size_t t = 0; t < mesh.triangles.size(); ++t) {
            const Triangle& tri = mesh.triangles[t];
            Item item;
            item.ref = {static_cast<uint32_t>(m), static_cast<uint32_t>(t)};
            for (const uint32_t i : {tri.a, tri.b, tri.c}) item.box.Grow(mesh.vertices[i]);
            item.centre = {(item.box.lo.x + item.box.hi.x) / 2, (item.box.lo.y + item.box.hi.y) / 2, (item.box.lo.z + item.box.hi.z) / 2};
            bvh.items.push_back(item);
        }
    }
    VerificationReport report;
    if (bvh.items.size() < 2) return report;
    const int root = bvh.Build(0, static_cast<int>(bvh.items.size()));

    const auto corners = [&](const TriangleRef& ref, Vec3& p0, Vec3& p1, Vec3& p2) {
        const Mesh& mesh = geometry.meshes[ref.meshIndex];
        const Triangle& tri = mesh.triangles[ref.triangleIndex];
        p0 = mesh.vertices[tri.a];
        p1 = mesh.vertices[tri.b];
        p2 = mesh.vertices[tri.c];
    };
    bvh.SelfPairs(root, [&](const Item& x, const Item& y) {
        // (each pair is tested in one fixed order, so the result never depends on how the tree grouped them)
        const TriangleRef first = x.ref < y.ref ? x.ref : y.ref, second = x.ref < y.ref ? y.ref : x.ref;
        Vec3 a0, a1, a2, b0, b1, b2;
        corners(first, a0, a1, a2);
        corners(second, b0, b1, b2);
        if (const auto hit = detail::IntersectTriangles(a0, a1, a2, b0, b1, b2)) {
            Intersection found;
            found.a = first;
            found.b = second;
            found.type = hit->type;
            found.weight = hit->weight;
            found.segmentStart = hit->segmentStart;
            found.segmentEnd = hit->segmentEnd;
            report.intersections.push_back(found);
        }
    });
    std::sort(report.intersections.begin(), report.intersections.end(), [](const Intersection& p, const Intersection& q) {
        return p.a == q.a ? p.b < q.b : p.a < q.a;
    });
    return report;
}

}  // namespace mesh_verifier
