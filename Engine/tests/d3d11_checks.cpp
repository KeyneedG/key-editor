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
    const auto cpuStats = cpu->stats(), gpuStats = gpu.stats();
    check(cpuStats.meshSubmissions == gpuStats.meshSubmissions && cpuStats.cameras == gpuStats.cameras &&
        gpuStats.batches <= cpuStats.batches, "Instancing changed mesh/camera visibility or increased draw counts");
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
void instancingChecks(RenderBackend& gpu) {
    Scene smallText; auto& smallCamera = smallText.entities.emplace_back(); auto& smallView = smallCamera.addComponent<Camera>();
    smallView.orthographic = true; smallView.orthographicSize = 100;
    auto& smallCanvas = smallText.entities.emplace_back(); smallCanvas.transform.localPosition.z = 5; smallCanvas.addComponent<Canvas>().camera = &smallCamera;
    for (int i = 0; i < 3; ++i) {
        auto& e = smallText.entities.emplace_back(); e.setParent(&smallCanvas, false); e.transform.localPosition = {i * .5f, (i - 1) * 25.f, 0};
        auto& text = e.addComponent<SimpleText>(); text.text = "RMB LOOK | F FRAME"; text.pixelSize = i == 0 ? 1 : i == 1 ? 1.25f : 2;
    }
    UI{}.refresh(smallText); gpu.render(smallText, {}); const auto instancedText = gpu.readPixels();
    for (auto& e : smallText.entities) if (auto* visual = e.getComponent<MeshRenderer>()) {
        auto mesh = std::make_shared<Mesh>(*visual->mesh); mesh->markChanged(); visual->mesh = mesh;
    }
    gpu.render(smallText, {}); const auto legacyText = gpu.readPixels();
    std::size_t textDifferences = 0; for (std::size_t i = 0; i < legacyText.size(); ++i) if (instancedText[i] != legacyText[i]) ++textDifferences;
    std::cout << "Small-font instanced/mesh pixel differences: " << textDifferences << '\n';
    check(textDifferences == 0, "Instanced glyph pixel boundaries differ from GPU font meshes");
    Scene cubes; auto& camera = cubes.entities.emplace_back(); camera.transform.localPosition.z = -8;
    auto& view = camera.addComponent<Camera>(); view.orthographic = true; view.orthographicSize = 6;
    auto& parent = cubes.entities.emplace_back(); parent.transform.localRotation = Quaternion::fromEuler({.1f, .2f, .05f}); parent.transform.localScale = {1.1f, .9f, 1.2f};
    for (int i = 0; i < 160; ++i) {
        auto& e = box(cubes, {(i % 16 - 7.5f) * .65f, (i / 16 - 4.5f) * .65f, 0},
            Color{std::uint8_t(50 + i % 10 * 20), std::uint8_t(220 - i % 9 * 20), std::uint8_t(30 + i % 11 * 20)});
        e.transform.localScale = {.45f, .45f, .45f}; e.transform.localRotation = Quaternion::fromEuler({i * .015f, i * .03f, 0});
        e.getComponent<MeshRenderer>()->unlit = i % 2 == 0;
        if (i % 2) e.setParent(&parent, false);
    }
    compare(gpu, cubes, "160 cubes with per-instance colors/lighting/transforms");
    check(gpu.stats().batches == 1 && gpu.stats().instancedBatches == 1 && gpu.stats().instances == 160 && gpu.stats().meshUploads == 1,
        "Independent identical cubes did not share one instanced draw/upload");
    for (auto& e : cubes.entities) if (auto* visual = e.getComponent<MeshRenderer>()) { visual->color = {20, 110, 240}; e.transform.localPosition.x += .1f; }
    compare(gpu, cubes, "Instance color/transform updates");
    check(gpu.stats().batches == 1 && gpu.stats().meshUploads == 0, "Instance data changes re-uploaded mesh geometry or split batches");
    auto edited = std::const_pointer_cast<Mesh>(cubes.entities[82].getComponent<MeshRenderer>()->mesh);
    for (auto& v : edited->vertices) v.x *= 1.5f;
    edited->markChanged(); compare(gpu, cubes, "One edited cube splits an instanced run");
    check(gpu.stats().batches == 3 && gpu.stats().instances == 160 && gpu.stats().meshUploads == 1, "Mesh edit did not split/re-upload only its geometry");
    for (auto& v : edited->vertices) v.x /= 1.5f;
    edited->markChanged(); compare(gpu, cubes, "Restored geometry reuses shared buffers");
    check(gpu.stats().batches == 1 && gpu.stats().meshUploads == 0, "Restoring identical geometry did not reuse its original buffers");
    cubes.entities[2].visible = false; cubes.entities[3].layer = 3; view.layers = layerMask(0);
    compare(gpu, cubes, "Instanced visibility/layer filtering"); check(gpu.stats().instances == 158, "Hidden or excluded instances were submitted");
    auto& overlay = cubes.entities.emplace_back(); overlay.transform = camera.transform; auto& overlayView = overlay.addComponent<Camera>();
    overlayView.orthographic = true; overlayView.orthographicSize = 6; overlayView.layers = layerMask(0); overlayView.clearColor = false; overlayView.depth = 1;
    compare(gpu, cubes, "Instanced camera passes"); check(gpu.stats().batches == 2 && gpu.stats().instances == 316, "Instancing crossed camera passes");
    const auto savedCubes = serializeScene(cubes); edited.reset(); cubes = {}; cubes = deserializeScene(savedCubes);
    compare(gpu, cubes, "Rebuilt scene keeps shared geometry on GPU");
    check(gpu.stats().batches == 2 && gpu.stats().meshUploads == 0, "Rebuilding scene snapshots re-uploaded unchanged geometry");

    Scene ties; auto& tieCamera = ties.entities.emplace_back(); tieCamera.transform.localPosition.z = -5; tieCamera.addComponent<Camera>();
    box(ties, {}, {240, 50, 20});
    auto& different = ties.entities.emplace_back(); different.addComponent<MeshRenderer>(std::make_shared<Mesh>(Mesh{
        {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f}}, {{0, 2, 1}, {0, 3, 2}}}), Color{20, 220, 40}).unlit = true;
    box(ties, {}, {20, 50, 240}); compare(gpu, ties, "Coplanar A/B/A draw order");
    check(gpu.stats().batches == 3 && gpu.readPixels()[std::size_t(gpu.height() / 2) * gpu.width() + gpu.width() / 2] == Color{240, 50, 20}.packed(),
        "Grouping reordered equal-depth surfaces");

    gpu.resize(640, 400);
    Scene textScene; auto& textCamera = textScene.entities.emplace_back(); auto& textView = textCamera.addComponent<Camera>();
    textView.orthographic = true; textView.orthographicSize = 100; textView.farPlane = 500;
    auto& canvas = textScene.entities.emplace_back(); canvas.setParent(&textCamera, false); canvas.transform.localPosition.z = 5;
    canvas.addComponent<Canvas>().camera = &textCamera;
    auto& outer = textScene.entities.emplace_back(); outer.setParent(&canvas, false);
    auto& outerImage = outer.addComponent<Image>(); outerImage.width = 280; outerImage.height = 160;
    auto& outerScroll = outer.addComponent<ScrollRect>(); outerScroll.contentHeight = 180;
    auto& inner = textScene.entities.emplace_back(); inner.setParent(&outer, false); outerScroll.content = &inner;
    inner.transform.localRotation = Quaternion::fromEuler({0, 0, .2f}); inner.transform.localScale = {.9f, 1.1f, 1};
    auto& innerImage = inner.addComponent<Image>(); innerImage.width = 230; innerImage.height = 120;
    auto& innerScroll = inner.addComponent<ScrollRect>(); innerScroll.contentHeight = 160; innerScroll.offset = 10;
    auto& content = textScene.entities.emplace_back(); content.setParent(&inner, false); innerScroll.content = &content;
    for (int i = 0; i < 24; ++i) {
        // Give overlapping labels distinct depths; compare glyph/clipping behavior independently of raster depth rounding.
        auto& e = textScene.entities.emplace_back(); e.setParent(&content, false); e.transform.localPosition = {i * .1f, i * .1f, -.1f - i * .01f};
        auto& text = e.addComponent<SimpleText>(); text.pixelSize = 1.25f;
        text.color = Color{std::uint8_t(240 - i * 6), std::uint8_t(80 + i * 7), 130};
        text.text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789 .,:;-_+=/!?()[]<>|*\\\"'%";
    }
    auto& field = textScene.entities.emplace_back(); field.setParent(&canvas, false); field.transform.localPosition = {0, -85, -.1f};
    auto& fieldImage = field.addComponent<Image>(); fieldImage.width = 220; fieldImage.height = 24;
    field.addComponent<InputField>().text = "INSTANCED INPUT FIELD";
    UI ui; ui.refresh(textScene); compare(gpu, textScene, "All font bits/colors with nested rotated clipping");
    check(gpu.stats().batches == 1 && gpu.stats().instancedBatches == 1 && gpu.stats().glyphInstances > 1000,
        "Text characters/images were not instances of one shared quad");
    innerScroll.offset = 30; outerScroll.offset = 8; inner.transform.localRotation = Quaternion::fromEuler({.15f, -.1f, -.25f});
    ui.refresh(textScene); compare(gpu, textScene, "Changing nested glyph clipping/scroll offsets");
    auto roundTrip = deserializeScene(serializeScene(textScene)); UI{}.refresh(roundTrip); compare(gpu, roundTrip, "Scene-loaded instanced text");
    ui.focus(field); ui.refresh(textScene); compare(gpu, textScene, "Instanced input caret and text clipping");
    textView.orthographic = false; textView.nearPlane = 1; canvas.transform.localPosition.z = 180; ui.refresh(textScene);
    compare(gpu, textScene, "Perspective glyph interpolation/clipping");
    gpu.resize(320, 200);
}
void checks(RenderBackend& gpu) {
    Scene scene; compare(gpu, scene, "No cameras", 0);
    auto& camera = scene.entities.emplace_back(); auto& view = camera.addComponent<Camera>();
    view.nearPlane = .2f; view.farPlane = 40; camera.transform.localPosition = {0, 0, -5};
    auto& center = box(scene, {}, {231, 122, 57});
    compare(gpu, scene, "Perspective cube");
    check(gpu.stats().batches == 1 && gpu.stats().triangles == 12 && gpu.stats().vertices == 8 && gpu.stats().cameras == 1 && gpu.stats().meshUploads == 1,
        "Cube submission/upload counts are incorrect");
    gpu.render(scene, {}); check(gpu.stats().meshUploads == 0, "Cached geometry was counted as a new upload");
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
    editor.frameRendered({gpu.stats(), gpu.name(), gpu.width(), gpu.height(), .02f, .001f, .002f, .003f});
    Entity* statsButton = nullptr;
    for (auto& e : editor.scene().entities) if (e.getComponent<Button>()) for (std::size_t i = 0; i < e.childCount(); ++i)
        if (auto* text = e.getChild(i)->getComponent<SimpleText>(); text && text->text == "Stats") statsButton = &e;
    check(statsButton, "Stats button missing");
    const auto p = statsButton->transform.position();
    Input input; input.focused = true; input.renderWidth = 1280; input.renderHeight = 800; input.viewport = {0, 0, 1280, 800};
    input.mouseX = int(640 + p.x); input.mouseY = int(400 - p.y); input.set(MouseButton::Left, true); editor.update(.016f, input);
    input.beginFrame(); input.set(MouseButton::Left, false); editor.update(.016f, input);
    compare(gpu, editor.scene(), "Stats overlay", .02);
    std::cout << "Editor submissions: " << gpu.stats().meshSubmissions << " -> " << gpu.stats().batches
              << " draw calls, " << gpu.stats().instances << " instances (" << gpu.stats().glyphInstances << " glyphs)\n";
    std::ofstream capture("editor-stats.ppm", std::ios::binary);
    capture << "P6\n" << gpu.width() << ' ' << gpu.height() << "\n255\n";
    for (auto pixel : gpu.readPixels()) { const char rgb[]{char(pixel >> 16), char(pixel >> 8), char(pixel)}; capture.write(rgb, 3); }
    capture.close(); check(bool(capture), "Could not write editor stats capture");
}
void nativeChecks(d3d11::Options options) {
    HWND window = CreateWindowExW(0, L"STATIC", L"Renderer checks", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    check(window, "Could not create test window");
    struct Close { HWND value; ~Close() { DestroyWindow(value); } } close{window};
    auto backend = d3d11::createRenderer(window, 320, 200, options); Scene scene;
    const bool previousVSync = Application::vSync;
    for (bool vsync : {false, true, false}) {
        Application::vSync = vsync;
        for (auto size : {std::pair{640, 480}, {500, 300}, {900, 500}}) {
            backend->render(scene, {31, 47, 63}); backend->present(size.first, size.second);
        }
    }
    Application::vSync = previousVSync;
    backend->resize(640, 400); backend->render(scene, {}); backend->present(640, 480);
    backend->present(0, 0); // Minimized windows must leave the swap chain intact.
}
int main(int argc, char** argv) {
    try {
        d3d11::Options options; options.softwareDevice = argc < 2 || std::string(argv[1]) != "--hardware";
        auto gpu = d3d11::createRenderer(nullptr, 320, 200, options); std::cout << gpu->name() << '\n';
        instancingChecks(*gpu); checks(*gpu); nativeChecks(options);
        std::cout << "Direct3D rendering, UI, readback, and swap chain checks passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
