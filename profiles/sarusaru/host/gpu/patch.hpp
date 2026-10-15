#pragma once

#include "ge_state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Bezier and spline surfaces (GE commands 0x05 and 0x06). The GE evaluates a
// grid of control points into triangles itself; here the same surface is
// tessellated on the CPU into an indexed draw the renderer already handles.
// The tunnel of the warp into a stage is such a surface.
namespace sarusaru::gpu {

struct PatchSettings {
    bool spline{};
    std::uint32_t u_count{}, v_count{}; // control points along u (the faster index) and v
    // Spline edge types (0x06 bits 16..17 and 18..19): bit 0 set makes the
    // curve start at its first control point, bit 1 end at its last;
    // otherwise the knots run on uniformly past that end.
    std::uint32_t u_type{}, v_type{};
    std::uint32_t u_divisions{1u}, v_divisions{1u}; // PSUB: per Bezier patch or spline segment
    std::uint32_t primitive{};                      // PPRIM: 0 triangles, 1 lines, 2 points
    bool reverse{};                                 // PFACE bit 0: the surface faces the other way
    bool has_texcoords{}, has_colors{}, has_normals{};
    bool compute_normals{}; // lit: normals come from the surface, not the control points
};

namespace patch_detail {

// The four control points along one axis that shape a sample, with their
// weights and the weights' derivatives along the axis.
struct AxisSample {
    std::uint32_t first{};
    std::array<float, 4> weight{}, slope{};
    float parameter{}; // a generated texture coordinate
};

inline void bezier_axis(std::uint32_t count, std::uint32_t divisions, std::vector<AxisSample> &out) {
    const std::uint32_t patches = (count - 1u) / 3u;
    const std::uint32_t samples = patches * divisions + 1u;
    out.resize(samples);
    for (std::uint32_t k = 0; k < samples; ++k) {
        const std::uint32_t patch = std::min(k / divisions, patches - 1u);
        const float t = static_cast<float>(k - patch * divisions) / static_cast<float>(divisions);
        const float s = 1.0f - t;
        AxisSample &sample = out[k];
        sample.first = patch * 3u;
        sample.weight = {s * s * s, 3.0f * t * s * s, 3.0f * t * t * s, t * t * t};
        sample.slope = {-3.0f * s * s, 3.0f * s * s - 6.0f * t * s, 6.0f * t * s - 3.0f * t * t, 3.0f * t * t};
        sample.parameter = static_cast<float>(patch) + t;
    }
}

// A cubic B-spline over `count` control points: knots[j + 3] = j inside, and
// at each end either three more uniform knots or three repeated ones.
inline void spline_axis(std::uint32_t count, std::uint32_t type, std::uint32_t divisions, std::vector<AxisSample> &out) {
    const std::uint32_t n = count;
    std::vector<float> knots(n + 4u);
    for (std::uint32_t j = 0; j + 3u <= n; ++j) knots[j + 3u] = static_cast<float>(j);
    for (std::uint32_t j = 0; j < 3u; ++j) {
        knots[j] = (type & 1u) != 0u ? 0.0f : static_cast<float>(j) - 3.0f;
        knots[n + 1u + j] = (type & 2u) != 0u ? static_cast<float>(n - 3u) : static_cast<float>(n - 2u + j);
    }
    const std::uint32_t segments = n - 3u;
    const std::uint32_t samples = segments * divisions + 1u;
    out.resize(samples);
    for (std::uint32_t k = 0; k < samples; ++k) {
        const float u = static_cast<float>(k) / static_cast<float>(divisions);
        // The knot span [knots[i], knots[i + 1]) holding u; the last sample
        // stays in the last span.
        const std::uint32_t span = std::min(3u + k / divisions, n - 1u);
        // Cox-de Boor (The NURBS Book, A2.2), keeping the degree 2 functions
        // for the derivative.
        std::array<float, 4> basis{1.0f, 0.0f, 0.0f, 0.0f}, quadratic{};
        std::array<float, 4> left{}, right{};
        for (std::uint32_t j = 1; j <= 3u; ++j) {
            left[j] = u - knots[span + 1u - j];
            right[j] = knots[span + j] - u;
            float saved = 0.0f;
            for (std::uint32_t r = 0; r < j; ++r) {
                const float denominator = right[r + 1u] + left[j - r];
                const float temp = denominator != 0.0f ? basis[r] / denominator : 0.0f;
                basis[r] = saved + right[r + 1u] * temp;
                saved = left[j - r] * temp;
            }
            basis[j] = saved;
            if (j == 2u) quadratic = basis;
        }
        // basis[r] is N(span - 3 + r, 3); quadratic[r] is N(span - 2 + r, 2).
        AxisSample &sample = out[k];
        sample.first = span - 3u;
        sample.weight = basis;
        for (std::uint32_t r = 0; r < 4u; ++r) {
            const std::uint32_t j = span - 3u + r;
            const float a = r >= 1u ? quadratic[r - 1u] : 0.0f; // N(j, 2)
            const float b = r <= 2u ? quadratic[r] : 0.0f;      // N(j + 1, 2)
            const float da = knots[j + 3u] - knots[j], db = knots[j + 4u] - knots[j + 1u];
            sample.slope[r] = 3.0f * ((da != 0.0f ? a / da : 0.0f) - (db != 0.0f ? b / db : 0.0f));
        }
        sample.parameter = u;
    }
}

inline std::array<float, 4> unpack(std::uint32_t color) {
    return {static_cast<float>(color & 0xFFu), static_cast<float>((color >> 8u) & 0xFFu),
            static_cast<float>((color >> 16u) & 0xFFu), static_cast<float>(color >> 24u)};
}

} // namespace patch_detail

// Tessellates `control` (u_count * v_count points, u varying fastest) into
// vertices and an index list of `primitive`. False, leaving nothing to draw,
// for a malformed patch.
inline bool tessellate_patch(const PatchSettings &settings, std::span<const Vertex> control, std::vector<Vertex> &vertices,
                             std::vector<std::uint16_t> &indices, PrimitiveType &primitive) {
    using namespace patch_detail;
    vertices.clear();
    indices.clear();
    if (settings.u_count < 4u || settings.v_count < 4u ||
        control.size() < static_cast<std::size_t>(settings.u_count) * settings.v_count)
        return false;
    // Segments along each axis; the divisions shrink until the grid fits the
    // renderer's 16-bit indices.
    const std::uint32_t u_segments = settings.spline ? settings.u_count - 3u : (settings.u_count - 1u) / 3u;
    const std::uint32_t v_segments = settings.spline ? settings.v_count - 3u : (settings.v_count - 1u) / 3u;
    std::uint32_t u_divisions = std::clamp(settings.u_divisions, 1u, 64u);
    std::uint32_t v_divisions = std::clamp(settings.v_divisions, 1u, 64u);
    while ((u_segments * u_divisions + 1u) * (v_segments * v_divisions + 1u) > 65536u) {
        if (u_divisions >= v_divisions && u_divisions > 1u) --u_divisions;
        else if (v_divisions > 1u) --v_divisions;
        else return false;
    }
    std::vector<AxisSample> u_axis, v_axis;
    if (settings.spline) {
        spline_axis(settings.u_count, settings.u_type, u_divisions, u_axis);
        spline_axis(settings.v_count, settings.v_type, v_divisions, v_axis);
    } else {
        bezier_axis(settings.u_count, u_divisions, u_axis);
        bezier_axis(settings.v_count, v_divisions, v_axis);
    }

    const std::size_t columns = u_axis.size(), rows = v_axis.size();
    vertices.resize(columns * rows);
    for (std::size_t row = 0; row < rows; ++row) {
        const AxisSample &v = v_axis[row];
        for (std::size_t column = 0; column < columns; ++column) {
            const AxisSample &u = u_axis[column];
            std::array<float, 3> position{}, du{}, dv{}, normal{};
            std::array<float, 2> texcoord{};
            std::array<float, 4> color{};
            for (std::uint32_t b = 0; b < 4u; ++b) {
                for (std::uint32_t a = 0; a < 4u; ++a) {
                    const Vertex &point = control[(v.first + b) * settings.u_count + u.first + a];
                    const float weight = u.weight[a] * v.weight[b];
                    const float along_u = u.slope[a] * v.weight[b], along_v = u.weight[a] * v.slope[b];
                    for (int axis = 0; axis < 3; ++axis) {
                        position[axis] += weight * point.position[axis];
                        du[axis] += along_u * point.position[axis];
                        dv[axis] += along_v * point.position[axis];
                        normal[axis] += weight * point.normal[axis];
                    }
                    if (settings.has_texcoords)
                        for (int axis = 0; axis < 2; ++axis) texcoord[axis] += weight * point.texcoord[axis];
                    if (settings.has_colors) {
                        const auto channels = unpack(point.color);
                        for (int channel = 0; channel < 4; ++channel) color[channel] += weight * channels[channel];
                    }
                }
            }
            Vertex &out = vertices[row * columns + column];
            out.position = {position[0], position[1], position[2], 1.0f};
            if (settings.compute_normals || !settings.has_normals) {
                normal = {du[1] * dv[2] - du[2] * dv[1], du[2] * dv[0] - du[0] * dv[2], du[0] * dv[1] - du[1] * dv[0]};
                const float length = std::hypot(normal[0], normal[1], normal[2]);
                normal = length > 0.0f ? std::array<float, 3>{normal[0] / length, normal[1] / length, normal[2] / length}
                                       : std::array<float, 3>{0.0f, 0.0f, 1.0f};
                if (settings.reverse)
                    for (float &component : normal) component = -component;
            }
            out.normal = normal;
            out.texcoord = settings.has_texcoords ? texcoord : std::array<float, 2>{u.parameter, v.parameter};
            if (settings.has_colors) {
                std::uint32_t packed = 0u;
                for (int channel = 0; channel < 4; ++channel)
                    packed |= static_cast<std::uint32_t>(std::clamp(std::lround(color[channel]), 0l, 255l)) << (channel * 8);
                out.color = packed;
            }
        }
    }

    const auto at = [&](std::size_t column, std::size_t row) { return static_cast<std::uint16_t>(row * columns + column); };
    if (settings.primitive == 2u) {
        primitive = PrimitiveType::Points;
        for (std::size_t i = 0; i < vertices.size(); ++i) indices.push_back(static_cast<std::uint16_t>(i));
        return true;
    }
    if (settings.primitive == 1u) {
        primitive = PrimitiveType::Lines;
        for (std::size_t row = 0; row < rows; ++row)
            for (std::size_t column = 0; column < columns; ++column) {
                if (column + 1u < columns) indices.insert(indices.end(), {at(column, row), at(column + 1u, row)});
                if (row + 1u < rows) indices.insert(indices.end(), {at(column, row), at(column, row + 1u)});
            }
        return true;
    }
    primitive = PrimitiveType::Triangles;
    indices.reserve((columns - 1u) * (rows - 1u) * 6u);
    for (std::size_t row = 0; row + 1u < rows; ++row)
        for (std::size_t column = 0; column + 1u < columns; ++column) {
            const std::uint16_t top_left = at(column, row), top_right = at(column + 1u, row);
            const std::uint16_t bottom_left = at(column, row + 1u), bottom_right = at(column + 1u, row + 1u);
            if (settings.reverse)
                indices.insert(indices.end(), {top_left, top_right, bottom_left, bottom_right, bottom_left, top_right});
            else
                indices.insert(indices.end(), {top_left, bottom_left, top_right, bottom_right, top_right, bottom_left});
        }
    return true;
}

} // namespace sarusaru::gpu
