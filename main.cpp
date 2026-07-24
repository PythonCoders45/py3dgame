#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <cstring>

// 1. GRAPHICS: GLFW & Vulkan
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// 2. PHYSICS: Jolt Physics Headers
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>

// 3. AI: Recast & Detour Navigation Headers
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

// 4. NETWORKING: GameNetworkingSockets
#include <steam/steamnetworkingsockets.h>

// Engine Subsystem Memory
struct UnifiedEngineState {
    GLFWwindow* window = nullptr;
    VkInstance vulkanInstance = VK_NULL_HANDLE;
    JPH::TempAllocatorImpl* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem = nullptr;
    JPH::PhysicsSystem* physicsSystem = nullptr;
    dtNavMesh* navMesh = nullptr;
    dtNavMeshQuery* navQuery = nullptr;
    HSteamListenSocket listenSocket = k_HSteamListenSocket_Invalid;
};

static UnifiedEngineState g_Engine;

// Opcode Constants
enum EngineOpcode {
    OP_INIT_GRAPHICS  = 1,
    OP_POLL_WINDOW    = 2,
    OP_INIT_PHYSICS   = 3,
    OP_STEP_PHYSICS   = 4,
    OP_INIT_NAV       = 5,
    OP_INIT_NET       = 6,
    OP_POLL_NET       = 7,
    OP_SHUTDOWN       = 8
};

// Parameter Struct Passed from Python
struct EngineParams {
    int width = 1280;
    int height = 720;
    const char* title = "Py3D Engine";
    float deltaTime = 0.01667f;
    uint16_t port = 7777;
};

// ============================================================================
// SINGLE C INTERFACE FUNCTION
// ============================================================================
extern "C" {

int py3d_engine_call(int opcode, void* param_struct) {
    EngineParams* params = static_cast<EngineParams*>(param_struct);

    switch (opcode) {

        // --------------------------------------------------------------------
        // 1. GRAPHICS & WINDOW (GLFW + Vulkan)
        // --------------------------------------------------------------------
        case OP_INIT_GRAPHICS: {
            std::cout << "[Vulkan/GLFW] Booting window system...\n";
            if (!glfwInit()) return 0;

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

            int w = params ? params->width : 1280;
            int h = params ? params->height : 720;
            const char* title = (params && params->title) ? params->title : "Py3D Engine";

            g_Engine.window = glfwCreateWindow(w, h, title, nullptr, nullptr);
            if (!g_Engine.window) return 0;

            VkApplicationInfo appInfo{};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "Py3D Engine Core";
            appInfo.apiVersion = VK_API_VERSION_1_2;

            uint32_t extensionCount = 0;
            const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

            VkInstanceCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = extensionCount;
            createInfo.ppEnabledExtensionNames = extensions;

            if (vkCreateInstance(&createInfo, nullptr, &g_Engine.vulkanInstance) != VK_SUCCESS) {
                return 0;
            }
            std::cout << "[Vulkan/GLFW] Window and Vulkan Instance Ready!\n";
            return 1;
        }

        // --------------------------------------------------------------------
        // 2. WINDOW POLL
        // --------------------------------------------------------------------
        case OP_POLL_WINDOW: {
            if (!g_Engine.window) return 1;
            glfwPollEvents();
            return glfwWindowShouldClose(g_Engine.window) ? 1 : 0;
        }

        // --------------------------------------------------------------------
        // 3. PHYSICS INITIALIZATION (Jolt Physics)
        // --------------------------------------------------------------------
        case OP_INIT_PHYSICS: {
            std::cout << "[Jolt Physics] Initializing simulation...\n";
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();

            g_Engine.tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
            g_Engine.jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 4);

            g_Engine.physicsSystem = new JPH::PhysicsSystem();
            g_Engine.physicsSystem->Init(
                1024, 0, 1024, 1024, 
                JPH::PhysicsSystem::sDefaultBroadPhaseLayerInterface,
                JPH::PhysicsSystem::sDefaultObjectVsBroadPhaseLayerFilter,
                JPH::PhysicsSystem::sDefaultObjectLayerPairFilter
            );
            std::cout << "[Jolt Physics] Engine core online!\n";
            return 1;
        }

        // --------------------------------------------------------------------
        // 4. PHYSICS STEP
        // --------------------------------------------------------------------
        case OP_STEP_PHYSICS: {
            if (g_Engine.physicsSystem) {
                float dt = params ? params->deltaTime : (1.0f / 60.0f);
                g_Engine.physicsSystem->Update(dt, 1, g_Engine.tempAllocator, g_Engine.jobSystem);
            }
            return 1;
        }

        // --------------------------------------------------------------------
        // 5. NAVIGATION INITIALIZATION (Recast & Detour)
        // --------------------------------------------------------------------
        case OP_INIT_NAV: {
            std::cout << "[Recast/Detour] Initializing NavMesh...\n";
            g_Engine.navMesh = dtAllocNavMesh();
            g_Engine.navQuery = dtAllocNavMeshQuery();

            dtNavMeshParams navParams{};
            navParams.tileWidth = 32.0f;
            navParams.tileHeight = 32.0f;
            navParams.maxTiles = 128;
            navParams.maxPolys = 32768;

            if (dtStatusFailed(g_Engine.navMesh->init(&navParams))) return 0;
            if (dtStatusFailed(g_Engine.navQuery->init(g_Engine.navMesh, 2048))) return 0;

            std::cout << "[Recast/Detour] AI Pathfinding ready!\n";
            return 1;
        }

        // --------------------------------------------------------------------
        // 6. NETWORKING INITIALIZATION (GameNetworkingSockets)
        // --------------------------------------------------------------------
        case OP_INIT_NET: {
            uint16_t port = params ? params->port : 7777;
            std::cout << "[UDP Network] Listening on port " << port << "...\n";

            SteamDatagramErrMsg errMsg;
            if (!GameNetworkingSockets_Init(nullptr, errMsg)) return 0;

            SteamNetworkingIPAddr serverAddr;
            serverAddr.Clear();
            serverAddr.m_port = port;

            g_Engine.listenSocket = SteamNetworkingSockets()->CreateListenSocketIP(serverAddr, 0, nullptr);
            return (g_Engine.listenSocket != k_HSteamListenSocket_Invalid) ? 1 : 0;
        }

        // --------------------------------------------------------------------
        // 7. NETWORKING POLL
        // --------------------------------------------------------------------
        case OP_POLL_NET: {
            SteamNetworkingSockets()->RunCallbacks();
            return 1;
        }

        // --------------------------------------------------------------------
        // 8. SHUTDOWN & CLEANUP
        // --------------------------------------------------------------------
        case OP_SHUTDOWN: {
            std::cout << "[Engine] Cleaning up sub-systems...\n";
            if (g_Engine.listenSocket != k_HSteamListenSocket_Invalid) {
                SteamNetworkingSockets()->CloseListenSocket(g_Engine.listenSocket);
                GameNetworkingSockets_Kill();
            }
            if (g_Engine.navQuery) dtFreeNavMeshQuery(g_Engine.navQuery);
            if (g_Engine.navMesh) dtFreeNavMesh(g_Engine.navMesh);

            if (g_Engine.physicsSystem) delete g_Engine.physicsSystem;
            if (g_Engine.jobSystem) delete g_Engine.jobSystem;
            if (g_Engine.tempAllocator) delete g_Engine.tempAllocator;
            delete JPH::Factory::sInstance;

            if (g_Engine.vulkanInstance != VK_NULL_HANDLE) vkDestroyInstance(g_Engine.vulkanInstance, nullptr);
            if (g_Engine.window) glfwDestroyWindow(g_Engine.window);
            glfwTerminate();

            std::cout << "[Engine] Clean shutdown complete.\n";
            return 1;
        }

        default:
            return -1;
    }
}

} // extern "C"
