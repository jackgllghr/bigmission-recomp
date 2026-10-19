#include "gpu/frame_interpolator.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

using sarusaru::gpu::DrawCall;
using sarusaru::gpu::FrameInterpolator;

constexpr std::array<float, 16> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

// A triangle of a static mesh, `depth` units in front of a camera at x.
DrawCall mesh(float object_x, float camera_x, std::uint32_t vertex_address = 0x08900000u, float depth = 10.0f) {
    DrawCall call;
    call.primitive = sarusaru::gpu::PrimitiveType::Triangles;
    call.vertex_address = vertex_address;
    call.vertex_type = 0x19Fu;
    call.primitive_count = 3u;
    call.texture.address = 0x09400000u;
    call.target.color_address = 0x04000000u;
    call.world = kIdentity;
    call.world[12] = object_x;
    call.view = kIdentity;
    call.view[12] = -camera_x;
    call.view[14] = -depth;
    call.projection = kIdentity;
    call.vertices.resize(3u);
    call.vertices[1].position[0] = 1.0f;
    call.vertices[2].position[1] = 1.0f;
    return call;
}

DrawCall sprite(float x, std::uint32_t vertex_address, std::uint32_t color = 0xFF000000u) {
    DrawCall call;
    call.primitive = sarusaru::gpu::PrimitiveType::Sprites;
    call.through = true;
    call.vertex_address = vertex_address;
    call.primitive_count = 2u;
    call.texture.address = 0x09100000u;
    call.vertices.resize(2u);
    call.vertices[0].position = {x, 10.0f, 0.0f, 1.0f};
    call.vertices[1].position = {x + 16.0f, 26.0f, 0.0f, 1.0f};
    call.vertices[1].texcoord = {16.0f, 16.0f};
    for (auto &vertex : call.vertices) vertex.color = color;
    return call;
}
} // namespace

int main() {
    DrawCall out;
    {
        // The first frame has nothing to blend with.
        FrameInterpolator interpolator;
        require(!interpolator.interpolate(mesh(0.0f, 0.0f), 0.5f, out) && out.world == mesh(0.0f, 0.0f).world,
                "first frame was blended");
        require(interpolator.stats().unmatched == 1u, "first frame not counted as unmatched");
        // The next frame's draw, moved and seen from a moved camera, lands halfway.
        interpolator.end_frame();
        require(interpolator.interpolate(mesh(1.0f, 0.5f), 0.5f, out), "moved mesh not blended");
        require(near(out.world[12], 0.5f) && near(out.view[12], -0.25f) && near(out.view[14], -10.0f),
                "matrices not blended halfway");
        // Its display buffer alternates, which must not prevent the match.
        interpolator.end_frame();
        DrawCall other_buffer = mesh(2.0f, 0.5f);
        other_buffer.target.color_address = 0x04088000u;
        require(interpolator.interpolate(other_buffer, 0.25f, out) && near(out.world[12], 1.25f),
                "alternating display buffer broke the match");
    }
    {
        // Instances of one mesh pair with the nearest previous instance, even
        // when the game draws them in another order.
        FrameInterpolator interpolator;
        interpolator.interpolate(mesh(0.0f, 0.0f), 0.5f, out);
        interpolator.interpolate(mesh(3.0f, 0.0f), 0.5f, out);
        interpolator.end_frame();
        require(interpolator.interpolate(mesh(3.2f, 0.0f), 0.5f, out) && near(out.world[12], 3.1f),
                "reordered instance matched the wrong one");
        require(interpolator.interpolate(mesh(0.2f, 0.0f), 0.5f, out) && near(out.world[12], 0.1f),
                "second instance matched the wrong one");
        require(interpolator.stats().matched == 2u, "instances not counted as matched");
    }
    {
        // A cut (the object far from where it was) is drawn as the new frame.
        FrameInterpolator interpolator;
        interpolator.interpolate(mesh(0.0f, 0.0f), 0.5f, out);
        interpolator.end_frame();
        require(!interpolator.interpolate(mesh(50.0f, 0.0f), 0.5f, out) && near(out.world[12], 50.0f),
                "a jump was blended");
        require(interpolator.stats().snapped == 1u, "jump not counted as snapped");
        // reset() forgets the previous frame.
        interpolator.reset();
        interpolator.end_frame();
        require(!interpolator.interpolate(mesh(50.0f, 0.0f), 0.5f, out), "reset kept the previous frame");
    }
    {
        // Vertices the CPU moved (skinning) blend, but texture coordinates do not.
        FrameInterpolator interpolator;
        DrawCall before = mesh(0.0f, 0.0f), after = mesh(0.0f, 0.0f);
        after.vertices[0].position[1] = 0.5f;
        after.vertices[0].texcoord = {0.5f, 0.5f};
        after.vertices[0].normal = {0.0f, 1.0f, 0.0f};
        interpolator.interpolate(before, 0.5f, out);
        interpolator.end_frame();
        require(interpolator.interpolate(after, 0.5f, out) && near(out.vertices[0].position[1], 0.25f) &&
                    near(out.vertices[0].normal[1], 0.5f) && out.vertices[0].texcoord[0] == 0.5f,
                "vertex blending incorrect");
        // One vertex leaping means the buffer now holds something else: none blend.
        DrawCall reused = mesh(0.0f, 0.0f);
        reused.vertices[2].position[1] = 40.0f;
        reused.vertices[0].position[1] = 0.5f;
        interpolator.end_frame();
        interpolator.interpolate(reused, 0.5f, out);
        require(out.vertices[0].position[1] == 0.5f && out.vertices[2].position[1] == 40.0f,
                "reused vertex buffer was blended");
    }
    {
        // Screen-space sprites from a buffer that moves every frame still match
        // (by what they draw), and blend position and colour; a sprite leaping
        // across the screen does not.
        FrameInterpolator interpolator;
        interpolator.interpolate(sprite(100.0f, 0x08E30000u, 0xFF000000u), 0.5f, out);
        interpolator.interpolate(sprite(300.0f, 0x08E30040u), 0.5f, out);
        interpolator.end_frame();
        require(interpolator.interpolate(sprite(110.0f, 0x08D90000u, 0xFF0000FFu), 0.5f, out) &&
                    near(out.vertices[0].position[0], 105.0f) && near(out.vertices[1].position[0], 121.0f) &&
                    out.vertices[0].color == 0xFF000080u && out.vertices[1].texcoord[0] == 16.0f,
                "moving sprite not blended");
        require(!interpolator.interpolate(sprite(20.0f, 0x08D90040u), 0.5f, out) && out.vertices[0].position[0] == 20.0f,
                "leaping sprite was blended");
    }
    {
        // Differently textured draws never pair.
        FrameInterpolator interpolator;
        interpolator.interpolate(mesh(0.0f, 0.0f), 0.5f, out);
        interpolator.end_frame();
        DrawCall retextured = mesh(1.0f, 0.0f);
        retextured.texture.address = 0x09500000u;
        require(!interpolator.interpolate(retextured, 0.5f, out) && interpolator.stats().unmatched == 2u,
                "draws with different textures were paired");
    }
    {
        // The camera is blended once for the frame: once an unmoved object has
        // fixed it, a draw with no previous-frame match, or one that snapped,
        // is still seen from the in-between camera, so seams between
        // neighbouring pieces of scenery stay closed.
        FrameInterpolator interpolator;
        interpolator.interpolate(mesh(0.0f, 0.0f), 0.5f, out);
        interpolator.interpolate(mesh(0.0f, 0.0f, 0x08910000u), 0.5f, out);
        interpolator.end_frame();
        require(interpolator.interpolate(mesh(0.0f, 1.0f), 0.5f, out) && near(out.view[12], -0.5f),
                "camera not blended");
        DrawCall fresh = mesh(5.0f, 1.0f, 0x08920000u);
        fresh.texture.address = 0x09500000u;
        require(!interpolator.interpolate(fresh, 0.5f, out) && near(out.view[12], -0.5f) && near(out.world[12], 5.0f),
                "unmatched draw left on the newer camera");
        require(!interpolator.interpolate(mesh(40.0f, 1.0f, 0x08910000u), 0.5f, out) && near(out.view[12], -0.5f) &&
                    near(out.world[12], 40.0f),
                "snapped draw left on the newer camera");
        // A cut fixes no camera: everything is drawn as the newer frame.
        interpolator.end_frame();
        require(!interpolator.interpolate(mesh(0.0f, 60.0f), 0.5f, out) && near(out.view[12], -60.0f),
                "cut camera blended");
        DrawCall after_cut = mesh(5.0f, 60.0f, 0x08920000u);
        after_cut.texture.address = 0x09500000u;
        require(!interpolator.interpolate(after_cut, 0.5f, out) && near(out.view[12], -60.0f), "draw after a cut blended");
    }
    {
        // 120 FPS motion: three pictures of one draw at once, each at its own
        // time, and a later draw with the same camera sees each picture's camera.
        constexpr float times[] = {0.25f, 0.5f, 0.75f};
        std::array<DrawCall, 3> pictures;
        FrameInterpolator interpolator;
        interpolator.interpolate(mesh(0.0f, 0.0f), times, pictures);
        interpolator.interpolate(mesh(0.0f, 0.0f, 0x08910000u), times, pictures);
        interpolator.end_frame();
        require(interpolator.interpolate(mesh(0.0f, 1.0f), times, pictures) && near(pictures[0].view[12], -0.25f) &&
                    near(pictures[1].view[12], -0.5f) && near(pictures[2].view[12], -0.75f),
                "the three pictures' cameras are not at their times");
        require(interpolator.interpolate(mesh(2.0f, 1.0f, 0x08910000u), times, pictures) &&
                    near(pictures[0].world[12], 0.5f) && near(pictures[1].world[12], 1.0f) &&
                    near(pictures[2].world[12], 1.5f) && near(pictures[2].view[12], -0.75f),
                "the three pictures of a moved mesh are not at their times");
    }
    std::cout << "frame interpolator tests passed\n";
    return 0;
}
