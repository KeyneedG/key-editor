#include "tiny3d/d3d11.hpp"
#include "tiny3d/engine.hpp"
#include "shaders.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <bit>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace tiny3d::d3d11 {
namespace {
using Microsoft::WRL::ComPtr;
void require(HRESULT hr, const char* action) {
    if (FAILED(hr)) {
        std::ostringstream message; message << "Direct3D 11: " << action << " failed (0x" << std::hex << unsigned(hr) << ')';
        throw std::runtime_error(message.str());
    }
}
ComPtr<ID3DBlob> compile(const char* source, const char* entry, const char* target) {
    ComPtr<ID3DBlob> code, error;
    auto hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
    if (FAILED(hr) && error) throw std::runtime_error("Direct3D 11 shader: " + std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize()));
    require(hr, "compile shader"); return code;
}

struct Matrix { float m[4][4]{}; };
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result;
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        for (int k = 0; k < 4; ++k) result.m[row][column] += a.m[row][k] * b.m[k][column];
    return result;
}
Matrix worldMatrix(const Transform& transform) {
    Matrix result;
    Vector3 x = transform.vector({1, 0, 0}), y = transform.vector({0, 1, 0}), z = transform.vector({0, 0, 1}), p = transform.position();
    result.m[0][0] = x.x; result.m[0][1] = y.x; result.m[0][2] = z.x; result.m[0][3] = p.x;
    result.m[1][0] = x.y; result.m[1][1] = y.y; result.m[1][2] = z.y; result.m[1][3] = p.y;
    result.m[2][0] = x.z; result.m[2][1] = y.z; result.m[2][2] = z.z; result.m[2][3] = p.z;
    result.m[3][3] = 1; return result;
}
Matrix viewProjection(const Transform& transform, const Camera& camera, int width, int height) {
    if (!(camera.nearPlane > 0 && camera.farPlane > camera.nearPlane) ||
        (camera.orthographic ? !(camera.orthographicSize > 0) : !(camera.fieldOfView > 0 && camera.fieldOfView < pi)))
        throw std::invalid_argument("Invalid camera projection");
    Matrix view; Vector3 p = transform.position();
    const std::array<Vector3, 3> basis{transform.right(), transform.up(), transform.forward()};
    for (int i = 0; i < 3; ++i) { view.m[i][0] = basis[i].x; view.m[i][1] = basis[i].y; view.m[i][2] = basis[i].z; view.m[i][3] = -dot(basis[i], p); }
    view.m[3][3] = 1;
    const float halfY = camera.orthographic ? camera.orthographicSize : std::tan(camera.fieldOfView * .5f);
    const float halfX = halfY * width / height;
    Matrix projection; projection.m[0][0] = 1 / halfX; projection.m[1][1] = 1 / halfY;
    const float range = camera.farPlane - camera.nearPlane;
    if (camera.orthographic) {
        projection.m[2][2] = 1 / range; projection.m[2][3] = -camera.nearPlane / range; projection.m[3][3] = 1;
    } else {
        projection.m[2][2] = camera.farPlane / range; projection.m[2][3] = -camera.nearPlane * camera.farPlane / range; projection.m[3][2] = 1;
    }
    return multiply(projection, view);
}
struct InstanceData {
    Matrix world;
    float color[4], rect[4]{};
    std::uint32_t ink[4]{};
    float quadZ = 0;
};
static_assert(sizeof(Matrix) == 64 && sizeof(InstanceData) == 116);

class D3D11Renderer final : public RenderBackend {
public:
    D3D11Renderer(void* window, int w, int h, Options options) : window_(static_cast<HWND>(window)) {
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        require(D3D11CreateDevice(nullptr, options.softwareDevice ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | (options.debug ? D3D11_CREATE_DEVICE_DEBUG : 0), levels, 1,
            D3D11_SDK_VERSION, &device_, &level, &context_), "create device");
        name_ = options.softwareDevice ? "Direct3D 11 (WARP)" : "Direct3D 11 (hardware)";
        createPipeline(); resize(w, h);
        if (window_) createSwapChain();
    }
    ~D3D11Renderer() override { if (context_) context_->ClearState(); }
    int width() const override { return width_; }
    int height() const override { return height_; }
    const char* name() const override { return name_.c_str(); }
    graphics::RenderStats stats() const override { return stats_; }

    void resize(int w, int h) override {
        if (w <= 0 || h <= 0) throw std::invalid_argument("Renderer dimensions must be positive");
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* none = nullptr; context_->PSSetShaderResources(0, 1, &none);
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = UINT(w); desc.Height = UINT(h); desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> frame; ComPtr<ID3D11RenderTargetView> target; ComPtr<ID3D11ShaderResourceView> source;
        require(device_->CreateTexture2D(&desc, nullptr, &frame), "create frame texture");
        require(device_->CreateRenderTargetView(frame.Get(), nullptr, &target), "create frame target");
        require(device_->CreateShaderResourceView(frame.Get(), nullptr, &source), "create frame source");
        desc.Format = DXGI_FORMAT_D32_FLOAT; desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> depth; ComPtr<ID3D11DepthStencilView> depthView;
        require(device_->CreateTexture2D(&desc, nullptr, &depth), "create depth texture");
        require(device_->CreateDepthStencilView(depth.Get(), nullptr, &depthView), "create depth view");
        frame_ = std::move(frame); target_ = std::move(target); source_ = std::move(source); depth_ = std::move(depthView);
        staging_.Reset(); width_ = w; height_ = h;
    }

    void render(const Scene& scene, Color background) override {
        stats_ = {};
        ID3D11ShaderResourceView* none = nullptr; context_->PSSetShaderResources(0, 1, &none);
        auto* target = target_.Get(); context_->OMSetRenderTargets(1, &target, depth_.Get());
        D3D11_VIEWPORT viewport{0, 0, float(width_), float(height_), 0, 1}; context_->RSSetViewports(1, &viewport);
        context_->RSSetState(raster_.Get()); context_->OMSetDepthStencilState(depthState_.Get(), 0);
        context_->OMSetBlendState(nullptr, nullptr, ~UINT{0});
        context_->IASetInputLayout(layout_.Get()); context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertex_.Get(), nullptr, 0); context_->PSSetShader(pixel_.Get(), nullptr, 0);
        auto* constants = constants_.Get(); context_->VSSetConstantBuffers(0, 1, &constants); context_->PSSetConstantBuffers(0, 1, &constants);
        const float clear[]{background.r / 255.f, background.g / 255.f, background.b / 255.f, 1};
        context_->ClearRenderTargetView(target_.Get(), clear);
        struct View { const Camera* camera; const Transform* transform; };
        struct Draw { const MeshRenderer* visual; Matrix world; std::uint32_t layer; };
        std::vector<View> views; std::vector<Draw> draws;
        for (const auto& entity : scene.entities) {
            if (!entity.visibleInHierarchy()) continue;
            for (const auto& component : entity.components) {
                if (!component->enabled) continue;
                if (auto* camera = dynamic_cast<const Camera*>(component.get())) views.push_back({camera, &entity.transform});
                if (auto* mesh = dynamic_cast<const MeshRenderer*>(component.get()); mesh && mesh->mesh && !mesh->mesh->triangles.empty())
                    draws.push_back({mesh, worldMatrix(entity.transform), layerMask(entity.layer)});
            }
        }
        std::stable_sort(views.begin(), views.end(), [](const View& a, const View& b) { return a.camera->depth < b.camera->depth; });
        for (const auto& view : views) {
            ++stats_.cameras;
            const auto projection = viewProjection(*view.transform, *view.camera, width_, height_);
            if (view.camera->clearColor) context_->ClearRenderTargetView(target_.Get(), clear);
            context_->ClearDepthStencilView(depth_.Get(), D3D11_CLEAR_DEPTH, 1, 0);
            context_->UpdateSubresource(constants_.Get(), 0, nullptr, &projection, 0, 0);
            struct Batch { std::shared_ptr<GpuMesh> mesh; std::size_t first, count; };
            std::vector<Batch> batches;
            std::vector<InstanceData> instances;
            std::vector<graphics::ClipPlane> planes;
            for (const auto& draw : draws) {
                if (!(view.camera->layers & draw.layer)) continue;
                ++stats_.meshSubmissions;
                InstanceData data{draw.world, {draw.visual->color.r / 255.f, draw.visual->color.g / 255.f,
                    draw.visual->color.b / 255.f, draw.visual->unlit ? 1.f : 0.f}};
                const auto& source = *draw.visual->mesh;
                auto mesh = meshBuffer(source.quads.empty() ? draw.visual->mesh : quadMesh_);
                const auto append = [&](const InstanceData& instance) {
                    // Only merge consecutive geometry, preserving equal-depth draw order.
                    if (batches.empty() || batches.back().mesh != mesh) batches.push_back({mesh, instances.size(), 0});
                    ++batches.back().count; instances.push_back(instance);
                };
                if (source.quads.empty()) append(data);
                else {
                    if (planes.size() > std::numeric_limits<UINT>::max() - source.clipPlanes.size()) throw std::invalid_argument("Too many UI clipping planes");
                    const auto firstPlane = UINT(planes.size());
                    planes.insert(planes.end(), source.clipPlanes.begin(), source.clipPlanes.end());
                    for (const auto& q : source.quads) {
                        data.rect[0] = q.x; data.rect[1] = q.y; data.rect[2] = q.width; data.rect[3] = q.height;
                        data.quadZ = q.z; data.ink[0] = q.inkLow; data.ink[1] = q.inkHigh;
                        data.ink[2] = firstPlane; data.ink[3] = UINT(source.clipPlanes.size());
                        append(data);
                        if (q.inkHigh & 0x80000000u) ++stats_.glyphInstances;
                    }
                }
            }
            if (instances.empty()) continue;
            uploadBuffer(instances.data(), instances.size(), sizeof(InstanceData), D3D11_BIND_VERTEX_BUFFER, instances_, instanceCapacity_);
            if (planes.empty()) planes.push_back({0, 0, 0, 1});
            if (uploadBuffer(planes.data(), planes.size(), sizeof(graphics::ClipPlane), D3D11_BIND_SHADER_RESOURCE, clips_, clipCapacity_)) {
                D3D11_SHADER_RESOURCE_VIEW_DESC desc{}; desc.Format = DXGI_FORMAT_UNKNOWN; desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                desc.Buffer.NumElements = clipCapacity_;
                clipSource_.Reset(); require(device_->CreateShaderResourceView(clips_.Get(), &desc, &clipSource_), "create clipping source");
            }
            auto* clips = clipSource_.Get(); context_->PSSetShaderResources(1, 1, &clips);
            for (const auto& batch : batches) {
                ID3D11Buffer* vertices[]{batch.mesh->vertices.Get(), instances_.Get()};
                const UINT strides[]{sizeof(Vector3), sizeof(InstanceData)}, offsets[]{0, UINT(batch.first * sizeof(InstanceData))};
                context_->IASetVertexBuffers(0, 2, vertices, strides, offsets);
                context_->IASetIndexBuffer(batch.mesh->indices.Get(), DXGI_FORMAT_R32_UINT, 0);
                context_->DrawIndexedInstanced(batch.mesh->count, UINT(batch.count), 0, 0, 0);
                ++stats_.batches;
                stats_.instances += batch.count;
                if (batch.count > 1) ++stats_.instancedBatches;
                stats_.triangles += batch.mesh->count / 3 * batch.count;
                stats_.vertices += batch.mesh->vertexCount * batch.count;
            }
        }
        // New scene/UI snapshots can reuse old geometry before expired source entries are removed.
        for (auto it = meshes_.begin(); it != meshes_.end();) { if (it->second.owner.expired()) it = meshes_.erase(it); else ++it; }
        for (auto it = geometry_.begin(); it != geometry_.end();) {
            auto& bucket = it->second;
            std::erase_if(bucket, [](const auto& mesh) { return mesh.expired(); });
            if (bucket.empty()) it = geometry_.erase(it); else ++it;
        }
    }

    void present(int w, int h) override {
        if (!swap_ || w <= 0 || h <= 0) return;
        if (w != clientWidth_ || h != clientHeight_) resizeSwapChain(w, h);
        auto* target = back_.Get(); context_->OMSetRenderTargets(1, &target, nullptr);
        const float black[]{0, 0, 0, 1}; context_->ClearRenderTargetView(target, black);
        const auto fit = fitViewport(w, h, width_, height_);
        D3D11_VIEWPORT viewport{float(fit.x), float(fit.y), float(fit.width), float(fit.height), 0, 1};
        if (fit.width > 0 && fit.height > 0) {
            context_->RSSetViewports(1, &viewport); context_->RSSetState(raster_.Get());
            context_->OMSetDepthStencilState(noDepth_.Get(), 0); context_->IASetInputLayout(nullptr);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(presentVertex_.Get(), nullptr, 0); context_->PSSetShader(presentPixel_.Get(), nullptr, 0);
            auto* source = source_.Get(); auto* sampler = sampler_.Get();
            context_->PSSetShaderResources(0, 1, &source); context_->PSSetSamplers(0, 1, &sampler); context_->Draw(3, 0);
            source = nullptr; context_->PSSetShaderResources(0, 1, &source);
        }
        require(swap_->Present(Application::vSync ? 1 : 0, 0), "present frame");
    }

    std::vector<std::uint32_t> readPixels() override {
        if (!staging_) {
            D3D11_TEXTURE2D_DESC desc{}; frame_->GetDesc(&desc); desc.BindFlags = 0;
            desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            require(device_->CreateTexture2D(&desc, nullptr, &staging_), "create readback texture");
        }
        context_->CopyResource(staging_.Get(), frame_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped), "read frame");
        struct Unmap { ID3D11DeviceContext* context; ID3D11Resource* resource; ~Unmap() { context->Unmap(resource, 0); } } unmap{context_.Get(), staging_.Get()};
        std::vector<std::uint32_t> pixels(std::size_t(width_) * height_);
        for (int y = 0; y < height_; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
            for (int x = 0; x < width_; ++x) pixels[std::size_t(y) * width_ + x] = row[x] & 0xffffff;
        }
        return pixels;
    }

private:
    struct GpuMesh {
        ComPtr<ID3D11Buffer> vertices, indices;
        std::vector<Vector3> positions;
        std::vector<std::array<std::size_t, 3>> triangles;
        UINT count = 0;
        std::size_t vertexCount = 0;
    };
    struct CachedMesh {
        std::weak_ptr<const Mesh> owner;
        std::shared_ptr<GpuMesh> geometry;
        std::uint64_t revision = 0;
    };
    std::shared_ptr<GpuMesh> meshBuffer(const std::shared_ptr<const Mesh>& mesh) {
        const auto found = meshes_.find(mesh.get());
        if (found != meshes_.end() && !found->second.owner.expired() && found->second.revision == mesh->revision &&
            found->second.geometry->vertexCount == mesh->vertices.size() && found->second.geometry->count == mesh->triangles.size() * 3) return found->second.geometry;
        if (mesh->vertices.size() > std::numeric_limits<UINT>::max() / sizeof(Vector3) ||
            mesh->triangles.size() > std::numeric_limits<UINT>::max() / (3 * sizeof(std::uint32_t)))
            throw std::invalid_argument("Mesh exceeds Direct3D buffer limits");
        std::uint64_t hash = 14695981039346656037ull;
        const auto add = [&](std::uint64_t value) { hash ^= value; hash *= 1099511628211ull; };
        add(mesh->vertices.size()); add(mesh->triangles.size());
        for (auto v : mesh->vertices) { add(std::bit_cast<std::uint32_t>(v.x)); add(std::bit_cast<std::uint32_t>(v.y)); add(std::bit_cast<std::uint32_t>(v.z)); }
        for (auto triangle : mesh->triangles) for (auto index : triangle) add(index);
        auto& bucket = geometry_[hash];
        for (const auto& item : bucket) if (auto shared = item.lock(); shared && shared->triangles == mesh->triangles &&
            shared->positions.size() == mesh->vertices.size() && std::equal(shared->positions.begin(), shared->positions.end(), mesh->vertices.begin(),
                [](Vector3 a, Vector3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; })) {
            meshes_.insert_or_assign(mesh.get(), CachedMesh{mesh, shared, mesh->revision}); return shared;
        }
        std::vector<std::uint32_t> indices; indices.reserve(mesh->triangles.size() * 3);
        for (auto triangle : mesh->triangles) for (auto index : triangle) {
            if (index >= mesh->vertices.size()) throw std::invalid_argument("Mesh index out of bounds");
            indices.push_back(static_cast<std::uint32_t>(index));
        }
        auto result = std::make_shared<GpuMesh>(); result->count = UINT(indices.size()); result->vertexCount = mesh->vertices.size();
        result->positions = mesh->vertices; result->triangles = mesh->triangles;
        D3D11_BUFFER_DESC desc{}; desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.ByteWidth = UINT(mesh->vertices.size() * sizeof(Vector3));
        D3D11_SUBRESOURCE_DATA data{}; data.pSysMem = mesh->vertices.data();
        require(device_->CreateBuffer(&desc, &data, &result->vertices), "upload vertices");
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER; desc.ByteWidth = UINT(indices.size() * sizeof(std::uint32_t)); data.pSysMem = indices.data();
        require(device_->CreateBuffer(&desc, &data, &result->indices), "upload indices");
        ++stats_.meshUploads;
        bucket.push_back(result); meshes_.insert_or_assign(mesh.get(), CachedMesh{mesh, result, mesh->revision}); return result;
    }
    bool uploadBuffer(const void* data, std::size_t count, UINT stride, UINT bind, ComPtr<ID3D11Buffer>& buffer, UINT& capacity) {
        const auto maximum = std::numeric_limits<UINT>::max() / stride;
        if (count > maximum) throw std::invalid_argument("Instance data exceeds Direct3D buffer limits");
        const bool grow = count > capacity;
        if (grow) {
            const UINT next = UINT(std::min<std::size_t>(maximum, std::max(count, std::max<std::size_t>(64, std::size_t(capacity) * 2))));
            D3D11_BUFFER_DESC desc{}; desc.ByteWidth = next * stride; desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = bind; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (bind == D3D11_BIND_SHADER_RESOURCE) { desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride = stride; }
            ComPtr<ID3D11Buffer> result; require(device_->CreateBuffer(&desc, nullptr, &result), "create instance/clip buffer");
            buffer = std::move(result); capacity = next;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(context_->Map(buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "upload instance/clip data");
        std::memcpy(mapped.pData, data, count * stride); context_->Unmap(buffer.Get(), 0);
        return grow;
    }
    void createPipeline() {
        auto vs = compile(shaders::scene, "VS", "vs_5_0"), ps = compile(shaders::scene, "PS", "ps_5_0");
        require(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex_), "create vertex shader");
        require(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel_), "create pixel shader");
        const D3D11_INPUT_ELEMENT_DESC elements[]{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(InstanceData, color), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"RECT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, offsetof(InstanceData, rect), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"INK", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, offsetof(InstanceData, ink), D3D11_INPUT_PER_INSTANCE_DATA, 1},
            {"QUADZ", 0, DXGI_FORMAT_R32_FLOAT, 1, offsetof(InstanceData, quadZ), D3D11_INPUT_PER_INSTANCE_DATA, 1}};
        require(device_->CreateInputLayout(elements, UINT(std::size(elements)), vs->GetBufferPointer(), vs->GetBufferSize(), &layout_), "create instance input layout");
        vs = compile(shaders::present, "VS", "vs_5_0"); ps = compile(shaders::present, "PS", "ps_5_0");
        require(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &presentVertex_), "create presentation vertex shader");
        require(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &presentPixel_), "create presentation pixel shader");
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth = sizeof(Matrix); buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        require(device_->CreateBuffer(&buffer, nullptr, &constants_), "create draw constants");
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_BACK; raster.DepthClipEnable = TRUE;
        require(device_->CreateRasterizerState(&raster, &raster_), "create raster state");
        D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
        require(device_->CreateDepthStencilState(&depth, &depthState_), "create depth state");
        depth.DepthEnable = FALSE; require(device_->CreateDepthStencilState(&depth, &noDepth_), "create presentation depth state");
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD = D3D11_FLOAT32_MAX;
        require(device_->CreateSamplerState(&sampler, &sampler_), "create presentation sampler");
    }
    void createSwapChain() {
        ComPtr<IDXGIDevice> device; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        require(device_.As(&device), "get DXGI device"); require(device->GetAdapter(&adapter), "get display adapter");
        require(adapter->GetParent(IID_PPV_ARGS(&factory)), "get swap chain factory");
        RECT client{}; GetClientRect(window_, &client);
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = UINT(std::max(1L, client.right)); desc.Height = UINT(std::max(1L, client.bottom));
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        require(factory->CreateSwapChainForHwnd(device_.Get(), window_, &desc, nullptr, nullptr, &swap_), "create swap chain");
        require(factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER), "disable DXGI fullscreen shortcut");
        clientWidth_ = int(desc.Width); clientHeight_ = int(desc.Height); createBackView();
    }
    void createBackView() {
        ComPtr<ID3D11Texture2D> back;
        require(swap_->GetBuffer(0, IID_PPV_ARGS(&back)), "get back buffer");
        require(device_->CreateRenderTargetView(back.Get(), nullptr, &back_), "create back buffer view");
    }
    void resizeSwapChain(int w, int h) {
        context_->OMSetRenderTargets(0, nullptr, nullptr); back_.Reset();
        require(swap_->ResizeBuffers(0, UINT(w), UINT(h), DXGI_FORMAT_UNKNOWN, 0), "resize swap chain");
        createBackView(); clientWidth_ = w; clientHeight_ = h;
    }
    HWND window_ = nullptr;
    int width_ = 0, height_ = 0, clientWidth_ = 0, clientHeight_ = 0;
    std::string name_;
    graphics::RenderStats stats_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID3D11Texture2D> frame_, staging_;
    ComPtr<ID3D11RenderTargetView> target_, back_;
    ComPtr<ID3D11ShaderResourceView> source_;
    ComPtr<ID3D11ShaderResourceView> clipSource_;
    ComPtr<ID3D11DepthStencilView> depth_;
    ComPtr<ID3D11VertexShader> vertex_, presentVertex_;
    ComPtr<ID3D11PixelShader> pixel_, presentPixel_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> constants_;
    ComPtr<ID3D11Buffer> instances_, clips_;
    UINT instanceCapacity_ = 0, clipCapacity_ = 0;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> depthState_, noDepth_;
    ComPtr<ID3D11SamplerState> sampler_;
    std::unordered_map<const Mesh*, CachedMesh> meshes_;
    std::unordered_map<std::uint64_t, std::vector<std::weak_ptr<GpuMesh>>> geometry_;
    std::shared_ptr<const Mesh> quadMesh_ = std::make_shared<Mesh>(Mesh{
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 2, 1}, {0, 3, 2}}});
};
}
std::unique_ptr<RenderBackend> createRenderer(void* window, int width, int height, Options options) {
    return std::make_unique<D3D11Renderer>(window, width, height, options);
}
} // namespace tiny3d::d3d11
