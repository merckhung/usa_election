#include "src/render/vk_loader.h"

#include <dlfcn.h>

namespace usa::vk {

#define USA_VK_DEFINE(name) PFN_##name name = nullptr;
USA_VK_GLOBAL_FUNCS(USA_VK_DEFINE)
USA_VK_INSTANCE_FUNCS(USA_VK_DEFINE)
USA_VK_DEVICE_FUNCS(USA_VK_DEFINE)
#undef USA_VK_DEFINE
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

bool LoadVulkanLoader() {
  if (vkGetInstanceProcAddr) return true;
  const char* names[] = {"libvulkan.so.1", "libvulkan.so", "libvulkan.1.dylib",
                         "libMoltenVK.dylib"};
  void* lib = nullptr;
  for (const char* n : names) {
    lib = dlopen(n, RTLD_NOW | RTLD_LOCAL);
    if (lib) break;
  }
  if (!lib) return false;
  vkGetInstanceProcAddr =
      reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
  if (!vkGetInstanceProcAddr) return false;
#define USA_VK_LOAD_GLOBAL(name) \
  name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(nullptr, #name));
  USA_VK_GLOBAL_FUNCS(USA_VK_LOAD_GLOBAL)
#undef USA_VK_LOAD_GLOBAL
  return vkCreateInstance != nullptr;
}

void LoadInstanceFunctions(VkInstance instance) {
#define USA_VK_LOAD_INSTANCE(name) \
  name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
  USA_VK_INSTANCE_FUNCS(USA_VK_LOAD_INSTANCE)
#undef USA_VK_LOAD_INSTANCE
}

void LoadDeviceFunctions(VkDevice device) {
#define USA_VK_LOAD_DEVICE(name) \
  name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
  USA_VK_DEVICE_FUNCS(USA_VK_LOAD_DEVICE)
#undef USA_VK_LOAD_DEVICE
}

const char* ResultString(VkResult r) {
  switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VK_ERROR_(other)";
  }
}

}  // namespace usa::vk
