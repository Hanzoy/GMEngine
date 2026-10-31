#include "editor_camera.hpp"
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool near(gm::Vec3 a, gm::Vec3 b, float epsilon = .001f) {
    for (int i = 0; i < 3; ++i)
        if (std::abs(a[i] - b[i]) > epsilon)
            return false;
    return true;
}
} // namespace
int main() {
    try {
        gm::EditorCamera camera;
        camera.position = {0, 0, 0};
        camera.yaw = camera.pitch = 0;
        auto pivot = camera.pivot();
        camera.look(230, 110, true);
        check(near(camera.pivot(), pivot), "Orbit must preserve the pivot");
        auto eye = camera.position;
        camera.look(-100, 90);
        check(near(camera.position, eye), "Look must preserve eye position");
        camera.look(100000, 100000);
        check(camera.valid(), "Pitch/yaw limits");
        camera.position = {0, 0, 0};
        camera.yaw = camera.pitch = 0;
        camera.pan(100, 100, 1000);
        check(camera.position[0] < 0 && camera.position[1] > 0, "Pan tracks dragged scene");
        pivot = camera.pivot();
        camera.dolly(3);
        check(near(pivot, camera.pivot()), "Dolly preserves orbit target");
        camera.changeSpeed(10000);
        check(camera.speed == 10000, "Maximum speed bounded");
        camera.changeSpeed(-10000);
        check(camera.speed == .01f, "Minimum speed bounded");
        camera.speed = 5;
        camera.position = {0, 0, 0};
        auto a = camera, b = camera;
        for (int i = 0; i < 30; ++i)
            a.fly(0, 0, 1, 1.f / 30);
        for (int i = 0; i < 120; ++i)
            b.fly(0, 0, 1, 1.f / 120);
        check(near(a.position, b.position) && near(a.position, {0, 0, 5}),
              "Fly distance is frame-rate independent");
        b = camera;
        for (int i = 0; i < 60; ++i)
            b.fly(1, 0, 1, 1.f / 60);
        check(std::abs(std::sqrt(gm::dot3D(b.position, b.position)) - 5) < .001f,
              "Diagonal speed normalized");
        b = camera;
        b.fly(0, 0, 1, 100);
        check(near(b.position, {0, 0, .5f}), "Stalled frames cannot teleport camera");
        b = camera;
        b.fly(0, 0, 1, .1f, true);
        check(near(b.position, {0, 0, 2}), "Shift speed multiplier");
        gm::EditorBounds3D bounds;
        auto cube = gm::primitiveMesh("builtin:cube");
        gm::sdk::Transform3D transform;
        transform.x = 30;
        transform.y = 7;
        transform.z = 15;
        transform.scaleX = 2;
        transform.rotationY = 37;
        bounds.include(cube, gm::local3D(transform));
        camera.yaw = 35;
        camera.pitch = 25;
        for (float aspect : {.25f, 1.f, 3.f}) {
            camera.frame(bounds, aspect);
            check(near(camera.pivot(), bounds.center()), "Frame centers transformed bounds");
            auto clip = gm::multiply3D(gm::perspective3D(60, aspect, .01f, 100000, false),
                                       gm::inverseAffine3D(camera.matrix()));
            for (int i = 0; i < 8; ++i) {
                auto p = gm::Vec3{(i & 1) ? bounds.maximum[0] : bounds.minimum[0],
                                  (i & 2) ? bounds.maximum[1] : bounds.minimum[1],
                                  (i & 4) ? bounds.maximum[2] : bounds.minimum[2]};
                float w = clip[3] * p[0] + clip[7] * p[1] + clip[11] * p[2] + clip[15];
                auto q = gm::transformPoint(clip, p);
                check(w > 0 && std::abs(q[0]) <= w && std::abs(q[1]) <= w,
                      "Frame fits all corners, including narrow viewport");
            }
        }
        gm::RenderAssets assets;
        assets.meshes.push_back(cube);
        gm::RenderFrame frame;
        transform = {};
        transform.z = 8;
        frame.meshes.push_back({7, 0, UINT32_MAX, gm::local3D(transform)});
        transform.z = 4;
        transform.scaleX = 2;
        transform.rotationY = 25;
        frame.meshes.push_back({9, 0, UINT32_MAX, gm::local3D(transform)});
        camera = {};
        camera.position = {0, 0, 0};
        camera.yaw = camera.pitch = 0;
        check(gm::pickEditorMesh(assets, frame, camera, .5f, .5f, 1) == 9,
              "Pick nearest rotated/scaled mesh without a game camera");
        check(gm::pickEditorMesh(assets, frame, camera, 0, 0, 1) == UINT32_MAX, "Empty-space ray misses");
        camera.look(900, 0);
        check(gm::pickEditorMesh(assets, frame, camera, .5f, .5f, 1) == UINT32_MAX,
              "Meshes behind camera cannot be picked");
        transform = {};
        transform.x = 7;
        transform.y = 4;
        transform.rotationY = -20;
        transform.rotationX = 30;
        transform.scaleZ = 3;
        camera.fromGameCamera(gm::local3D(transform));
        check(near(camera.position, {7, 4, 0}) && std::abs(camera.yaw + 20) < .001f &&
                  std::abs(camera.pitch - 30) < .001f,
              "Initialize once from a scaled game camera");
        camera.speed = std::numeric_limits<float>::infinity();
        check(!camera.valid(), "Nonfinite camera rejected");
        std::cout << "Editor camera navigation, framing and picking contracts passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
