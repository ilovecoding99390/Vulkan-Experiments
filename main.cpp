#define VK_NO_PROTOTYPES
#define GLFW_INCLUDE_VULKAN
#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#define TINYOBJLOADER_IMPLEMENTATION

#include <vulkan/vulkan.h>
#include <volk/volk.h>
#include <vma/vk_mem_alloc.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <tiny_obj_loader.h>
#include "constants.hpp"


#include <vector>
#include <iostream>
#include <fstream>
#include <array>
#include <filesystem>
#include <string>
#include <chrono>
#include <sstream>


#define chk2(x) a=x; if (a != VK_SUCCESS && a!= VK_SUBOPTIMAL_KHR) {cerr << "A Call returned an error (" << a << ") Line:"<< __LINE__ <<" In:"<<__func__<< endl; exit(a);}
int a;

using namespace std;
using namespace constants;
using glm::vec2;
using glm::vec3;
using glm::vec4;


VkPhysicalDeviceFeatures deviceFeatures{};
constexpr uint32_t maxframesinflight=2;
vector<const char *> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
VkCullModeFlagBits cullMode=VK_CULL_MODE_BACK_BIT; 
VkFrontFace frontface=VK_FRONT_FACE_CLOCKWISE; 

struct Engine;
struct Window;

struct fpsdata{
    std::chrono::duration<double> time;
    uint32_t framecount;
    double deltat;
    double fps;
};

struct Window{
    GLFWwindow* GlfwWindow;
    VkSurfaceKHR Surface;
    VkSurfaceCapabilitiesKHR SurfaceCapabilities;
    VkSwapchainKHR Swapchain;
    vector<VkImage> images;
    vector<VkImageView> imageviews;
    struct Engine* engine;

    uint32_t ActiveImage;
    uint32_t FrameInFlight;

    array<VkCommandBuffer, maxframesinflight> CmdBuffers;
    array<VkSemaphore, maxframesinflight> ImageAquiredSemaphore;
    vector<VkSemaphore> PresentationReadySemaphore;
    array<VkFence,maxframesinflight> FrameFence;

    VkImage DepthImage;
    VmaAllocation DepthAlloc;
    VkImageView DepthImageView;

    int width, height;
};

struct ShaderContainer{
    VkPipelineLayout PipelineLayout;
    VkPipeline Pipeline;
    vector<VkShaderModule> ShaderModules;
};

struct Engine{
    VkInstance Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    VkQueue Queue;
    VkCommandPool CmdPool;
    vector<Window> Windows;
    uint32_t QueueFamily;
    VmaAllocator Allocator;
    ShaderContainer Shader;
    int displayw, displayh;
    VkFormat ImageFormat, Depthformat;
    
};

struct Vertex{
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

struct GpuBuffer{
    VkBuffer vkbuffer;
    VmaAllocation alloc;
    VmaAllocationInfo allocinfo;
    uint32_t size;
};

struct GpuMesh{
    vector<Vertex> vertecies;
    vector<uint32_t> indecies;
    GpuBuffer VertexBuffer;
    GpuBuffer IndeciesBuffer;
};

struct Bounds{
    vec3 min;
    vec3 max;
};

struct extragpudata{
    vec4 bounds_min; // matches float3 + padding in shader
    vec4 bounds_max; // matches float3 + padding in shader

    vec4 offset; // xyz = offset, w unused

    vec4 rotation; // xyz = euler angles, w = deltat
    
    vec4 scale;
};

static inline void chk(VkResult result) {
	if (result != VK_SUCCESS) {
		cerr << "Vulkan call returned an error (" << result << ")\n";
        exit(result);
	}
}
static inline void chk3(VkResult result) {
	if (result != VK_SUCCESS) {
		cerr << "Vulkan call returned an error (" << result << ")\n";
        //exit(result);
	}
}
static inline void chk(bool result) {
	if (!result) {
		cerr << "Call returned an error\n";
		exit(result);
	}
}
static inline void gchk(int r){
    if(r!=GLFW_TRUE){
        cerr << "Call returned an error\n";
		exit(r);
    }
}
inline int IsKeyPressed(const Window &Window,int key){
    return glfwGetKey(Window.GlfwWindow,key);
}


static inline void swapchainImageBarriercmd(VkCommandBuffer cmdbuff, Window &window){
    std::array<VkImageMemoryBarrier, 2> barriers{
    VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = window.images[window.ActiveImage],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    },
    VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = window.DepthImage,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}
    }
    };  
    vkCmdPipelineBarrier(cmdbuff, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers.data());
    
}
static inline void viewportAndScissorcmd(VkCommandBuffer cmdbuff, const Window &window){
    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(window.width),
        .height = static_cast<float>(window.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    VkRect2D scissor{
        .offset = {0, 0},
        .extent = {static_cast<uint32_t>(window.width), static_cast<uint32_t>(window.height)}
    };
    vkCmdSetViewport(cmdbuff, 0, 1, &viewport);
    vkCmdSetScissor(cmdbuff, 0, 1, &scissor);
}
static inline void addPresentationBarriercmd(VkCommandBuffer cmdbuff, Window &window){
    std::array<VkImageMemoryBarrier, 2> barriers{
    VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = window.images[window.ActiveImage],
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    },
    VkImageMemoryBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = window.DepthImage,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}
    }
    };
    vkCmdPipelineBarrier(cmdbuff,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0,nullptr,
        0,nullptr,
        2,barriers.data());
}




void SetupEngine(Engine &Engine){
    gchk(glfwInit());
    chk2(volkInitialize());
    
    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Learning_Vulkan",
        .apiVersion = VK_API_VERSION_1_3
    };
    
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = glfwExtensionCount,
        .ppEnabledExtensionNames = glfwExtensions,
    };
    chk2(vkCreateInstance(&instanceCI, nullptr, &Engine.Instance));
    volkLoadInstance(Engine.Instance);
}

VkPhysicalDevice GetDevice(Engine &Engine){
    uint32_t deviceCount{0};
    chk2(vkEnumeratePhysicalDevices(Engine.Instance, &deviceCount, nullptr));
    vector<VkPhysicalDevice> Devices(deviceCount);
    chk2(vkEnumeratePhysicalDevices(Engine.Instance, &deviceCount, Devices.data()));
    for(VkPhysicalDevice Device : Devices){
        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(Device, &prop);
        
        if (prop.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
            cout<<"Using: "<<prop.deviceName<<endl;
            return Device;
        }
    }
    VkPhysicalDeviceProperties prop1;
    vkGetPhysicalDeviceProperties(Devices[0], &prop1);
    cout<<"Using: "<<prop1.deviceName<<endl;
    return Devices[0];
}

void GetFormats(Engine &Engine){
    vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
    for (VkFormat& format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(Engine.PhysicalDevice, format, &formatProperties);
        if (formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            Engine.Depthformat = format;
            break;
        }
    }
    Engine.ImageFormat= VK_FORMAT_B8G8R8A8_SRGB;
}

void GetVirtualDeviceQueue(Engine &Engine){
    uint32_t queueFamilyCount{ 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(Engine.PhysicalDevice, &queueFamilyCount, nullptr);
    vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(Engine.PhysicalDevice, &queueFamilyCount, queueFamilies.data());
    uint32_t queueFamily{ 0 };
    for (size_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queueFamily = i;
            break;
        }
    }
    const float qfpriorities{ 1.0f };
    VkDeviceQueueCreateInfo queueinfo{
        .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=queueFamily,
        .queueCount=1,
        .pQueuePriorities=&qfpriorities
    };
    

    VkDeviceCreateInfo Deviceinfo{
        .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1,
        .pQueueCreateInfos=&queueinfo,
        .enabledExtensionCount=static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames=deviceExtensions.data()
    };
    vkCreateDevice(Engine.PhysicalDevice,&Deviceinfo,nullptr,&Engine.Device);
    vkGetDeviceQueue(Engine.Device, queueFamily, 0, &Engine.Queue);
    Engine.QueueFamily=queueFamily;
}

static vector<char> readFile(const string &filename){
	ifstream file(filename, ios::ate | ios::binary);
	if (!file.is_open())
	{
		throw runtime_error("failed to open file!");
	}
	vector<char> buffer(file.tellg());
	file.seekg(0, ios::beg);
	file.read(buffer.data(), static_cast<streamsize>(buffer.size()));
	file.close();
	return buffer;
}

VkShaderModule MakeShaderModule(Engine &engine, string filename){
    VkShaderModule shaderModule;
    string str(SLANGSHADERPATHFROMCMAKE);
    vector<char> code=readFile(str+"/"+filename);
    VkShaderModuleCreateInfo createInfo{.codeSize = code.size() * sizeof(char), .pCode = reinterpret_cast<const uint32_t *>(code.data())};
    chk2(vkCreateShaderModule(engine.Device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

void setupslang(Engine &Engine){
    ShaderContainer shader;
    shader.ShaderModules.push_back(MakeShaderModule(Engine,"slang.spv"));
    constexpr size_t VSShaderModule=0;
    constexpr size_t FRShaderModule=0;

    vector<VkPipelineShaderStageCreateInfo> shaderStages{
    { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = shader.ShaderModules[VSShaderModule], .pName = "VSMain"},
    { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = shader.ShaderModules[FRShaderModule], .pName = "FRMain" }
    };

    VkPushConstantRange pushConstantRange{.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = sizeof(extragpudata)};
    VkPipelineLayoutCreateInfo pipelinecreateinfo{.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange};
    chk2(vkCreatePipelineLayout(Engine.Device, &pipelinecreateinfo, nullptr, &shader.PipelineLayout));
    
    VkPipelineRenderingCreateInfo renderingCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &Engine.ImageFormat,
        .depthAttachmentFormat = Engine.Depthformat
    };
    VkVertexInputBindingDescription vertexBinding{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    vector<VkVertexInputAttributeDescription> vertexAttributes{
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
    };
    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates.data()
    };
    const VkPipelineRasterizationStateCreateInfo rasterizationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .cullMode=cullMode,
        .frontFace=frontface,
        .lineWidth = 1.0f,
    };
    VkGraphicsPipelineCreateInfo pipelineCI{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = &renderingCI,
    .stageCount = 2,
    .pStages = &shaderStages[0],
    .pVertexInputState = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pViewportState = &viewportState,
    .pRasterizationState = &rasterizationState,
    .pMultisampleState = &multisampleState,
    .pDepthStencilState = &depthStencilState,
    .pColorBlendState = &colorBlendState,
    .pDynamicState = &dynamicState,
    .layout = shader.PipelineLayout
    };
    chk2(vkCreateGraphicsPipelines(Engine.Device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &shader.Pipeline));

    Engine.Shader=shader;
}

Engine init(){
    Engine Engine;
    SetupEngine(Engine);
    Engine.PhysicalDevice=GetDevice(Engine);
    GetFormats(Engine);
    GetVirtualDeviceQueue(Engine);
    setupslang(Engine);

    VkCommandPoolCreateInfo commandPoolCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = Engine.QueueFamily};
    chk2(vkCreateCommandPool(Engine.Device, &commandPoolCI, nullptr, &Engine.CmdPool));
    VmaVulkanFunctions vkFunctions{ 
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr, 
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr, 
        .vkCreateImage = vkCreateImage
    };
    VmaAllocatorCreateInfo allocatorCI{.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, .physicalDevice = Engine.PhysicalDevice, .device = Engine.Device, .pVulkanFunctions = &vkFunctions, .instance = Engine.Instance};
    chk2(vmaCreateAllocator(&allocatorCI, &Engine.Allocator));
    
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
    Engine.displayw = mode->width;
    Engine.displayh = mode->height;
    return Engine;
}


void makedepthimage(Engine &engine, Window &window, int w, int h){
    VkImageCreateInfo depthimageinfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = engine.Depthformat,
        .extent{.width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h), .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocinfo{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    vmaCreateImage(engine.Allocator, &depthimageinfo, &allocinfo, &window.DepthImage, &window.DepthAlloc, nullptr);
    VkImageViewCreateInfo imageviewinfo{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .pNext =nullptr, .image=window.DepthImage, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format=engine.Depthformat, .subresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
    chk2(vkCreateImageView(engine.Device, &imageviewinfo, nullptr, &window.DepthImageView));
}

VkSwapchainKHR MakeSwapchain(Engine &Engine, int w, int h, VkSurfaceCapabilitiesKHR surfaceCaps, VkSurfaceKHR surface){
    VkSwapchainKHR swapchain;
    VkExtent2D swapchainExtent = surfaceCaps.currentExtent;
    if (surfaceCaps.currentExtent.width == UINT32_MAX) {
        swapchainExtent = { .width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h) };
    }

    VkSwapchainCreateInfoKHR swapchainCI{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = surfaceCaps.minImageCount,
        .imageFormat = Engine.ImageFormat,
        .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
        .imageExtent=swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR
    };
    chk2(vkCreateSwapchainKHR(Engine.Device, &swapchainCI, nullptr, &swapchain));
    return swapchain;
}

void RecreateSwapchain(Window &window, int width, int height){
    Engine &engine = *window.engine;
    chk2(vkDeviceWaitIdle(engine.Device));

    for (VkImageView view : window.imageviews) {
        vkDestroyImageView(engine.Device, view, nullptr);
    }
    vmaDestroyImage(engine.Allocator, window.DepthImage, window.DepthAlloc);
    vkDestroyImageView(engine.Device, window.DepthImageView, nullptr);
    window.imageviews.clear();
    window.images.clear();

    if (window.Swapchain != VK_NULL_HANDLE) {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine.PhysicalDevice, window.Surface, &window.SurfaceCapabilities);
    }

    VkSwapchainKHR oldSwapchain = window.Swapchain;

    VkExtent2D swapchainExtent = window.SurfaceCapabilities.currentExtent;
    if (swapchainExtent.width == UINT32_MAX) {
        swapchainExtent.width = static_cast<uint32_t>(width);
        swapchainExtent.height = static_cast<uint32_t>(height);
    }

    VkSwapchainCreateInfoKHR swapchainCI{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = window.Surface,
        .minImageCount = window.SurfaceCapabilities.minImageCount,
        .imageFormat = engine.ImageFormat,
        .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .oldSwapchain = oldSwapchain
    };

    chk2(vkCreateSwapchainKHR(engine.Device, &swapchainCI, nullptr, &window.Swapchain));
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(engine.Device, oldSwapchain, nullptr);
    }

    uint32_t imageCount = 0;
    chk2(vkGetSwapchainImagesKHR(engine.Device, window.Swapchain, &imageCount, nullptr));
    window.images.resize(imageCount);
    chk2(vkGetSwapchainImagesKHR(engine.Device, window.Swapchain, &imageCount, window.images.data()));
    window.imageviews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewCI{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = window.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = engine.ImageFormat,
            .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        };
        chk2(vkCreateImageView(engine.Device, &viewCI, nullptr, &window.imageviews[i]));
    }

    for (VkSemaphore semaphore : window.PresentationReadySemaphore) {
        vkDestroySemaphore(engine.Device, semaphore, nullptr);
    }
    window.PresentationReadySemaphore.clear();
    window.PresentationReadySemaphore.resize(imageCount);
    VkSemaphoreCreateInfo semaphoreCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (auto &semaphore : window.PresentationReadySemaphore) {
        chk2(vkCreateSemaphore(engine.Device, &semaphoreCI, nullptr, &semaphore));
    }
    makedepthimage(engine, window, width, height);
    window.width = width;
    window.height = height;
}

void ResizeCallback(GLFWwindow* glfwwindow, int width, int height){
    if (width == 0 || height == 0) {
        return;
    }

    Window *window = reinterpret_cast<Window *>(glfwGetWindowUserPointer(glfwwindow));
    if (!window || !window->engine) {
        return;
    }

    RecreateSwapchain(*window, width, height);
}

void addsemaphores(Engine &engine, Window &window){

    for(size_t i=0; i<maxframesinflight; i++){
        vkCreateFence(engine.Device, &fenceCI, nullptr, &window.FrameFence[i]);
        vkCreateSemaphore(engine.Device, &semaphoreCI, nullptr, &window.ImageAquiredSemaphore[i]);
    }
    window.PresentationReadySemaphore.resize(window.images.size());
	for (auto& semaphore : window.PresentationReadySemaphore) {
	chk2(vkCreateSemaphore(engine.Device, &semaphoreCI, nullptr, &semaphore));
	}
}

void MakeWindow(int w, int h, string name, Engine &Engine){
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    Window window{.ActiveImage=0, .FrameInFlight=0, .width=w, .height=h};
    window.GlfwWindow = glfwCreateWindow(w, h, name.c_str(), nullptr, nullptr);    
    
    //surface
    chk2(glfwCreateWindowSurface(Engine.Instance, window.GlfwWindow, nullptr, &window.Surface));
    chk2(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Engine.PhysicalDevice, window.Surface, &window.SurfaceCapabilities));
    
    //swapchain
    window.Swapchain = MakeSwapchain(Engine, w, h, window.SurfaceCapabilities, window.Surface);

    //cmdbuffer
    const VkCommandBufferAllocateInfo cmdbuffinfo{
        .sType =VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = Engine.CmdPool,
        .commandBufferCount = maxframesinflight
    };
    vkAllocateCommandBuffers(Engine.Device,&cmdbuffinfo,window.CmdBuffers.data());

    //imageview
    uint32_t imageCount{ 0 };

    chk2(vkGetSwapchainImagesKHR(Engine.Device, window.Swapchain, &imageCount, nullptr));
    window.images.resize(imageCount);
    chk2(vkGetSwapchainImagesKHR(Engine.Device, window.Swapchain, &imageCount, window.images.data()));
	window.imageviews.resize(imageCount);
	for (auto i = 0; i < imageCount; i++) {
		VkImageViewCreateInfo viewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = window.images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = Engine.ImageFormat, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
	chk2(vkCreateImageView(Engine.Device, &viewCI, nullptr, &window.imageviews[i]));
	}   

    window.engine = &Engine;

    makedepthimage(Engine, window, w, h);
    addsemaphores(Engine, window);
    Engine.Windows.push_back(window);

    Window &storedWindow = Engine.Windows.back();
    glfwSetWindowUserPointer(storedWindow.GlfwWindow, reinterpret_cast<void *>(&storedWindow));
    glfwSetFramebufferSizeCallback(storedWindow.GlfwWindow, ResizeCallback);
}


GpuBuffer MakeGPUBuffer(Engine &engine,VkBufferUsageFlags usageflags, const void *data, uint32_t size){
    VkBufferCreateInfo bufinfo{
        .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=size,
        .usage=usageflags
    };
    VmaAllocationCreateInfo alloccreateinfo{
        .flags=VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT|VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage=VMA_MEMORY_USAGE_AUTO
    };
    GpuBuffer buffer{.size=size};
    chk2(vmaCreateBuffer(engine.Allocator, &bufinfo, &alloccreateinfo, &buffer.vkbuffer, &buffer.alloc, &buffer.allocinfo));

    memcpy(buffer.allocinfo.pMappedData, data, size);
    chk2(vmaFlushAllocation(engine.Allocator, buffer.alloc, 0, size));
    
    return buffer;
}

GpuMesh MakeGPUMesh(vector<Vertex> vertecies,vector<uint32_t> indecies, Engine &engine){
    GpuMesh out{.vertecies=vertecies, .indecies=indecies};
    void* vptr = vertecies.data();
    void* iptr = indecies.data();
    uint32_t vsize=sizeof(Vertex)*vertecies.size();
    uint32_t isize=sizeof(uint32_t)*indecies.size();
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    out.VertexBuffer=MakeGPUBuffer(engine, usage, vptr, vsize);
    out.IndeciesBuffer=MakeGPUBuffer(engine, usage, iptr, isize);

    return out;
}

void DrawGPUMesh(Engine &Engine, Window &window,GpuMesh Mesh){
    VkCommandBuffer cmdbuff=window.CmdBuffers[window.FrameInFlight];
    vkCmdBindPipeline(cmdbuff, VK_PIPELINE_BIND_POINT_GRAPHICS, Engine.Shader.Pipeline);

    VkDeviceSize offset{0};
    vkCmdBindVertexBuffers(cmdbuff, 0,1,&Mesh.VertexBuffer.vkbuffer, &offset);
    vkCmdBindIndexBuffer(cmdbuff, Mesh.IndeciesBuffer.vkbuffer, offset, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmdbuff,Mesh.indecies.size(), 1,0,0,0);
}

void DeleteGPUMesh(GpuMesh mesh, Engine &engine){
    vmaDestroyBuffer(engine.Allocator, mesh.IndeciesBuffer.vkbuffer, mesh.IndeciesBuffer.alloc);
    vmaDestroyBuffer(engine.Allocator, mesh.VertexBuffer.vkbuffer, mesh.VertexBuffer.alloc);
}


void StartFrame(Engine &Engine, Window &window){
    glfwGetWindowSize(window.GlfwWindow, &window.width, &window.height);

    VkFence *currentframefence = &window.FrameFence[window.FrameInFlight];
    chk2(vkWaitForFences(Engine.Device, 1, currentframefence, true, 15000000000));
    chk2(vkResetFences(Engine.Device, 1, currentframefence));

    VkSemaphore AquiredFlag = window.ImageAquiredSemaphore[window.FrameInFlight];
    
    chk2(vkAcquireNextImageKHR(Engine.Device, window.Swapchain, 15000000000, AquiredFlag, nullptr, &window.ActiveImage));
   
    VkCommandBufferBeginInfo cmdbuffbeginfo{
        .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };


    chk2(vkResetCommandBuffer(window.CmdBuffers[window.FrameInFlight], 0));
    chk2(vkBeginCommandBuffer(window.CmdBuffers[window.FrameInFlight], &cmdbuffbeginfo));
}

void BeginRender(Engine &Engine, Window &window){
    

    VkRenderingAttachmentInfo attachinfo{
        .sType=VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView=window.imageviews[window.ActiveImage],
        .imageLayout=VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue={15.0f/255.0f, 15.0f/255.0f, 15.0f/255.0f,1}
    };
    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .imageView   = window.DepthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = 1.0f
    };
    VkRenderingInfo rendinfo{
        .sType=VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{.extent{.width=(uint32_t)window.width,.height=(uint32_t)window.height}},
        .layerCount=1,
        .colorAttachmentCount=1,
        .pColorAttachments=&attachinfo,
        .pDepthAttachment=&depthAttachmentInfo
    };

    swapchainImageBarriercmd(window.CmdBuffers[window.FrameInFlight], window);
    vkCmdBeginRendering(window.CmdBuffers[window.FrameInFlight],&rendinfo);
    viewportAndScissorcmd(window.CmdBuffers[window.FrameInFlight], window);
}

void RenderFrame(Engine &Engine, Window &window){
    StartFrame(Engine, window);
    BeginRender(Engine, window);
}

void submitCommands(VkQueue &queue, VkCommandBuffer &cmdbuff, Window &window){
    VkPipelineStageFlags waitstage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSubmitInfo submitinfo{
        .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount=1,
        .pWaitSemaphores=&window.ImageAquiredSemaphore[window.FrameInFlight],
        .pWaitDstStageMask=&waitstage,
        .commandBufferCount=1,
        .pCommandBuffers=&cmdbuff,
        .signalSemaphoreCount=1,
        .pSignalSemaphores=&window.PresentationReadySemaphore[window.ActiveImage]
    };

    chk2(vkQueueSubmit(queue, 1, &submitinfo, window.FrameFence[window.FrameInFlight]));
}

void EndFrame(Engine &engine, Window &window){
    VkCommandBuffer cmdbuff=window.CmdBuffers[window.FrameInFlight];
    vkCmdEndRendering(cmdbuff);
    addPresentationBarriercmd(cmdbuff, window);
    chk2(vkEndCommandBuffer(cmdbuff));
    submitCommands(engine.Queue, cmdbuff, window);
    
    VkPresentInfoKHR presentinfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
    	.pWaitSemaphores = &window.PresentationReadySemaphore[window.ActiveImage],
		.swapchainCount = 1,
		.pSwapchains = &window.Swapchain,
		.pImageIndices = &window.ActiveImage
    };
    chk2(vkQueuePresentKHR(engine.Queue, &presentinfo));

    window.FrameInFlight=(window.FrameInFlight+1)%maxframesinflight;
}


void Clean(Engine &Engine){
    chk2(vkDeviceWaitIdle(Engine.Device));
    for(Window &window : Engine.Windows){
        for (VkImageView imageView : window.imageviews) {
            vkDestroyImageView(Engine.Device, imageView, nullptr);
        }
        vmaDestroyImage(Engine.Allocator, window.DepthImage, window.DepthAlloc);
        vkDestroyImageView(Engine.Device, window.DepthImageView, nullptr);
        vkDestroySwapchainKHR(Engine.Device,window.Swapchain,nullptr);
        glfwDestroyWindow(window.GlfwWindow);
        for (size_t i = 0; i < maxframesinflight; i++) {
            vkDestroySemaphore(Engine.Device, window.ImageAquiredSemaphore[i], nullptr);
            vkDestroyFence(Engine.Device, window.FrameFence[i], nullptr);
        }
        for (VkSemaphore semaphore : window.PresentationReadySemaphore) {
            vkDestroySemaphore(Engine.Device, semaphore, nullptr);
        }
    }
    cout<<"Cleared Windows"<<endl;
    vmaDestroyAllocator(Engine.Allocator);
    for(VkShaderModule ShaderMod : Engine.Shader.ShaderModules){
        vkDestroyShaderModule(Engine.Device, ShaderMod, nullptr);
    }
    vkDestroyPipeline(Engine.Device, Engine.Shader.Pipeline, nullptr);
    vkDestroyPipelineLayout(Engine.Device, Engine.Shader.PipelineLayout, nullptr);
    vkDestroyCommandPool(Engine.Device, Engine.CmdPool, nullptr);
    vkDestroyDevice(Engine.Device, nullptr);
	vkDestroyInstance(Engine.Instance, nullptr);
    glfwTerminate();
}


Bounds getGlobalBoundingBox(tinyobj::attrib_t attrib) {

    // Initialize bounds to extreme values
    float minX = numeric_limits<float>::infinity();
    float minY = numeric_limits<float>::infinity();
    float minZ = numeric_limits<float>::infinity();
    
    float maxX = -numeric_limits<float>::infinity();
    float maxY = -numeric_limits<float>::infinity();
    float maxZ = -numeric_limits<float>::infinity();

    // Loop through the flattened vertices array
    for (size_t i = 0; i < attrib.vertices.size(); i += 3) {
        float vx = attrib.vertices[i + 0];
        float vy = attrib.vertices[i + 1];
        float vz = attrib.vertices[i + 2];

        minX = min(minX, vx);
        minY = min(minY, vy);
        minZ = min(minZ, vz);

        maxX = max(maxX, vx);
        maxY = max(maxY, vy);
        maxZ = max(maxZ, vz);
    }
    return Bounds{.min{minX,minY,minZ}, .max{maxX,maxY,maxZ}};
}

void getgpudatafromtinyobj(string path, string materialpath, GpuMesh &mesh, Bounds &bounds, Engine Engine, int debug=0){
    tinyobj::attrib_t attrib;
    vector<tinyobj::shape_t> shapes;
    vector<tinyobj::material_t> materials;
    vector<Vertex> Verticies;
    vector<uint32_t> indices;

    string warn;
    string err;
    
    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), materialpath.c_str());
    if (!warn.empty()) {
        cout << warn << endl;
        
    }

    if (!err.empty()) {
        cerr << err << endl;
    }

    if (!ret) {
        Clean(Engine);
        exit(1);
    }
    
    bounds= getGlobalBoundingBox(attrib);
    if(debug){
        cout<<"min:{";
        cout<<bounds.min[0]<<", ";
        cout<<bounds.min[1]<<", ";
        cout<<bounds.min[2];
        cout<<"}  max:{";
        cout<<bounds.max[0]<<", ";
        cout<<bounds.max[1]<<", ";
        cout<<bounds.max[2];
        cout<<"}"<<endl;
    }
    
    const size_t vertexCount = attrib.vertices.size() / 3;
    Verticies.reserve(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        if(i&255==255){
        cout << "\rVerteciesLoaded:" << i << "/" << vertexCount << "\t";
        }
        Vertex v{};
        v.pos = { attrib.vertices[3 * i], attrib.vertices[3 * i + 1], attrib.vertices[3 * i + 2] };
        if (attrib.normals.size() >= (3 * i + 3)) {
            v.normal = { attrib.normals[3 * i], attrib.normals[3 * i + 1], attrib.normals[3 * i + 2] };
        } else {
            v.normal = { 0.0f, 0.0f, 0.0f };
        }
        if (attrib.texcoords.size() >= (2 * i + 2)) {
            v.uv = { attrib.texcoords[2 * i], attrib.texcoords[2 * i + 1] };
        } else {
            v.uv = { 0.0f, 0.0f };
        }
        Verticies.push_back(v);
    }
    cout << "\rVerteciesLoaded:" << vertexCount << "/" << vertexCount<<endl;
    if(debug){
        cout<<"NormalsLoaded: "<<attrib.normals.size()/3<<"\tUVs loaded: "<<attrib.texcoords.size()/3<<endl;
    }
    if (!shapes.empty()) {
        for(tinyobj::shape_t shape : shapes){
            vector<tinyobj::index_t> tinyobjIndices = shape.mesh.indices;
            indices.reserve(tinyobjIndices.size());
            for (auto &index : tinyobjIndices) {
                indices.push_back(static_cast<uint32_t>(index.vertex_index));
            }
        }
    }
    
    mesh = MakeGPUMesh(Verticies, indices, Engine);
}


fpsdata makefps(){
    fpsdata f{.framecount=0, .deltat=0, .fps=0};
    auto now = chrono::steady_clock::now();
    f.time = now.time_since_epoch();
    return f;
}

void updatefps(fpsdata &fpsdata){
    fpsdata.framecount+=1;
    ostringstream frame;
    
    auto currentTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = currentTime.time_since_epoch() - fpsdata.time;

    fpsdata.deltat=elapsed.count();
    fpsdata.fps=1/fpsdata.deltat;
    fpsdata.time=currentTime.time_since_epoch();
    
    frame<<"\033[2K\rframe:"<<fpsdata.framecount<<"\tfps:"<<fpsdata.fps<<"\tdeltat:"<<fpsdata.deltat<<"\t";
    cout << frame.str() << flush;
}   




void RenderMain(){
    Engine Engine=init();
    MakeWindow(800,800,"Vulkan Test :D",Engine);

    string str(OBJPATHFROMCMAKE);
    
    string inputfile;
    cout<<endl<<endl<<"type filename in assets/models here e.g. /bunny.obj : ";
    cin>>inputfile;
    cout<<endl<<endl;
    Bounds bounds;
    GpuMesh mesh;
    getgpudatafromtinyobj(str+inputfile, str+"/materials",mesh, bounds, Engine, 1);

    extragpudata data{};
    data.bounds_min = vec4(bounds.min, 0.0f);
    data.bounds_max = vec4(bounds.max, 0.0f);
    data.offset = vec4(0.0f);
    data.rotation = vec4(0.0f); // rotation.w will store deltat
    data.scale = vec4(1.0f);

    fpsdata fps=makefps();

    cout<<"\033[?25l"<<endl;
    ostringstream frame;
    while (1==1){
        updatefps(fps);
        try{
            glfwPollEvents();
            
            if (glfwWindowShouldClose(Engine.Windows[0].GlfwWindow)){
                break;
            }

            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_UP)){
                data.rotation.x -= 3.0f * fps.deltat; 
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_DOWN)){
                data.rotation.x += 3.0f * fps.deltat; 
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_LEFT)){
                data.rotation.y -= 3.0f * fps.deltat; 
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_RIGHT)){
                data.rotation.y += 3.0f * fps.deltat; 
            }
            frame<<"theta x:"<<data.rotation.x<<"\ttheta y:"<<data.rotation.y<<"\t";

            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_W)){
                data.offset.y += 0.5f * fps.deltat;
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_S)){
                data.offset.y -= 0.5f * fps.deltat;
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_D)){
                data.offset.x += 0.5f * fps.deltat;
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_A)){
                data.offset.x -= 0.5f * fps.deltat;
            }
            cout << frame.str() << flush;
            frame.str(""); 
            frame.clear(); 
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_EQUAL)){
                data.scale.x += 0.5f * fps.deltat;
                data.scale.y += 0.5f * fps.deltat;
                data.scale.z += 0.5f * fps.deltat;
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_MINUS)){
                data.scale.x -= 0.5f * fps.deltat;
                data.scale.y -= 0.5f * fps.deltat;
                data.scale.z -= 0.5f * fps.deltat;
            }
            if(IsKeyPressed(Engine.Windows[0], GLFW_KEY_R)){
                data.offset = vec4(0.0f);
                data.rotation = vec4(0.0f);
                data.scale = vec4(1.0f);
            }

            RenderFrame(Engine, Engine.Windows[0]);
            // store deltat in rotation.w to match shader packing: float3 rotation; float deltat;
            data.rotation.w = static_cast<float>(fps.deltat);
            vkCmdPushConstants(Engine.Windows[0].CmdBuffers[Engine.Windows[0].FrameInFlight], Engine.Shader.PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(data), &data);
            DrawGPUMesh(Engine, Engine.Windows[0], mesh);
            EndFrame(Engine, Engine.Windows[0]);
        }
        catch(...){
            break;
        }
    }
    DeleteGPUMesh(mesh,Engine);
    cout<<"\nCleaning"<<endl;
    Clean(Engine);
    cout<<"Finished Cleaning Vulkan\033[?25h"<<endl;
}

int main(){
    RenderMain();
    return 0;
}