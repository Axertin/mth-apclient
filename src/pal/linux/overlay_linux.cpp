// Linux Vulkan/SDL dev overlay.
//
// We do NOT link against libvulkan at build time: this TU defines VK_NO_PROTOTYPES
// (matching the imgui backend's IMGUI_IMPL_VULKAN_NO_PROTOTYPES) and resolves every
// Vulkan entry point at runtime from the loader the game already opened.
//
// Strategy:
//   - dlopen("libvulkan.so.1", RTLD_NOLOAD) to grab the already-loaded loader.
//   - Hook the loader trampolines the game calls: vkCreateInstance / vkCreateDevice /
//     vkCreateSwapchainKHR / vkQueuePresentKHR (dlsym from the loader handle).
//   - Capture instance/device/physical-device/queue/swapchain state from those hooks.
//   - On each present: lazily init ImGui-on-Vulkan, draw a frame into the just-
//     rendered swapchain image (loadOp=LOAD, present->present layout), and chain
//     present after our overlay submit using the game's own wait semaphores.
//
// SDL input:
//   - Hook ProcessSDLEvent(SDL_Event&) (a game function; addr from OverlayConfig).
//     main runs `while(!done){ SDL_WaitEvent(&e); ProcessSDLEvent(e); }`, so every
//     event flows through it. (SDL_PollEvent is modal-only; the game's own
//     SDL_PeepEvents calls are all ADDEVENT, so neither is the input path.)
//   - Toggle console visibility with F1 (overridable via MTHAP_CONSOLE_KEY).
//   - While the console is open, swallow input by NOT forwarding to the original
//     ProcessSDLEvent, and capture it for ImGui instead.
//
// Threading model note: ProcessSDLEvent runs on the main (event) thread, while
// vkQueuePresentKHR runs on a separate render thread. ImGui is NOT thread-safe, so
// the input hook must not touch ImGui: it only flips g_console_open (atomic) and
// pushes captured events into g_input_queue (mutex-guarded); the present hook drains
// that queue into ImGui IO right before NewFrame, keeping every ImGui call on the
// render thread.
//
// Everything degrades gracefully: any missing piece of captured state means we
// forward the present untouched and never crash.

#define VK_NO_PROTOTYPES
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>

#include <SDL_events.h>
#include <SDL_keycode.h>
#include <SDL_mouse.h>
#include <dlfcn.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include "pal/pal_hook.hpp"
#include "pal/pal_log.hpp"
#include "pal/pal_overlay.hpp"

namespace pal
{

namespace
{

// ---------------------------------------------------------------------------
// Resolved Vulkan entry points.
// ---------------------------------------------------------------------------

// Global / instance loader.
PFN_vkGetInstanceProcAddr g_vkGetInstanceProcAddr = nullptr;
PFN_vkGetDeviceProcAddr g_vkGetDeviceProcAddr = nullptr;

// Device-level functions we CALL (resolved via vkGetDeviceProcAddr once the
// device is known).
PFN_vkGetDeviceQueue g_vkGetDeviceQueue = nullptr;
PFN_vkGetSwapchainImagesKHR g_vkGetSwapchainImagesKHR = nullptr;
PFN_vkCreateImageView g_vkCreateImageView = nullptr;
PFN_vkDestroyImageView g_vkDestroyImageView = nullptr;
PFN_vkCreateRenderPass g_vkCreateRenderPass = nullptr;
PFN_vkDestroyRenderPass g_vkDestroyRenderPass = nullptr;
PFN_vkCreateFramebuffer g_vkCreateFramebuffer = nullptr;
PFN_vkDestroyFramebuffer g_vkDestroyFramebuffer = nullptr;
PFN_vkCreateDescriptorPool g_vkCreateDescriptorPool = nullptr;
PFN_vkDestroyDescriptorPool g_vkDestroyDescriptorPool = nullptr;
PFN_vkCreateCommandPool g_vkCreateCommandPool = nullptr;
PFN_vkDestroyCommandPool g_vkDestroyCommandPool = nullptr;
PFN_vkAllocateCommandBuffers g_vkAllocateCommandBuffers = nullptr;
PFN_vkResetCommandBuffer g_vkResetCommandBuffer = nullptr;
PFN_vkBeginCommandBuffer g_vkBeginCommandBuffer = nullptr;
PFN_vkEndCommandBuffer g_vkEndCommandBuffer = nullptr;
PFN_vkCmdBeginRenderPass g_vkCmdBeginRenderPass = nullptr;
PFN_vkCmdEndRenderPass g_vkCmdEndRenderPass = nullptr;
PFN_vkQueueSubmit g_vkQueueSubmit = nullptr;
PFN_vkDeviceWaitIdle g_vkDeviceWaitIdle = nullptr;
PFN_vkCreateSemaphore g_vkCreateSemaphore = nullptr;
PFN_vkDestroySemaphore g_vkDestroySemaphore = nullptr;
PFN_vkCreateFence g_vkCreateFence = nullptr;
PFN_vkDestroyFence g_vkDestroyFence = nullptr;
PFN_vkWaitForFences g_vkWaitForFences = nullptr;
PFN_vkResetFences g_vkResetFences = nullptr;

// Originals of the functions we HOOK (trampolines installed by the hook engine).
PFN_vkCreateInstance g_orig_create_instance = nullptr;
PFN_vkCreateDevice g_orig_create_device = nullptr;
PFN_vkCreateSwapchainKHR g_orig_create_swapchain = nullptr;
PFN_vkQueuePresentKHR g_orig_present = nullptr;

// ---------------------------------------------------------------------------
// Captured Vulkan state.
// ---------------------------------------------------------------------------

struct VulkanState
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    bool queue_family_known = false;
    VkQueue queue = VK_NULL_HANDLE;

    // Swapchain-derived resources (rebuilt on every vkCreateSwapchainKHR).
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::uint32_t min_image_count = 0;
    std::uint32_t image_count = 0;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;

    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkSemaphore> render_finished; // one per swapchain image
    std::vector<VkFence> in_flight;           // one per swapchain image; gates cmd-buffer reuse

    // Publication gate (acquire/release): set true with memory_order_release ONLY
    // after ALL swapchain resources above are fully built, so the present thread that
    // loads it with memory_order_acquire sees fully-constructed (non-atomic) handles.
    std::atomic<bool> resources_ready{false};
};

VulkanState g_state;

// Threading model: the capture hooks (vkCreateDevice/vkCreateSwapchainKHR) and the
// present detour can run on different threads during startup, but swapchain
// (re)creation and presentation are NOT expected to run concurrently for the SAME
// resources on this engine (a typical single render-thread). The atomic readiness
// gate (g_state.resources_ready) publishes/visibilities the non-atomic handle writes
// across threads via acquire/release; destroy_swapchain_resources() additionally does
// vkDeviceWaitIdle before destroying. We deliberately do NOT take a lock across the
// GPU submit / forwarded present to avoid deadlock.

// Set in init_imgui (first present) and read/reset across the swapchain rebuild path;
// atomic with acquire/release so visibility is consistent across threads.
std::atomic<bool> g_imgui_inited{false};

// The content sink. Written from the worker thread (set_ui), read from the render
// thread (present detour). Aligned pointer => atomic load/store is correct.
std::atomic<IOverlayUi *> g_ui{nullptr};

// One-time "skipping render, state incomplete" warning so we don't spam the log
// every frame.
bool g_warned_incomplete = false;

// ---------------------------------------------------------------------------
// SDL input state (file-scope, used by repl_process_sdl_event and the present hook).
// ---------------------------------------------------------------------------

// The game processes every event through ProcessSDLEvent(SDL_Event&), called by
// main's SDL_WaitEvent loop (and the window-modal filter). That is what we hook;
// SDL_PollEvent/SDL_PeepEvents are not on the game's input path.
using PFN_ProcessSDLEvent = void (*)(SDL_Event *);
PFN_ProcessSDLEvent g_orig_process = nullptr;

// Atomic: the toggle is flipped in the SDL hook (which may run on the game thread)
// and read in the present hook (render thread) draw gate.
std::atomic<bool> g_console_open{false};

// Cross-thread input hand-off. SDL_PeepEvents may run on a different thread than
// vkQueuePresentKHR (the engine has a ycGameThread), and ImGui is NOT thread-safe.
// So the SDL hook only CAPTURES events into this queue; the present hook drains it
// into ImGui IO right before NewFrame, keeping all ImGui access on the render thread.
std::mutex g_input_mu;
std::vector<SDL_Event> g_input_queue; // guarded by g_input_mu

// Toggle key; resolved once in the VulkanOverlay ctor.
SDL_Keycode g_toggle_key = SDLK_F1;

// ---------------------------------------------------------------------------
// Key-name helper: env string -> SDL_Keycode for MTHAP_CONSOLE_KEY override.
// ---------------------------------------------------------------------------

static SDL_Keycode parse_console_key(const char *name)
{
    if (name == nullptr || name[0] == '\0')
        return SDLK_F1;
    std::string_view sv{name};
    if (sv == "F1")
        return SDLK_F1;
    if (sv == "F2")
        return SDLK_F2;
    if (sv == "F3")
        return SDLK_F3;
    if (sv == "F4")
        return SDLK_F4;
    if (sv == "F5")
        return SDLK_F5;
    if (sv == "F6")
        return SDLK_F6;
    if (sv == "F7")
        return SDLK_F7;
    if (sv == "F8")
        return SDLK_F8;
    if (sv == "F9")
        return SDLK_F9;
    if (sv == "F10")
        return SDLK_F10;
    if (sv == "F11")
        return SDLK_F11;
    if (sv == "F12")
        return SDLK_F12;
    if (sv == "BACKQUOTE" || sv == "GRAVE" || sv == "`")
        return SDLK_BACKQUOTE;
    return SDLK_F1;
}

// ---------------------------------------------------------------------------
// SDL_Keycode -> ImGuiKey translation (no SDL linkage — headers only).
// ---------------------------------------------------------------------------

static ImGuiKey sdl_keycode_to_imgui(SDL_Keycode key)
{
    switch (key)
    {
    // Navigation / editing
    case SDLK_TAB:
        return ImGuiKey_Tab;
    case SDLK_LEFT:
        return ImGuiKey_LeftArrow;
    case SDLK_RIGHT:
        return ImGuiKey_RightArrow;
    case SDLK_UP:
        return ImGuiKey_UpArrow;
    case SDLK_DOWN:
        return ImGuiKey_DownArrow;
    case SDLK_PAGEUP:
        return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN:
        return ImGuiKey_PageDown;
    case SDLK_HOME:
        return ImGuiKey_Home;
    case SDLK_END:
        return ImGuiKey_End;
    case SDLK_INSERT:
        return ImGuiKey_Insert;
    case SDLK_DELETE:
        return ImGuiKey_Delete;
    case SDLK_BACKSPACE:
        return ImGuiKey_Backspace;
    case SDLK_SPACE:
        return ImGuiKey_Space;
    case SDLK_RETURN:
        return ImGuiKey_Enter;
    case SDLK_ESCAPE:
        return ImGuiKey_Escape;
    // Modifiers
    case SDLK_LCTRL:
        return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL:
        return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT:
        return ImGuiKey_LeftShift;
    case SDLK_RSHIFT:
        return ImGuiKey_RightShift;
    case SDLK_LALT:
        return ImGuiKey_LeftAlt;
    case SDLK_RALT:
        return ImGuiKey_RightAlt;
    case SDLK_LGUI:
        return ImGuiKey_LeftSuper;
    case SDLK_RGUI:
        return ImGuiKey_RightSuper;
    // Letters A-Z (SDL keycodes == ASCII lowercase)
    case SDLK_a:
        return ImGuiKey_A;
    case SDLK_b:
        return ImGuiKey_B;
    case SDLK_c:
        return ImGuiKey_C;
    case SDLK_d:
        return ImGuiKey_D;
    case SDLK_e:
        return ImGuiKey_E;
    case SDLK_f:
        return ImGuiKey_F;
    case SDLK_g:
        return ImGuiKey_G;
    case SDLK_h:
        return ImGuiKey_H;
    case SDLK_i:
        return ImGuiKey_I;
    case SDLK_j:
        return ImGuiKey_J;
    case SDLK_k:
        return ImGuiKey_K;
    case SDLK_l:
        return ImGuiKey_L;
    case SDLK_m:
        return ImGuiKey_M;
    case SDLK_n:
        return ImGuiKey_N;
    case SDLK_o:
        return ImGuiKey_O;
    case SDLK_p:
        return ImGuiKey_P;
    case SDLK_q:
        return ImGuiKey_Q;
    case SDLK_r:
        return ImGuiKey_R;
    case SDLK_s:
        return ImGuiKey_S;
    case SDLK_t:
        return ImGuiKey_T;
    case SDLK_u:
        return ImGuiKey_U;
    case SDLK_v:
        return ImGuiKey_V;
    case SDLK_w:
        return ImGuiKey_W;
    case SDLK_x:
        return ImGuiKey_X;
    case SDLK_y:
        return ImGuiKey_Y;
    case SDLK_z:
        return ImGuiKey_Z;
    // Digits 0-9
    case SDLK_0:
        return ImGuiKey_0;
    case SDLK_1:
        return ImGuiKey_1;
    case SDLK_2:
        return ImGuiKey_2;
    case SDLK_3:
        return ImGuiKey_3;
    case SDLK_4:
        return ImGuiKey_4;
    case SDLK_5:
        return ImGuiKey_5;
    case SDLK_6:
        return ImGuiKey_6;
    case SDLK_7:
        return ImGuiKey_7;
    case SDLK_8:
        return ImGuiKey_8;
    case SDLK_9:
        return ImGuiKey_9;
    // Function keys
    case SDLK_F1:
        return ImGuiKey_F1;
    case SDLK_F2:
        return ImGuiKey_F2;
    case SDLK_F3:
        return ImGuiKey_F3;
    case SDLK_F4:
        return ImGuiKey_F4;
    case SDLK_F5:
        return ImGuiKey_F5;
    case SDLK_F6:
        return ImGuiKey_F6;
    case SDLK_F7:
        return ImGuiKey_F7;
    case SDLK_F8:
        return ImGuiKey_F8;
    case SDLK_F9:
        return ImGuiKey_F9;
    case SDLK_F10:
        return ImGuiKey_F10;
    case SDLK_F11:
        return ImGuiKey_F11;
    case SDLK_F12:
        return ImGuiKey_F12;
    // Punctuation used in server addresses and common console input
    case SDLK_MINUS:
        return ImGuiKey_Minus;
    case SDLK_EQUALS:
        return ImGuiKey_Equal;
    case SDLK_LEFTBRACKET:
        return ImGuiKey_LeftBracket;
    case SDLK_RIGHTBRACKET:
        return ImGuiKey_RightBracket;
    case SDLK_BACKSLASH:
        return ImGuiKey_Backslash;
    case SDLK_SEMICOLON:
        return ImGuiKey_Semicolon;
    case SDLK_QUOTE:
        return ImGuiKey_Apostrophe;
    case SDLK_COMMA:
        return ImGuiKey_Comma;
    case SDLK_PERIOD:
        return ImGuiKey_Period;
    case SDLK_SLASH:
        return ImGuiKey_Slash;
    case SDLK_BACKQUOTE:
        return ImGuiKey_GraveAccent;
    // Keypad
    case SDLK_KP_0:
        return ImGuiKey_Keypad0;
    case SDLK_KP_1:
        return ImGuiKey_Keypad1;
    case SDLK_KP_2:
        return ImGuiKey_Keypad2;
    case SDLK_KP_3:
        return ImGuiKey_Keypad3;
    case SDLK_KP_4:
        return ImGuiKey_Keypad4;
    case SDLK_KP_5:
        return ImGuiKey_Keypad5;
    case SDLK_KP_6:
        return ImGuiKey_Keypad6;
    case SDLK_KP_7:
        return ImGuiKey_Keypad7;
    case SDLK_KP_8:
        return ImGuiKey_Keypad8;
    case SDLK_KP_9:
        return ImGuiKey_Keypad9;
    case SDLK_KP_PERIOD:
        return ImGuiKey_KeypadDecimal;
    case SDLK_KP_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case SDLK_KP_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case SDLK_KP_MINUS:
        return ImGuiKey_KeypadSubtract;
    case SDLK_KP_PLUS:
        return ImGuiKey_KeypadAdd;
    case SDLK_KP_ENTER:
        return ImGuiKey_KeypadEnter;
    case SDLK_KP_EQUALS:
        return ImGuiKey_KeypadEqual;
    default:
        return ImGuiKey_None;
    }
}

// ---------------------------------------------------------------------------
// Feed one SDL event into ImGui IO.
// Returns true for mouse/keyboard/text events that ImGui consumed and that
// the game should NOT see while the console is open.
// ---------------------------------------------------------------------------

static bool feed_imgui(const SDL_Event &e)
{
    // Only feed when an ImGui context exists (context created lazily on first frame).
    if (ImGui::GetCurrentContext() == nullptr)
        return false;

    ImGuiIO &io = ImGui::GetIO();

    switch (e.type)
    {
    case SDL_MOUSEMOTION:
        io.AddMousePosEvent(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
        return true;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    {
        int btn = -1;
        if (e.button.button == SDL_BUTTON_LEFT)
            btn = 0;
        else if (e.button.button == SDL_BUTTON_RIGHT)
            btn = 1;
        else if (e.button.button == SDL_BUTTON_MIDDLE)
            btn = 2;
        if (btn >= 0)
            io.AddMouseButtonEvent(btn, e.type == SDL_MOUSEBUTTONDOWN);
        return true;
    }

    case SDL_MOUSEWHEEL:
        io.AddMouseWheelEvent(e.wheel.preciseX, e.wheel.preciseY);
        return true;

    case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(e.text.text);
        return true;

    case SDL_KEYDOWN:
    case SDL_KEYUP:
    {
        const bool down = (e.type == SDL_KEYDOWN);
        // Push modifier state derived from the event's mod bitmask.
        const SDL_Keymod mod = static_cast<SDL_Keymod>(e.key.keysym.mod);
        io.AddKeyEvent(ImGuiMod_Ctrl, (mod & KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (mod & KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mod & KMOD_ALT) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (mod & KMOD_GUI) != 0);
        ImGuiKey imgui_key = sdl_keycode_to_imgui(e.key.keysym.sym);
        if (imgui_key != ImGuiKey_None)
            io.AddKeyEvent(imgui_key, down);
        return true;
    }

    default:
        return false;
    }
}

// Pure classifier (no ImGui): is this an event the console wants while open?
// Mirrors the event types feed_imgui() translates. Used for the swallow decision
// on the (possibly game) thread, where ImGui must not be touched.
static bool is_imgui_input_event(const SDL_Event &e)
{
    switch (e.type)
    {
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    case SDL_TEXTINPUT:
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// ProcessSDLEvent detour: the game's central event processor (one event by
// reference). We flip the console on the toggle key, and while the console is
// open we capture input for ImGui (drained on the render thread) and swallow it
// by NOT forwarding to the original, so the player does not act while typing.
// Everything else is forwarded to the game unchanged.
// ---------------------------------------------------------------------------

extern "C" void repl_process_sdl_event(SDL_Event *e)
{
    if (e != nullptr)
    {
        // Toggle key: flip on keydown; swallow BOTH down and up so the game never
        // sees a dangling toggle key (which it would treat as stuck-held).
        if ((e->type == SDL_KEYDOWN || e->type == SDL_KEYUP) && e->key.keysym.sym == g_toggle_key)
        {
            if (e->type == SDL_KEYDOWN && e->key.repeat == 0)
                g_console_open.store(!g_console_open.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return; // swallow: do not forward to the game
        }

        // While open: queue input for ImGui (fed on the render thread) and swallow
        // it from the game. While closed: fall through and process normally.
        if (g_console_open.load(std::memory_order_relaxed) && is_imgui_input_event(*e))
        {
            std::lock_guard<std::mutex> lk(g_input_mu);
            g_input_queue.push_back(*e);
            return; // swallow
        }
    }

    if (g_orig_process)
        g_orig_process(e);
}

// ---------------------------------------------------------------------------
// Function resolution helpers.
// ---------------------------------------------------------------------------

template <typename Fn> void resolve_device_fn(Fn &slot, VkDevice device, const char *name)
{
    slot = reinterpret_cast<Fn>(g_vkGetDeviceProcAddr(device, name));
    if (slot == nullptr)
        pal::logf(pal::LogLevel::Error, "overlay: failed to resolve device fn %s", name);
}

// Resolve every device-level function we CALL. Safe to call repeatedly.
bool resolve_device_functions(VkDevice device)
{
    if (g_vkGetDeviceProcAddr == nullptr)
    {
        g_vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(g_vkGetInstanceProcAddr(g_state.instance, "vkGetDeviceProcAddr"));
        if (g_vkGetDeviceProcAddr == nullptr)
        {
            pal::logf(pal::LogLevel::Error, "overlay: vkGetDeviceProcAddr unresolved");
            return false;
        }
    }

    resolve_device_fn(g_vkGetDeviceQueue, device, "vkGetDeviceQueue");
    resolve_device_fn(g_vkGetSwapchainImagesKHR, device, "vkGetSwapchainImagesKHR");
    resolve_device_fn(g_vkCreateImageView, device, "vkCreateImageView");
    resolve_device_fn(g_vkDestroyImageView, device, "vkDestroyImageView");
    resolve_device_fn(g_vkCreateRenderPass, device, "vkCreateRenderPass");
    resolve_device_fn(g_vkDestroyRenderPass, device, "vkDestroyRenderPass");
    resolve_device_fn(g_vkCreateFramebuffer, device, "vkCreateFramebuffer");
    resolve_device_fn(g_vkDestroyFramebuffer, device, "vkDestroyFramebuffer");
    resolve_device_fn(g_vkCreateDescriptorPool, device, "vkCreateDescriptorPool");
    resolve_device_fn(g_vkDestroyDescriptorPool, device, "vkDestroyDescriptorPool");
    resolve_device_fn(g_vkCreateCommandPool, device, "vkCreateCommandPool");
    resolve_device_fn(g_vkDestroyCommandPool, device, "vkDestroyCommandPool");
    resolve_device_fn(g_vkAllocateCommandBuffers, device, "vkAllocateCommandBuffers");
    resolve_device_fn(g_vkResetCommandBuffer, device, "vkResetCommandBuffer");
    resolve_device_fn(g_vkBeginCommandBuffer, device, "vkBeginCommandBuffer");
    resolve_device_fn(g_vkEndCommandBuffer, device, "vkEndCommandBuffer");
    resolve_device_fn(g_vkCmdBeginRenderPass, device, "vkCmdBeginRenderPass");
    resolve_device_fn(g_vkCmdEndRenderPass, device, "vkCmdEndRenderPass");
    resolve_device_fn(g_vkQueueSubmit, device, "vkQueueSubmit");
    resolve_device_fn(g_vkDeviceWaitIdle, device, "vkDeviceWaitIdle");
    resolve_device_fn(g_vkCreateSemaphore, device, "vkCreateSemaphore");
    resolve_device_fn(g_vkDestroySemaphore, device, "vkDestroySemaphore");
    resolve_device_fn(g_vkCreateFence, device, "vkCreateFence");
    resolve_device_fn(g_vkDestroyFence, device, "vkDestroyFence");
    resolve_device_fn(g_vkWaitForFences, device, "vkWaitForFences");
    resolve_device_fn(g_vkResetFences, device, "vkResetFences");

    // All non-null?
    return g_vkGetDeviceQueue && g_vkGetSwapchainImagesKHR && g_vkCreateImageView && g_vkDestroyImageView && g_vkCreateRenderPass && g_vkDestroyRenderPass &&
           g_vkCreateFramebuffer && g_vkDestroyFramebuffer && g_vkCreateDescriptorPool && g_vkDestroyDescriptorPool && g_vkCreateCommandPool &&
           g_vkDestroyCommandPool && g_vkAllocateCommandBuffers && g_vkResetCommandBuffer && g_vkBeginCommandBuffer && g_vkEndCommandBuffer &&
           g_vkCmdBeginRenderPass && g_vkCmdEndRenderPass && g_vkQueueSubmit && g_vkDeviceWaitIdle && g_vkCreateSemaphore && g_vkDestroySemaphore &&
           g_vkCreateFence && g_vkDestroyFence && g_vkWaitForFences && g_vkResetFences;
}

// Loader thunk handed to ImGui_ImplVulkan_LoadFunctions. The Vulkan backend uses
// it to resolve every function it needs (instance-level resolution covers device
// functions too via the loader).
PFN_vkVoidFunction imgui_loader_thunk(const char *name, void * /*user*/)
{
    if (g_vkGetInstanceProcAddr == nullptr)
        return nullptr;
    return g_vkGetInstanceProcAddr(g_state.instance, name);
}

// ---------------------------------------------------------------------------
// Swapchain resource teardown / build.
// ---------------------------------------------------------------------------

void destroy_swapchain_resources()
{
    if (g_state.device == VK_NULL_HANDLE)
        return;

    if (g_vkDeviceWaitIdle)
        g_vkDeviceWaitIdle(g_state.device);

    if (g_vkDestroySemaphore)
        for (VkSemaphore s : g_state.render_finished)
            if (s != VK_NULL_HANDLE)
                g_vkDestroySemaphore(g_state.device, s, nullptr);
    g_state.render_finished.clear();

    if (g_vkDestroyFence)
        for (VkFence f : g_state.in_flight)
            if (f != VK_NULL_HANDLE)
                g_vkDestroyFence(g_state.device, f, nullptr);
    g_state.in_flight.clear();

    if (g_vkDestroyFramebuffer)
        for (VkFramebuffer fb : g_state.framebuffers)
            if (fb != VK_NULL_HANDLE)
                g_vkDestroyFramebuffer(g_state.device, fb, nullptr);
    g_state.framebuffers.clear();

    if (g_vkDestroyImageView)
        for (VkImageView v : g_state.image_views)
            if (v != VK_NULL_HANDLE)
                g_vkDestroyImageView(g_state.device, v, nullptr);
    g_state.image_views.clear();

    // Command buffers are freed implicitly with their pool.
    if (g_state.command_pool != VK_NULL_HANDLE && g_vkDestroyCommandPool)
    {
        g_vkDestroyCommandPool(g_state.device, g_state.command_pool, nullptr);
        g_state.command_pool = VK_NULL_HANDLE;
    }
    g_state.command_buffers.clear();

    if (g_state.render_pass != VK_NULL_HANDLE && g_vkDestroyRenderPass)
    {
        g_vkDestroyRenderPass(g_state.device, g_state.render_pass, nullptr);
        g_state.render_pass = VK_NULL_HANDLE;
    }

    g_state.images.clear();
    g_state.resources_ready.store(false, std::memory_order_release);
}

bool build_render_pass()
{
    VkAttachmentDescription color{};
    color.format = g_state.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // draw over the game's frame
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &color;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;

    if (g_vkCreateRenderPass(g_state.device, &rp, nullptr, &g_state.render_pass) != VK_SUCCESS)
    {
        pal::logf(pal::LogLevel::Error, "overlay: vkCreateRenderPass failed");
        return false;
    }
    return true;
}

// Rebuild every per-image resource from the freshly created swapchain. Returns
// true if the overlay is ready to render against this swapchain.
bool build_swapchain_resources()
{
    // Drop readiness before tearing down / rebuilding so the present thread degrades
    // (forwards untouched) rather than touching half-built resources.
    g_state.resources_ready.store(false, std::memory_order_release);
    destroy_swapchain_resources();

    if (!resolve_device_functions(g_state.device))
    {
        pal::logf(pal::LogLevel::Error, "overlay: device function resolution incomplete; not rendering");
        return false;
    }

    // Enumerate swapchain images.
    std::uint32_t count = 0;
    if (g_vkGetSwapchainImagesKHR(g_state.device, g_state.swapchain, &count, nullptr) != VK_SUCCESS || count == 0)
    {
        pal::logf(pal::LogLevel::Error, "overlay: vkGetSwapchainImagesKHR (count) failed");
        return false;
    }
    g_state.images.resize(count);
    if (g_vkGetSwapchainImagesKHR(g_state.device, g_state.swapchain, &count, g_state.images.data()) != VK_SUCCESS)
    {
        pal::logf(pal::LogLevel::Error, "overlay: vkGetSwapchainImagesKHR (data) failed");
        return false;
    }
    g_state.image_count = count;

    if (!build_render_pass())
        return false;

    // Image views.
    g_state.image_views.resize(count, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo iv{};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = g_state.images[i];
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = g_state.format;
        iv.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.baseMipLevel = 0;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.baseArrayLayer = 0;
        iv.subresourceRange.layerCount = 1;
        if (g_vkCreateImageView(g_state.device, &iv, nullptr, &g_state.image_views[i]) != VK_SUCCESS)
        {
            pal::logf(pal::LogLevel::Error, "overlay: vkCreateImageView[%u] failed", i);
            return false;
        }
    }

    // Framebuffers.
    g_state.framebuffers.resize(count, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VkImageView attach = g_state.image_views[i];
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = g_state.render_pass;
        fb.attachmentCount = 1;
        fb.pAttachments = &attach;
        fb.width = g_state.extent.width;
        fb.height = g_state.extent.height;
        fb.layers = 1;
        if (g_vkCreateFramebuffer(g_state.device, &fb, nullptr, &g_state.framebuffers[i]) != VK_SUCCESS)
        {
            pal::logf(pal::LogLevel::Error, "overlay: vkCreateFramebuffer[%u] failed", i);
            return false;
        }
    }

    // Command pool (graphics family, resettable buffers) + one command buffer per image.
    if (!g_state.queue_family_known)
        pal::logf(pal::LogLevel::Warn, "overlay: assuming graphics queue family 0 (vkCreateDevice was not intercepted; queue family was never captured)");

    VkCommandPoolCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = g_state.queue_family;
    if (g_vkCreateCommandPool(g_state.device, &cp, nullptr, &g_state.command_pool) != VK_SUCCESS)
    {
        pal::logf(pal::LogLevel::Error, "overlay: vkCreateCommandPool failed");
        return false;
    }

    g_state.command_buffers.resize(count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = g_state.command_pool;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = count;
    if (g_vkAllocateCommandBuffers(g_state.device, &cba, g_state.command_buffers.data()) != VK_SUCCESS)
    {
        pal::logf(pal::LogLevel::Error, "overlay: vkAllocateCommandBuffers failed");
        return false;
    }

    // Per-image render-finished semaphores.
    g_state.render_finished.resize(count, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (g_vkCreateSemaphore(g_state.device, &si, nullptr, &g_state.render_finished[i]) != VK_SUCCESS)
        {
            pal::logf(pal::LogLevel::Error, "overlay: vkCreateSemaphore[%u] failed", i);
            return false;
        }
    }

    // Per-image in-flight fences. Created SIGNALED so the first vkWaitForFences for a
    // given image returns immediately (no submit has been issued for it yet). The
    // fence guarantees the PREVIOUS overlay submit for this image index has completed
    // before its command buffer is reset and re-recorded.
    g_state.in_flight.resize(count, VK_NULL_HANDLE);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (g_vkCreateFence(g_state.device, &fi, nullptr, &g_state.in_flight[i]) != VK_SUCCESS)
        {
            pal::logf(pal::LogLevel::Error, "overlay: vkCreateFence[%u] failed", i);
            return false;
        }
    }

    // Publish: release-store so the present thread's acquire-load sees every handle
    // written above fully constructed. Only reached when ALL resources succeeded.
    g_state.resources_ready.store(true, std::memory_order_release);
    pal::logf(pal::LogLevel::Info, "overlay: swapchain resources built (%ux%u, %u images, fmt=%d)", g_state.extent.width, g_state.extent.height,
              g_state.image_count, static_cast<int>(g_state.format));
    return true;
}

// ---------------------------------------------------------------------------
// ImGui-on-Vulkan lazy init.
// ---------------------------------------------------------------------------

bool init_imgui()
{
    if (g_imgui_inited.load(std::memory_order_acquire))
        return true;

    // Descriptor pool sized for ImGui.
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 64;

    VkDescriptorPoolCreateInfo dp{};
    dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp.maxSets = 64;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &pool_size;
    if (g_vkCreateDescriptorPool(g_state.device, &dp, nullptr, &g_state.descriptor_pool) != VK_SUCCESS)
    {
        pal::logf(pal::LogLevel::Error, "overlay: vkCreateDescriptorPool failed");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; // never write imgui.ini into the game dir
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // Input arrives in a later task; give NewFrame a legal display size now so it
    // does not assert before the first present sets the real swapchain extent.
    io.DisplaySize = ImVec2(static_cast<float>(g_state.extent.width), static_cast<float>(g_state.extent.height));

    if (!ImGui_ImplVulkan_LoadFunctions(&imgui_loader_thunk, nullptr))
    {
        pal::logf(pal::LogLevel::Error, "overlay: ImGui_ImplVulkan_LoadFunctions failed");
        g_vkDestroyDescriptorPool(g_state.device, g_state.descriptor_pool, nullptr);
        g_state.descriptor_pool = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        return false;
    }

    ImGui_ImplVulkan_InitInfo init{};
    init.Instance = g_state.instance;
    init.PhysicalDevice = g_state.physical_device;
    init.Device = g_state.device;
    init.QueueFamily = g_state.queue_family;
    init.Queue = g_state.queue;
    init.DescriptorPool = g_state.descriptor_pool;
    init.RenderPass = g_state.render_pass;
    init.MinImageCount = g_state.min_image_count < 2 ? 2 : g_state.min_image_count;
    init.ImageCount = g_state.image_count;
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineCache = VK_NULL_HANDLE;
    init.Subpass = 0;
    init.UseDynamicRendering = false;
    init.Allocator = nullptr;
    init.CheckVkResultFn = nullptr;

    if (!ImGui_ImplVulkan_Init(&init))
    {
        pal::logf(pal::LogLevel::Error, "overlay: ImGui_ImplVulkan_Init failed");
        g_vkDestroyDescriptorPool(g_state.device, g_state.descriptor_pool, nullptr);
        g_state.descriptor_pool = VK_NULL_HANDLE;
        ImGui::DestroyContext();
        return false;
    }

    // v1.91.5 uploads the font texture automatically on first RenderDrawData; no
    // manual command-buffer dance required.
    g_imgui_inited.store(true, std::memory_order_release);
    pal::logf(pal::LogLevel::Info, "overlay: ImGui Vulkan backend initialized");
    return true;
}

void shutdown_imgui()
{
    if (!g_imgui_inited.load(std::memory_order_acquire))
        return;
    if (g_state.device != VK_NULL_HANDLE && g_vkDeviceWaitIdle)
        g_vkDeviceWaitIdle(g_state.device);
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    if (g_state.descriptor_pool != VK_NULL_HANDLE && g_vkDestroyDescriptorPool)
    {
        g_vkDestroyDescriptorPool(g_state.device, g_state.descriptor_pool, nullptr);
        g_state.descriptor_pool = VK_NULL_HANDLE;
    }
    g_imgui_inited.store(false, std::memory_order_release);
}

bool state_complete_for_render()
{
    // Acquire-load the readiness gate: pairs with the release-store in
    // build_swapchain_resources so all non-atomic g_state handle writes that preceded
    // the release are visible here before we touch any of them.
    return g_state.instance != VK_NULL_HANDLE && g_state.physical_device != VK_NULL_HANDLE && g_state.device != VK_NULL_HANDLE &&
           g_state.queue != VK_NULL_HANDLE && g_state.resources_ready.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// Detours.
// ---------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL repl_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance)
{
    VkResult r = g_orig_create_instance ? g_orig_create_instance(pCreateInfo, pAllocator, pInstance) : VK_ERROR_INITIALIZATION_FAILED;
    if (r == VK_SUCCESS && pInstance != nullptr)
    {
        g_state.instance = *pInstance;
        pal::logf(pal::LogLevel::Info, "overlay: captured VkInstance");
    }
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL repl_vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo,
                                                   const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
    VkResult r = g_orig_create_device ? g_orig_create_device(physicalDevice, pCreateInfo, pAllocator, pDevice) : VK_ERROR_INITIALIZATION_FAILED;
    if (r == VK_SUCCESS && pDevice != nullptr)
    {
        g_state.device = *pDevice;
        g_state.physical_device = physicalDevice;
        if (pCreateInfo != nullptr && pCreateInfo->queueCreateInfoCount > 0 && pCreateInfo->pQueueCreateInfos != nullptr)
        {
            g_state.queue_family = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;
            g_state.queue_family_known = true;
        }
        // Resolve device functions now so we can grab the queue immediately.
        if (resolve_device_functions(g_state.device) && g_state.queue_family_known && g_vkGetDeviceQueue)
        {
            g_vkGetDeviceQueue(g_state.device, g_state.queue_family, 0, &g_state.queue);
        }
        pal::logf(pal::LogLevel::Info, "overlay: captured VkDevice (queue family %u)", g_state.queue_family);
    }
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL repl_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator,
                                                         VkSwapchainKHR *pSwapchain)
{
    VkResult r = g_orig_create_swapchain ? g_orig_create_swapchain(device, pCreateInfo, pAllocator, pSwapchain) : VK_ERROR_INITIALIZATION_FAILED;
    if (r != VK_SUCCESS || pSwapchain == nullptr || pCreateInfo == nullptr)
        return r;

    // Fallback: recover the device if vkCreateDevice was missed due to the
    // startup-timing race (the swapchain call carries the VkDevice).
    if (g_state.device == VK_NULL_HANDLE)
    {
        g_state.device = device;
        pal::logf(pal::LogLevel::Warn, "overlay: VkDevice recovered from vkCreateSwapchainKHR (missed vkCreateDevice)");
    }

    g_state.swapchain = *pSwapchain;
    g_state.format = pCreateInfo->imageFormat;
    g_state.extent = pCreateInfo->imageExtent;
    g_state.min_image_count = pCreateInfo->minImageCount;

    // If ImGui was initialized against a previous swapchain (resize / vsync
    // toggle), tear it down so it re-inits with the new render pass + counts.
    if (g_imgui_inited.load(std::memory_order_acquire))
        shutdown_imgui();

    if (!build_swapchain_resources())
        pal::logf(pal::LogLevel::Warn, "overlay: swapchain resource build failed; overlay inactive for this swapchain");

    g_warned_incomplete = false; // allow a fresh warning if state still incomplete
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL repl_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo)
{
    // 1. Adopt the present queue if we never captured one (e.g. missed device).
    if (g_state.queue == VK_NULL_HANDLE)
        g_state.queue = queue;

    // 2. Degrade: forward untouched if we cannot render.
    if (!state_complete_for_render() || pPresentInfo == nullptr)
    {
        if (!g_warned_incomplete)
        {
            pal::logf(pal::LogLevel::Warn, "overlay: state incomplete (instance=%p device=%p phys=%p queue=%p ready=%d); forwarding present untouched",
                      reinterpret_cast<void *>(g_state.instance), reinterpret_cast<void *>(g_state.device), reinterpret_cast<void *>(g_state.physical_device),
                      reinterpret_cast<void *>(g_state.queue), static_cast<int>(g_state.resources_ready.load(std::memory_order_acquire)));
            g_warned_incomplete = true;
        }
        return g_orig_present(queue, pPresentInfo);
    }

    // 3. Skip the entire frame when the console is closed (zero GPU cost). Discard
    //    any input captured during the closing frame so it does not replay on reopen.
    if (!g_console_open.load(std::memory_order_acquire))
    {
        std::lock_guard<std::mutex> lk(g_input_mu);
        g_input_queue.clear();
        return g_orig_present(queue, pPresentInfo);
    }

    // 4a. Lazy ImGui init (runs only when we are about to draw).
    if (!init_imgui())
        return g_orig_present(queue, pPresentInfo);

    // 4b. Find OUR swapchain in the present info and its image index. For first
    //     light we render only into the swapchain we captured; others pass through.
    std::uint32_t our_idx = UINT32_MAX;
    std::uint32_t image_index = 0;
    for (std::uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i)
    {
        if (pPresentInfo->pSwapchains[i] == g_state.swapchain)
        {
            our_idx = i;
            image_index = pPresentInfo->pImageIndices[i];
            break;
        }
    }
    if (our_idx == UINT32_MAX || image_index >= g_state.image_count)
        return g_orig_present(queue, pPresentInfo);

    // 5. Build the ImGui frame. First drain SDL input captured by the SDL hook
    //    (possibly on the game thread) into ImGui IO -- done here so every ImGui
    //    call stays on this (the render) thread.
    {
        std::lock_guard<std::mutex> lk(g_input_mu);
        for (const SDL_Event &e : g_input_queue)
            feed_imgui(e);
        g_input_queue.clear();
    }

    ImGui_ImplVulkan_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(g_state.extent.width), static_cast<float>(g_state.extent.height));
    io.DeltaTime = 1.0f / 60.0f; // real timing arrives with input handling
    io.MouseDrawCursor = true;   // draw ImGui's software cursor (game may hide the OS cursor)
    ImGui::NewFrame();

    if (IOverlayUi *ui = g_ui.load(std::memory_order_acquire))
        ui->draw();
    else
        ImGui::ShowDemoWindow();

    ImGui::Render();

    // 6. Record the overlay draw into this image's command buffer. Wait on this
    //    image's in-flight fence first so the PREVIOUS overlay submit for this image
    //    index has fully completed before we reset/re-record/re-submit its command
    //    buffer (created signaled, so the first wait returns immediately).
    VkFence in_flight = g_state.in_flight[image_index];
    g_vkWaitForFences(g_state.device, 1, &in_flight, VK_TRUE, UINT64_MAX);
    g_vkResetFences(g_state.device, 1, &in_flight);

    VkCommandBuffer cmd = g_state.command_buffers[image_index];
    g_vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g_vkBeginCommandBuffer(cmd, &begin);

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g_state.render_pass;
    rp.framebuffer = g_state.framebuffers[image_index];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = g_state.extent;
    rp.clearValueCount = 0; // loadOp = LOAD, nothing to clear
    rp.pClearValues = nullptr;
    g_vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    g_vkCmdEndRenderPass(cmd);
    g_vkEndCommandBuffer(cmd);

    // 7. Submit: wait on the game's render-finished semaphores (so we draw after
    //    the frame is rendered), signal our overlay semaphore for this image.
    VkSemaphore overlay_sem = g_state.render_finished[image_index];

    std::vector<VkPipelineStageFlags> wait_stages(pPresentInfo->waitSemaphoreCount, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = pPresentInfo->waitSemaphoreCount;
    submit.pWaitSemaphores = pPresentInfo->pWaitSemaphores;
    submit.pWaitDstStageMask = wait_stages.empty() ? nullptr : wait_stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &overlay_sem;

    // Pass this image's in-flight fence; it is signaled when the overlay work for this
    // image completes, gating the next reuse of this command buffer.
    if (g_vkQueueSubmit(queue, 1, &submit, in_flight) != VK_SUCCESS)
    {
        // If our submit fails we must NOT consume the game's wait semaphores; fall
        // back to forwarding the original present (the game's semaphores are still
        // pending and will be waited on by the present below). We reset in_flight to
        // signaled (an empty submit) so a future frame's vkWaitForFences for this image
        // does not block forever on a fence that was reset but never signaled.
        VkSubmitInfo signal_only{};
        signal_only.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        g_vkQueueSubmit(queue, 1, &signal_only, in_flight);
        pal::logf(pal::LogLevel::Warn, "overlay: vkQueueSubmit failed; forwarding original present");
        return g_orig_present(queue, pPresentInfo);
    }

    // 8. Present, waiting on OUR semaphore (which in turn waited on the game's).
    VkPresentInfoKHR modified = *pPresentInfo;
    modified.waitSemaphoreCount = 1;
    modified.pWaitSemaphores = &overlay_sem;
    return g_orig_present(queue, &modified);
}

// ---------------------------------------------------------------------------
// Overlay implementation.
// ---------------------------------------------------------------------------

class VulkanOverlay final : public IOverlay
{
  public:
    explicit VulkanOverlay(const OverlayConfig &cfg) : cfg_(cfg)
    {
        pal::logf(pal::LogLevel::Info, "overlay: VulkanOverlay constructing (event_proc=0x%llx)", static_cast<unsigned long long>(cfg_.process_sdl_event_addr));

        // The game already loaded libvulkan; RTLD_NOLOAD just hands us its handle.
        vulkan_handle_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD);
        if (vulkan_handle_ == nullptr)
        {
            const char *dl_err = dlerror(); // dlerror() clears on read; call once
            pal::logf(pal::LogLevel::Error, "overlay: dlopen(libvulkan.so.1, NOLOAD) failed: %s — overlay inert", dl_err ? dl_err : "(null)");
            return;
        }

        g_vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vulkan_handle_, "vkGetInstanceProcAddr"));
        if (g_vkGetInstanceProcAddr == nullptr)
        {
            pal::logf(pal::LogLevel::Error, "overlay: dlsym(vkGetInstanceProcAddr) failed — overlay inert");
            return;
        }

        // Resolve the loader trampolines the game calls; hooking these catches it.
        struct Spec
        {
            const char *name;
            void *replacement;
            void **trampoline;
        };
        void *p_create_instance = dlsym(vulkan_handle_, "vkCreateInstance");
        void *p_create_device = dlsym(vulkan_handle_, "vkCreateDevice");
        void *p_create_swapchain = dlsym(vulkan_handle_, "vkCreateSwapchainKHR");
        void *p_present = dlsym(vulkan_handle_, "vkQueuePresentKHR");

        const Spec specs[kVkHookCount] = {
            {"vkCreateInstance", reinterpret_cast<void *>(&repl_vkCreateInstance), reinterpret_cast<void **>(&g_orig_create_instance)},
            {"vkCreateDevice", reinterpret_cast<void *>(&repl_vkCreateDevice), reinterpret_cast<void **>(&g_orig_create_device)},
            {"vkCreateSwapchainKHR", reinterpret_cast<void *>(&repl_vkCreateSwapchainKHR), reinterpret_cast<void **>(&g_orig_create_swapchain)},
            {"vkQueuePresentKHR", reinterpret_cast<void *>(&repl_vkQueuePresentKHR), reinterpret_cast<void **>(&g_orig_present)},
        };
        void *targets[kVkHookCount] = {p_create_instance, p_create_device, p_create_swapchain, p_present};

        for (std::size_t i = 0; i < kVkHookCount; ++i)
        {
            if (targets[i] == nullptr)
            {
                pal::logf(pal::LogLevel::Error, "overlay: dlsym(%s) failed; not hooked", specs[i].name);
                continue;
            }
            HookId id = pal::hook_engine().install_hook(targets[i], specs[i].replacement, specs[i].trampoline);
            if (id == pal::kInvalidHookId)
            {
                pal::logf(pal::LogLevel::Error, "overlay: failed to hook %s at %p", specs[i].name, targets[i]);
            }
            else
            {
                ids_[installed_++] = id;
                pal::logf(pal::LogLevel::Info, "overlay: hooked %s at %p (id=%llu)", specs[i].name, targets[i], static_cast<unsigned long long>(id));
            }
        }

        // Resolve toggle key from environment, then install the ProcessSDLEvent hook.
        g_toggle_key = parse_console_key(std::getenv("MTHAP_CONSOLE_KEY"));
        pal::logf(pal::LogLevel::Info, "overlay: console toggle key = 0x%x (SDLK)", static_cast<int>(g_toggle_key));

        if (cfg_.process_sdl_event_addr != 0)
        {
            void *evt_target = reinterpret_cast<void *>(cfg_.process_sdl_event_addr);
            HookId evt_id =
                pal::hook_engine().install_hook(evt_target, reinterpret_cast<void *>(&repl_process_sdl_event), reinterpret_cast<void **>(&g_orig_process));
            if (evt_id == pal::kInvalidHookId)
            {
                pal::logf(pal::LogLevel::Error, "overlay: failed to hook ProcessSDLEvent at %p; input unavailable", evt_target);
            }
            else
            {
                ids_[installed_++] = evt_id;
                pal::logf(pal::LogLevel::Info, "overlay: hooked ProcessSDLEvent at %p (id=%llu)", evt_target, static_cast<unsigned long long>(evt_id));
            }
        }
        else
        {
            pal::logf(pal::LogLevel::Warn, "overlay: process_sdl_event_addr == 0; input unavailable for this build (render-only mode)");
        }
    }

    ~VulkanOverlay() override
    {
        // Remove hooks FIRST so no detour fires mid-teardown.
        for (std::size_t i = 0; i < installed_; ++i)
            pal::hook_engine().remove_hook(ids_[i]);
        installed_ = 0;

        // Tear down ImGui + every Vulkan object we created.
        shutdown_imgui();
        destroy_swapchain_resources();

        g_ui.store(nullptr, std::memory_order_release);

        if (vulkan_handle_ != nullptr)
        {
            dlclose(vulkan_handle_); // NOLOAD handle: balances our reference
            vulkan_handle_ = nullptr;
        }
        pal::logf(pal::LogLevel::Info, "overlay: VulkanOverlay destroyed");
    }

    void set_ui(IOverlayUi *ui) override
    {
        g_ui.store(ui, std::memory_order_release);
    }

  private:
    static constexpr std::size_t kVkHookCount = 4;  // vkCreateInstance/Device/Swapchain/Present
    static constexpr std::size_t kMaxHookCount = 5; // + SDL_PollEvent
    OverlayConfig cfg_;
    void *vulkan_handle_ = nullptr;
    HookId ids_[kMaxHookCount]{};
    std::size_t installed_ = 0;
};

} // namespace

std::unique_ptr<IOverlay> make_overlay(const OverlayConfig &cfg)
{
    return std::make_unique<VulkanOverlay>(cfg);
}

} // namespace pal
