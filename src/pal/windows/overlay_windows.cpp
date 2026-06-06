// Windows D3D12/Win32 dev overlay.
//
// Feature-parity counterpart to the Linux Vulkan overlay (overlay_linux.cpp),
// built on the stock imgui_impl_dx12 + imgui_impl_win32 backends. Hooks are
// installed via the shared pal::hook_engine() (MinHook):
//   - IDXGISwapChain::Present / IDXGISwapChain1::Present1  -> composite ImGui
//   - IDXGISwapChain::ResizeBuffers                        -> rebuild RTVs
//   - ID3D12CommandQueue::ExecuteCommandLists             -> capture DIRECT queue
// Input: the window procedure is subclassed (SetWindowLongPtrW) and fed to
// ImGui_ImplWin32_WndProcHandler; the toggle key flips the console and game
// input is swallowed while it is open.

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string_view>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_overlay.hpp"

// Provided by imgui_impl_win32 (also declared in its header; repeated for clarity).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace pal
{

namespace
{

// ---------------------------------------------------------------------------
// Shared state (single-file overlay; detours are free functions over globals,
// mirroring the Linux overlay).
// ---------------------------------------------------------------------------

std::atomic<IOverlayUi *> g_ui{nullptr};
std::atomic<bool> g_console_open{false};
std::atomic<ID3D12CommandQueue *> g_command_queue{nullptr};

// Render resources - touched only on the render thread inside the Present detour.
struct FrameContext
{
    ID3D12CommandAllocator *allocator = nullptr;
    ID3D12Resource *render_target = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle{};
};

ID3D12Device *g_device = nullptr;
ID3D12DescriptorHeap *g_rtv_heap = nullptr;
ID3D12DescriptorHeap *g_srv_heap = nullptr;
ID3D12GraphicsCommandList *g_cmd_list = nullptr;
FrameContext *g_frames = nullptr;
UINT g_frame_count = 0;
HWND g_hwnd = nullptr;
WNDPROC g_orig_wndproc = nullptr;
bool g_imgui_inited = false;
bool g_init_failed = false;
ID3D12Fence *g_fence = nullptr;
UINT64 g_fence_last_signaled = 0;
HANDLE g_fence_event = nullptr;

// Identity of the swapchain init_render bound g_frames to. render_overlay only
// renders when the presented swapchain matches this; a different pointer means a
// full recreate (our cached back buffers belong to a dead swapchain).
IDXGISwapChain3 *g_inited_swap = nullptr;
bool g_swapchain_mismatch_warned = false; // rate-limit the recreate warning

UINT g_toggle_vk = VK_F1;

// Cross-thread input marshalling. The Win32 message pump and D3D12 present run on
// DIFFERENT threads in this game, so the WndProc must not touch ImGui directly
// (it races NewFrame). Instead it queues raw messages here under g_msg_mu; the
// render thread drains + replays them into ImGui before NewFrame. The render
// thread publishes ImGui's capture intent into g_want_* (read one frame in
// arrears by the WndProc to decide what game input to swallow). Mirrors the
// Linux overlay's SDL-event queue.
struct Win32Msg
{
    HWND hwnd;
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
};
std::mutex g_msg_mu;
std::vector<Win32Msg> g_msg_queue; // guarded by g_msg_mu
std::atomic<bool> g_want_keyboard{false};
std::atomic<bool> g_want_mouse{false};

// MTHAP_CONSOLE_KEY env string to a Win32 virtual-key code.
static UINT parse_console_key(const char *name)
{
    if (name == nullptr || name[0] == '\0')
        return VK_F1;
    std::string_view sv{name};
    if (sv == "F1")
        return VK_F1;
    if (sv == "F2")
        return VK_F2;
    if (sv == "F3")
        return VK_F3;
    if (sv == "F4")
        return VK_F4;
    if (sv == "F5")
        return VK_F5;
    if (sv == "F6")
        return VK_F6;
    if (sv == "F7")
        return VK_F7;
    if (sv == "F8")
        return VK_F8;
    if (sv == "F9")
        return VK_F9;
    if (sv == "F10")
        return VK_F10;
    if (sv == "F11")
        return VK_F11;
    if (sv == "F12")
        return VK_F12;
    if (sv == "BACKQUOTE" || sv == "GRAVE" || sv == "`")
        return VK_OEM_3;
    return VK_F1;
}

// ---------------------------------------------------------------------------
// Detour function-pointer types. COM methods take an explicit `this` first.
// ---------------------------------------------------------------------------

using PFN_Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using PFN_ExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);

PFN_Present g_orig_present = nullptr;
PFN_Present1 g_orig_present1 = nullptr;
PFN_ResizeBuffers g_orig_resize = nullptr;
PFN_ExecuteCommandLists g_orig_execute = nullptr;

// IDXGISwapChain(1) and ID3D12CommandQueue vtable indices (after the three
// IUnknown slots). Stable across the D3D12/DXGI ABI.
constexpr unsigned kIdxPresent = 8;          // IDXGISwapChain::Present
constexpr unsigned kIdxResizeBuffers = 13;   // IDXGISwapChain::ResizeBuffers
constexpr unsigned kIdxPresent1 = 22;        // IDXGISwapChain1::Present1
constexpr unsigned kIdxExecuteCmdLists = 10; // ID3D12CommandQueue::ExecuteCommandLists

// Defined below (after vtable discovery); composites ImGui onto the back buffer.
static void render_overlay(IDXGISwapChain3 *swap);
// Defined below; blocks until the last overlay submit completes on the GPU.
static void wait_for_gpu();

HRESULT STDMETHODCALLTYPE repl_present(IDXGISwapChain *self, UINT sync, UINT flags)
{
    IDXGISwapChain3 *swap3 = nullptr;
    if (SUCCEEDED(self->QueryInterface(IID_PPV_ARGS(&swap3))))
    {
        render_overlay(swap3);
        swap3->Release();
    }
    return g_orig_present(self, sync, flags);
}

HRESULT STDMETHODCALLTYPE repl_present1(IDXGISwapChain1 *self, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS *params)
{
    IDXGISwapChain3 *swap3 = nullptr;
    if (SUCCEEDED(self->QueryInterface(IID_PPV_ARGS(&swap3))))
    {
        render_overlay(swap3);
        swap3->Release();
    }
    return g_orig_present1(self, sync, flags, params);
}

HRESULT STDMETHODCALLTYPE repl_resize(IDXGISwapChain *self, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
{
    // Release our back-buffer references + RTVs so ResizeBuffers can succeed.
    if (g_imgui_inited && g_frames != nullptr)
    {
        wait_for_gpu(); // overlay submits referencing these back buffers must finish first
        for (UINT i = 0; i < g_frame_count; ++i)
        {
            if (g_frames[i].render_target != nullptr)
            {
                g_frames[i].render_target->Release();
                g_frames[i].render_target = nullptr;
            }
        }
    }

    const HRESULT hr = g_orig_resize(self, count, w, h, fmt, flags);

    // Rebuild RTVs against the new back buffers. A changed buffer count would
    // invalidate our fixed-size heap/frame array; bail to inert in that case.
    if (g_imgui_inited && SUCCEEDED(hr))
    {
        IDXGISwapChain3 *swap3 = nullptr;
        if (SUCCEEDED(self->QueryInterface(IID_PPV_ARGS(&swap3))))
        {
            DXGI_SWAP_CHAIN_DESC desc{};
            swap3->GetDesc(&desc);
            if (desc.BufferCount != g_frame_count)
            {
                pal::logf(pal::LogLevel::Warn, "overlay: ResizeBuffers changed buffer count %u->%u; overlay inert", g_frame_count, desc.BufferCount);
                g_init_failed = true;
            }
            else
            {
                const UINT rtv_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
                for (UINT i = 0; i < g_frame_count; ++i)
                {
                    if (FAILED(swap3->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].render_target))))
                    {
                        // Leaving a null back buffer would crash render_overlay; bail inert
                        // (parity with init_render's GetBuffer failure path).
                        pal::logf(pal::LogLevel::Error, "overlay: GetBuffer %u failed after resize; overlay inert", i);
                        g_init_failed = true;
                        break;
                    }
                    g_frames[i].rtv_handle.ptr = rtv_cpu.ptr + static_cast<SIZE_T>(i) * rtv_size;
                    g_device->CreateRenderTargetView(g_frames[i].render_target, nullptr, g_frames[i].rtv_handle);
                }
            }
            swap3->Release();
        }
    }

    return hr;
}

void STDMETHODCALLTYPE repl_execute(ID3D12CommandQueue *self, UINT n, ID3D12CommandList *const *lists)
{
    if (g_command_queue.load(std::memory_order_acquire) == nullptr)
    {
        const D3D12_COMMAND_QUEUE_DESC desc = self->GetDesc();
        if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            g_command_queue.store(self, std::memory_order_release);
            pal::logf(pal::LogLevel::Info, "overlay: captured DIRECT command queue %p", static_cast<void *>(self));
        }
    }
    g_orig_execute(self, n, lists);
}

// Read the swapchain + command-queue vtable addresses by standing up throwaway
// D3D12 objects (vtables are shared across all instances of a type). Fills
// out[0..3] = {Present, Present1, ResizeBuffers, ExecuteCommandLists}.
static bool discover_vtable_addrs(void *out[4])
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MthapOverlayDummy";
    RegisterClassExW(&wc);
    HWND dummy = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

    ID3D12Device *device = nullptr;
    ID3D12CommandQueue *queue = nullptr;
    IDXGIFactory4 *factory = nullptr;
    IDXGISwapChain1 *swapchain = nullptr;
    bool ok = false;

    if (dummy == nullptr)
    {
        pal::logf(pal::LogLevel::Error, "overlay: dummy window creation failed");
        goto cleanup;
    }
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: D3D12CreateDevice failed");
        goto cleanup;
    }

    {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue))))
        {
            pal::logf(pal::LogLevel::Error, "overlay: CreateCommandQueue failed");
            goto cleanup;
        }
    }
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: CreateDXGIFactory1 failed");
        goto cleanup;
    }

    {
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.BufferCount = 2;
        scd.Width = 64;
        scd.Height = 64;
        scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SampleDesc.Count = 1;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChainForHwnd(queue, dummy, &scd, nullptr, nullptr, &swapchain)))
        {
            pal::logf(pal::LogLevel::Error, "overlay: CreateSwapChainForHwnd failed");
            goto cleanup;
        }
    }

    {
        void **sc_vtbl = *reinterpret_cast<void ***>(swapchain);
        void **q_vtbl = *reinterpret_cast<void ***>(queue);
        out[0] = sc_vtbl[kIdxPresent];
        out[1] = sc_vtbl[kIdxPresent1];
        out[2] = sc_vtbl[kIdxResizeBuffers];
        out[3] = q_vtbl[kIdxExecuteCmdLists];
        ok = true;
    }

cleanup:
    if (swapchain != nullptr)
        swapchain->Release();
    if (factory != nullptr)
        factory->Release();
    if (queue != nullptr)
        queue->Release();
    if (device != nullptr)
        device->Release();
    if (dummy != nullptr)
        DestroyWindow(dummy);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

// Block until the GPU has finished the most recent overlay submission. The overlay
// reuses a single command list + per-frame allocators, so the previous submit must
// complete before they are reset or released. The overlay's GPU workload is tiny,
// so fully serializing here is cheap.
static void wait_for_gpu()
{
    if (g_fence == nullptr || g_fence_last_signaled == 0)
        return;
    if (g_fence->GetCompletedValue() < g_fence_last_signaled)
    {
        g_fence->SetEventOnCompletion(g_fence_last_signaled, g_fence_event);
        WaitForSingleObject(g_fence_event, INFINITE);
    }
}

static void destroy_render_resources()
{
    if (g_frames != nullptr)
    {
        for (UINT i = 0; i < g_frame_count; ++i)
        {
            if (g_frames[i].render_target != nullptr)
                g_frames[i].render_target->Release();
            if (g_frames[i].allocator != nullptr)
                g_frames[i].allocator->Release();
        }
        delete[] g_frames;
        g_frames = nullptr;
    }
    if (g_cmd_list != nullptr)
    {
        g_cmd_list->Release();
        g_cmd_list = nullptr;
    }
    if (g_srv_heap != nullptr)
    {
        g_srv_heap->Release();
        g_srv_heap = nullptr;
    }
    if (g_rtv_heap != nullptr)
    {
        g_rtv_heap->Release();
        g_rtv_heap = nullptr;
    }
    if (g_device != nullptr)
    {
        g_device->Release();
        g_device = nullptr;
    }
    if (g_fence != nullptr)
    {
        g_fence->Release();
        g_fence = nullptr;
    }
    if (g_fence_event != nullptr)
    {
        CloseHandle(g_fence_event);
        g_fence_event = nullptr;
    }
    g_fence_last_signaled = 0;
    g_frame_count = 0;
}

static bool is_keyboard_msg(UINT msg)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
        return true;
    default:
        return false;
    }
}

static bool is_mouse_msg(UINT msg)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    default:
        return false;
    }
}

// Messages worth forwarding to ImGui_ImplWin32_WndProcHandler (input + focus).
static bool is_imgui_msg(UINT msg)
{
    if (is_keyboard_msg(msg) || is_mouse_msg(msg))
        return true;
    switch (msg)
    {
    case WM_NCMOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_SETCURSOR:
        return true;
    default:
        return false;
    }
}

static LRESULT CALLBACK overlay_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_KEYDOWN && wparam == g_toggle_vk)
    {
        g_console_open.store(!g_console_open.load(std::memory_order_relaxed), std::memory_order_relaxed);
        return 0; // consume the toggle key itself (atomic only; no ImGui touch)
    }

    // Do NOT call ImGui from this thread: the message pump and the present thread
    // are different, and ImGui's context/IO is not thread-safe. Queue the message
    // for the render thread to replay before NewFrame.
    if (is_imgui_msg(msg))
    {
        {
            const std::lock_guard<std::mutex> lk(g_msg_mu);
            g_msg_queue.push_back(Win32Msg{hwnd, msg, wparam, lparam});
        }

        // Swallow from the game while the console is open and ImGui wants this
        // input. WantCapture* is published by the render thread (one frame stale).
        if (g_console_open.load(std::memory_order_relaxed))
        {
            if (g_want_keyboard.load(std::memory_order_relaxed) && is_keyboard_msg(msg))
                return 0;
            if (g_want_mouse.load(std::memory_order_relaxed) && is_mouse_msg(msg))
                return 0;
        }
    }

    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wparam, lparam);
}

// One-time render init. Returns false either because the queue is not captured
// yet (retry next frame) or because a hard failure occurred (g_init_failed set).
static bool init_render(IDXGISwapChain3 *swap)
{
    ID3D12CommandQueue *queue = g_command_queue.load(std::memory_order_acquire);
    if (queue == nullptr)
        return false; // not ready; retry on a later frame

    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&g_device))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: queue->GetDevice failed; overlay inert");
        g_init_failed = true;
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    swap->GetDesc(&desc);
    g_frame_count = desc.BufferCount;
    g_hwnd = desc.OutputWindow;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = g_frame_count;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g_device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g_rtv_heap))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: RTV heap creation failed; overlay inert");
        g_init_failed = true;
        destroy_render_resources();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 1;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g_srv_heap))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: SRV heap creation failed; overlay inert");
        g_init_failed = true;
        destroy_render_resources();
        return false;
    }

    const UINT rtv_size = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_cpu = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();

    g_frames = new FrameContext[g_frame_count];
    for (UINT i = 0; i < g_frame_count; ++i)
    {
        if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frames[i].allocator))))
        {
            pal::logf(pal::LogLevel::Error, "overlay: command allocator %u failed; overlay inert", i);
            g_init_failed = true;
            destroy_render_resources();
            return false;
        }
        if (FAILED(swap->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].render_target))))
        {
            pal::logf(pal::LogLevel::Error, "overlay: GetBuffer %u failed; overlay inert", i);
            g_init_failed = true;
            destroy_render_resources();
            return false;
        }
        g_frames[i].rtv_handle.ptr = rtv_cpu.ptr + static_cast<SIZE_T>(i) * rtv_size;
        g_device->CreateRenderTargetView(g_frames[i].render_target, nullptr, g_frames[i].rtv_handle);
    }

    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr, IID_PPV_ARGS(&g_cmd_list))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: CreateCommandList failed; overlay inert");
        g_init_failed = true;
        destroy_render_resources();
        return false;
    }
    g_cmd_list->Close();

    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
    {
        pal::logf(pal::LogLevel::Error, "overlay: CreateFence failed; overlay inert");
        g_init_failed = true;
        destroy_render_resources();
        return false;
    }
    g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_fence_event == nullptr)
    {
        pal::logf(pal::LogLevel::Error, "overlay: CreateEvent failed; overlay inert");
        g_init_failed = true;
        destroy_render_resources();
        return false;
    }
    g_fence_last_signaled = 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // do not write imgui.ini next to the game
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frame_count), desc.BufferDesc.Format, g_srv_heap, g_srv_heap->GetCPUDescriptorHandleForHeapStart(),
                        g_srv_heap->GetGPUDescriptorHandleForHeapStart());

    g_orig_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&overlay_wndproc)));
    pal::logf(pal::LogLevel::Info, "overlay: WndProc subclassed (orig=%p)", reinterpret_cast<void *>(g_orig_wndproc));

    g_inited_swap = swap;

    g_imgui_inited = true;
    pal::logf(pal::LogLevel::Info, "overlay: render init complete (%u frames, hwnd=%p)", g_frame_count, static_cast<void *>(g_hwnd));
    return true;
}

static void render_overlay(IDXGISwapChain3 *swap)
{
    if (g_init_failed)
        return;
    if (!g_imgui_inited && !init_render(swap))
        return;

    // Drain the WndProc message queue on the render thread (always, to bound its
    // growth). Replayed into ImGui only when we actually render a frame below.
    std::vector<Win32Msg> input;
    {
        const std::lock_guard<std::mutex> lk(g_msg_mu);
        input.swap(g_msg_queue);
    }

    // Our g_frames back buffers + RTVs belong to g_inited_swap; if the game recreated
    // the swapchain (different pointer) they are stale, so skip rather than render
    // into freed targets. Warn once so a recreate is visible in the log.
    if (swap != g_inited_swap)
    {
        if (!g_swapchain_mismatch_warned)
        {
            pal::logf(pal::LogLevel::Warn, "overlay: presented swapchain differs from the inited one; overlay disabled for it");
            g_swapchain_mismatch_warned = true;
        }
        return;
    }

    const UINT idx = swap->GetCurrentBackBufferIndex();
    if (idx >= g_frame_count)
    {
        pal::logf(pal::LogLevel::Error, "overlay: back-buffer idx %u >= frame_count %u; skipping frame", idx, g_frame_count);
        return;
    }

    // Replay queued WndProc input into ImGui (render thread only), before NewFrame
    // consumes the event queue.
    for (const Win32Msg &m : input)
        ImGui_ImplWin32_WndProcHandler(m.hwnd, m.msg, m.wparam, m.lparam);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const bool console_open = g_console_open.load(std::memory_order_relaxed);
    // The game hides the OS cursor, so draw ImGui's own software cursor while the
    // console is open; otherwise the user has no visible pointer to click with.
    ImGui::GetIO().MouseDrawCursor = console_open;

    if (IOverlayUi *ui = g_ui.load(std::memory_order_acquire); ui != nullptr)
        ui->draw(console_open);

    ImGui::Render();

    // Publish ImGui's capture intent for the WndProc thread's swallow decision.
    const ImGuiIO &io = ImGui::GetIO();
    g_want_keyboard.store(io.WantCaptureKeyboard, std::memory_order_relaxed);
    g_want_mouse.store(io.WantCaptureMouse, std::memory_order_relaxed);

    FrameContext &fc = g_frames[idx];

    wait_for_gpu(); // previous overlay submit must finish before reusing the list + allocator
    fc.allocator->Reset();
    g_cmd_list->Reset(fc.allocator, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = fc.render_target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_cmd_list->ResourceBarrier(1, &barrier);

    g_cmd_list->OMSetRenderTargets(1, &fc.rtv_handle, FALSE, nullptr);
    g_cmd_list->SetDescriptorHeaps(1, &g_srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd_list);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmd_list->ResourceBarrier(1, &barrier);
    g_cmd_list->Close();

    ID3D12CommandQueue *queue = g_command_queue.load(std::memory_order_acquire);
    ID3D12CommandList *lists[] = {g_cmd_list};
    g_orig_execute(queue, 1, lists);
    queue->Signal(g_fence, ++g_fence_last_signaled);
}

// ---------------------------------------------------------------------------
// Overlay object.
// ---------------------------------------------------------------------------

class D3D12Overlay final : public IOverlay
{
  public:
    D3D12Overlay()
    {
        g_toggle_vk = parse_console_key(std::getenv("MTHAP_CONSOLE_KEY"));
        pal::logf(pal::LogLevel::Info, "overlay: D3D12Overlay constructing (toggle vk=0x%x)", static_cast<unsigned>(g_toggle_vk));

        void *addrs[4];
        if (!discover_vtable_addrs(addrs))
        {
            pal::logf(pal::LogLevel::Error, "overlay: vtable discovery failed; overlay inert");
            return;
        }

        struct Spec
        {
            void *target;
            void *replacement;
            void **trampoline;
            const char *name;
        };
        const Spec specs[kHookCount] = {
            {addrs[0], reinterpret_cast<void *>(&repl_present), reinterpret_cast<void **>(&g_orig_present), "Present"},
            {addrs[1], reinterpret_cast<void *>(&repl_present1), reinterpret_cast<void **>(&g_orig_present1), "Present1"},
            {addrs[2], reinterpret_cast<void *>(&repl_resize), reinterpret_cast<void **>(&g_orig_resize), "ResizeBuffers"},
            {addrs[3], reinterpret_cast<void *>(&repl_execute), reinterpret_cast<void **>(&g_orig_execute), "ExecuteCommandLists"},
        };

        for (std::size_t i = 0; i < kHookCount; ++i)
        {
            HookId id = pal::hook_engine().install_hook(specs[i].target, specs[i].replacement, specs[i].trampoline);
            if (id == pal::kInvalidHookId)
            {
                pal::logf(pal::LogLevel::Error, "overlay: failed to hook %s at %p", specs[i].name, specs[i].target);
            }
            else
            {
                ids_[installed_++] = id;
                pal::logf(pal::LogLevel::Info, "overlay: hooked %s at %p (id=%llu)", specs[i].name, specs[i].target, static_cast<unsigned long long>(id));
            }
        }
    }

    ~D3D12Overlay() override
    {
        // Relies on the render/message threads being quiesced at teardown (the App
        // is destroyed at process shutdown). MinHook does not drain in-flight
        // detours, so a present racing this dtor would touch freed GPU state.
        for (std::size_t i = 0; i < installed_; ++i)
            pal::hook_engine().remove_hook(ids_[i]);
        installed_ = 0;
        g_orig_present = nullptr;
        g_orig_present1 = nullptr;
        g_orig_resize = nullptr;
        g_orig_execute = nullptr;

        if (g_orig_wndproc != nullptr && g_hwnd != nullptr)
        {
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_orig_wndproc));
            g_orig_wndproc = nullptr;
        }

        if (g_imgui_inited)
        {
            wait_for_gpu(); // drain the last overlay submit before releasing GPU objects
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_imgui_inited = false;
        }
        destroy_render_resources();

        // Return the scattered globals to a clean slate so a fresh overlay (were
        // one ever constructed) re-inits correctly rather than inheriting a stale
        // inert flag or a released queue pointer.
        g_init_failed = false;
        g_command_queue.store(nullptr, std::memory_order_release);
        g_console_open.store(false, std::memory_order_relaxed);
        g_ui.store(nullptr, std::memory_order_release);
        pal::logf(pal::LogLevel::Info, "overlay: D3D12Overlay destroyed");
    }

    void set_ui(IOverlayUi *ui) override
    {
        g_ui.store(ui, std::memory_order_release);
    }

  private:
    static constexpr std::size_t kHookCount = 4;
    HookId ids_[kHookCount]{};
    std::size_t installed_ = 0;
};

} // namespace

std::unique_ptr<IOverlay> make_overlay(const OverlayConfig &)
{
    return std::make_unique<D3D12Overlay>();
}

} // namespace pal
