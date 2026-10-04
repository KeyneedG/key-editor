#include "tiny3d/d3d11.hpp"
#include "tiny3d/engine.hpp"
#include "tiny3d/ui.hpp"
#include "tiny3d/editor.hpp"
#include "tiny3d/scene.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace tiny3d;
void check(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F f) { bool rejected = false; try { f(); } catch (const std::exception&) { rejected = true; } check(rejected, "Invalid render data was accepted"); }

void compare(RenderBackend& gpu, const Scene& scene, const char* name, double tolerance = .015) {
    auto cpu = createSoftwareRenderer(nullptr, gpu.width(), gpu.height());
    const Color background{28, 36, 52}; cpu->render(scene, background); gpu.render(scene, background);
    const auto expected = cpu->readPixels(), actual = gpu.readPixels();
    check(actual.size() == expected.size(), "Readback size differs from the render resolution");
    std::size_t different = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        bool mismatch = false;
        for (unsigned shift : {0, 8, 16}) if (std::abs(int((actual[i] >> shift) & 255) - int((expected[i] >> shift) & 255)) > 2) mismatch = true;
        if (mismatch) ++different;
    }
    const double fraction = double(different) / actual.size();
    std::cout << name << ": " << fraction * 100 << "% pixels differ\n";
    if (fraction > tolerance) throw std::runtime_error(std::string("GPU/software rendering mismatch: ") + name);
}
Entity& box(Scene& scene, Vector3 position, Color color, unsigned layer = 0) {
    auto& e = scene.entities.emplace_back(); e.layer = static_cast<std::uint8_t>(layer); e.transform.localPosition = position;
    e.addComponent<MeshRenderer>(std::make_shared<Mesh>(cube()), color).unlit = true; return e;
}
void checks(RenderBackend& gpu) {
    Scene scene; compare(gpu, scene, "No cameras", 0);
    auto& camera = scene.entities.emplace_back(); auto& view = camera.addComponent<Camera>();
    view.nearPlane = .2f; view.farPlane = 40; camera.transform.localPosition = {0, 0, -5};
    auto& center = box(scene, {}, {231, 122, 57});
    compare(gpu, scene, "Perspective cube");
    center.getComponent<MeshRenderer>()->unlit = false; center.transform.localRotation = Quaternion::fromEuler({.3f, .45f, .1f});
    compare(gpu, scene, "Flat lighting");
    auto& parent = scene.entities.emplace_back(); parent.transform.localScale = {1.7f, .8f, 1.2f}; parent.transform.localRotation = Quaternion::fromEuler({.2f, -.3f, .2f});
    center.setParent(&parent, false); compare(gpu, scene, "Parent transforms and shear");
    parent.visible = false; compare(gpu, scene, "Hidden parent", 0); parent.visible = true;
    view.orthographic = true; view.orthographicSize = 2; compare(gpu, scene, "Orthographic lighting");
    box(scene, {0, 0, -1}, {54, 198, 98}); compare(gpu, scene, "Depth ordering");
    auto& overlay = scene.entities.emplace_back(); overlay.transform.localPosition = camera.transform.localPosition;
    auto& overlayView = overlay.addComponent<Camera>(); overlayView.depth = -5; overlayView.orthographic = true; overlayView.orthographicSize = 2;
    overlayView.layers = layerMask(3); overlayView.clearColor = false;
    box(scene, {1, .6f, -2}, {50, 114, 255}, 3); view.layers = layerMask(0); view.depth = -10;
    compare(gpu, scene, "Camera sorting/layers/overlay");
    overlayView.depth = view.depth; compare(gpu, scene, "Equal camera depth ordering");
    overlayView.clearColor = true; compare(gpu, scene, "Camera color clearing"); overlayView.enabled = false;
    view.orthographic = false;
    auto& clipped = box(scene, {-.7f, 0, -4.5f}, {242, 232, 65}); compare(gpu, scene, "Near/frustum clipping");
    auto editableMesh = std::make_shared<Mesh>(cube()); clipped.getComponent<MeshRenderer>()->mesh = editableMesh;
    compare(gpu, scene, "Mesh replacement");
    for (auto& v : editableMesh->vertices) v.x *= .3f;
    editableMesh->markChanged(); compare(gpu, scene, "Mesh edits");
    gpu.resize(400, 240); compare(gpu, scene, "Render target resize");
    rejects([&] { gpu.resize(0, 100); });
    editableMesh->triangles[0][0] = 9999; editableMesh->markChanged(); rejects([&] { gpu.render(scene, {}); });
    editableMesh->triangles[0][0] = 0; editableMesh->markChanged();
    view.nearPlane = -1; rejects([&] { gpu.render(scene, {}); }); view.nearPlane = .2f;

    Scene uiScene;
    auto& uiCamera = uiScene.entities.emplace_back(); auto& uiView = uiCamera.addComponent<Camera>(); uiView.orthographic = true; uiView.orthographicSize = 120;
    auto& canvas = uiScene.entities.emplace_back(); canvas.setParent(&uiCamera, false); canvas.transform.localPosition.z = 5; canvas.addComponent<Canvas>().camera = &uiCamera;
    auto& rect = uiScene.entities.emplace_back(); rect.setParent(&canvas, false); auto& image = rect.addComponent<Image>(); image.width = 180; image.height = 90;
    auto& scroll = rect.addComponent<ScrollRect>(); scroll.contentHeight = 200;
    auto& content = uiScene.entities.emplace_back(); content.setParent(&rect, false); scroll.content = &content;
    auto& label = uiScene.entities.emplace_back(); label.setParent(&content, false); label.transform.localPosition = {0, -35, -.1f}; label.addComponent<SimpleText>().text = "CLIPPED\nGPU UI";
    auto& inputEntity = uiScene.entities.emplace_back(); inputEntity.setParent(&canvas, false); inputEntity.transform.localPosition = {0, 80, -.1f};
    inputEntity.addComponent<Image>().width = 180; inputEntity.addComponent<InputField>().text = "DIRECT3D 11";
    UI ui; ui.refresh(uiScene); compare(gpu, uiScene, "Text/input/scroll UI");
    auto textMesh = label.getComponent<MeshRenderer>()->mesh;
    auto* inputInk = dynamic_cast<MeshRenderer*>(inputEntity.components.back().get());
    check(inputInk, "Input text renderer was not created");
    auto inputMesh = inputInk->mesh;
    ui.refresh(uiScene); check(textMesh == label.getComponent<MeshRenderer>()->mesh, "Unchanged UI clipping recreates meshes");
    check(inputMesh == inputInk->mesh, "Unchanged input text recreates its mesh");
    inputEntity.getComponent<InputField>()->text = "EDITED GPU TEXT";
    ui.refresh(uiScene); check(inputMesh != inputInk->mesh, "Input text edits did not invalidate its mesh");
    compare(gpu, uiScene, "Edited input text");
    scroll.offset = 25; ui.refresh(uiScene); check(textMesh != label.getComponent<MeshRenderer>()->mesh, "Scrolling did not invalidate clipping");
    compare(gpu, uiScene, "Scrolled UI");
    rect.transform.localScale = {.8f, 1.2f, 1}; image.width = 160; ui.refresh(uiScene); compare(gpu, uiScene, "Changed UI clip rectangle");
    gpu.resize(1280, 800);
    auto source = std::filesystem::path(TINY3D_SOURCE_DIRECTORY);
    const auto demo = source.parent_path() / "DEMO/Assets/Untitled.scene.json";
    Scene initial;
    if (std::filesystem::exists(demo)) initial = loadScene(demo);
    else {
        initial.entities.emplace_back().addComponent<Camera>();
        box(initial, {0, 0, 5}, {170, 190, 220});
    }
    editor::Editor editor(std::move(initial), std::filesystem::current_path() / "gpu-editor-check");
    compare(gpu, editor.scene(), "Editor scene and panels", .02);
    editor.addObject("Cube"); editor.update(0, Input{});
    compare(gpu, editor.scene(), "Inspector and gizmos", .02);
}
void nativeChecks(d3d11::Options options) {
    HWND window = CreateWindowExW(0, L"STATIC", L"Renderer checks", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window, "Could not create test window");
    struct Close { HWND value; ~Close() { DestroyWindow(value); } } close{window};
    auto backend = d3d11::createRenderer(window, 320, 200, options); Scene scene;
    for (auto size : {std::pair{640, 480}, {500, 300}, {900, 500}}) {
        backend->render(scene, {31, 47, 63}); backend->present(size.first, size.second);
    }
    backend->resize(640, 400); backend->render(scene, {}); backend->present(640, 480);
    backend->present(0, 0); // Minimized windows must leave the swap chain intact.
}
int main(int argc, char** argv) {
    try {
        d3d11::Options options; options.softwareDevice = argc < 2 || std::string(argv[1]) != "--hardware";
        auto gpu = d3d11::createRenderer(nullptr, 320, 200, options); std::cout << gpu->name() << '\n';
        checks(*gpu); nativeChecks(options);
        std::cout << "Direct3D rendering, UI, readback, and swap chain checks passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
