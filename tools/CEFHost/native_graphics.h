/* Native loader preflight; ANGLE retains final feature/device selection. */
#ifndef CEF_HOST_NATIVE_GRAPHICS_H
#define CEF_HOST_NATIVE_GRAPHICS_H
#include <stdlib.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
extern VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ArosVulkanGetInstanceProcAddr(
    VkInstance instance, const char *name) __asm__("vkGetInstanceProcAddr");

static int native_vulkan_available(void)
{
    PFN_vkGetInstanceProcAddr proc = ArosVulkanGetInstanceProcAddr;
    PFN_vkEnumerateInstanceVersion version_fn =
        (PFN_vkEnumerateInstanceVersion)proc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    uint32_t version = VK_API_VERSION_1_0;
    if (!version_fn || version_fn(&version) != VK_SUCCESS || version < VK_API_VERSION_1_1) {
        KPrintF("[CEF-HOST] graphics: Vulkan 1.1 unavailable; using software rendering\n");
        return 0;
    }
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)proc(VK_NULL_HANDLE, "vkCreateInstance");
    const char *extensions[] = {"VK_KHR_surface", "VK_AROS_surface"};
    VkApplicationInfo app = {0};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create ? create(&info, NULL, &instance) : VK_ERROR_INITIALIZATION_FAILED;
    if (result != VK_SUCCESS) {
        KPrintF("[CEF-HOST] graphics: Vulkan instance unavailable (%ld); using software rendering\n", (LONG)result);
        return 0;
    }
    PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)proc(instance, "vkDestroyInstance");
    PFN_vkEnumeratePhysicalDevices enumerate = (PFN_vkEnumeratePhysicalDevices)proc(instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties = (PFN_vkGetPhysicalDeviceProperties)proc(instance, "vkGetPhysicalDeviceProperties");
    uint32_t count = 0;
    int available = 0;
    if (destroy && enumerate && properties &&
        enumerate(instance, &count, NULL) == VK_SUCCESS && count && count <= 1024) {
        VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
        if (devices) {
            result = enumerate(instance, &count, devices);
            if (result == VK_SUCCESS || result == VK_INCOMPLETE) {
                for (uint32_t i = 0; i < count; ++i) {
                    VkPhysicalDeviceProperties caps;
                    properties(devices[i], &caps);
                    if (caps.apiVersion >= VK_API_VERSION_1_1) available = 1;
                }
            }
            free(devices);
        }
    }
    if (destroy) destroy(instance, NULL);
    KPrintF("[CEF-HOST] graphics: %s\n", available ?
        "Vulkan device available; selecting ANGLE Vulkan" :
        "no compatible Vulkan device; using software rendering");
    return available;
}
#endif
