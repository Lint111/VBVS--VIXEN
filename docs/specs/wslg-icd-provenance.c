/* ICD PROVENANCE: name the loaded driver from DEVICE PROPERTIES, never from the env var.
   Mirrors DeviceNode's selection rule (prefer first discrete) so it reports the SAME device the
   engine would pick. Uses three independent property-side signals:
     - deviceName / driverName        (VK_KHR_driver_properties, when present)
     - driverID enum
     - VK_MSFT_layered_driver         (the codebase's existing Dozen marker, VulkanSwapChain.cpp:207)
*/
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* driverIdName(VkDriverId id){
  switch((int)id){
    case VK_DRIVER_ID_MESA_LLVMPIPE: return "MESA_LLVMPIPE (lavapipe, software)";
    case VK_DRIVER_ID_MESA_DOZEN:    return "MESA_DOZEN (dzn, Vulkan-over-D3D12)";
    case VK_DRIVER_ID_NVIDIA_PROPRIETARY: return "NVIDIA_PROPRIETARY";
    case VK_DRIVER_ID_MESA_RADV:     return "MESA_RADV";
    case VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA: return "INTEL_MESA";
    case 0: return "(not reported)";
    default: return "(other)";
  }
}
int main(void){
  VkApplicationInfo ai={VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion=VK_API_VERSION_1_2;
  VkInstanceCreateInfo ici={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo=&ai;
  VkInstance inst;
  if(vkCreateInstance(&ici,NULL,&inst)!=VK_SUCCESS){printf("ICD-PROVENANCE: vkCreateInstance FAILED\n");return 1;}
  uint32_t n=0; vkEnumeratePhysicalDevices(inst,&n,NULL);
  if(n==0){printf("ICD-PROVENANCE: no physical devices\n");return 2;}
  VkPhysicalDevice g[8]; if(n>8)n=8; vkEnumeratePhysicalDevices(inst,&n,g);
  /* DeviceNode's rule: prefer the first DISCRETE gpu, else index 0 */
  uint32_t sel=0;
  for(uint32_t i=0;i<n;i++){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(g[i],&p);
    if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){sel=i;break;}}

  VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(g[sel],&props);

  /* driver properties (core 1.2) */
  VkPhysicalDeviceDriverProperties dp={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
  VkPhysicalDeviceProperties2 p2={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; p2.pNext=&dp;
  vkGetPhysicalDeviceProperties2(g[sel],&p2);

  /* the codebase's own Dozen marker */
  int layered=0; uint32_t ec=0;
  vkEnumerateDeviceExtensionProperties(g[sel],NULL,&ec,NULL);
  VkExtensionProperties*e=malloc(sizeof(*e)*ec);
  vkEnumerateDeviceExtensionProperties(g[sel],NULL,&ec,e);
  for(uint32_t i=0;i<ec;i++) if(!strcmp(e[i].extensionName,"VK_MSFT_layered_driver")) layered=1;

  printf("ICD-PROVENANCE (from device properties, not env):\n");
  printf("  physical devices visible : %u   (engine would select index %u)\n",n,sel);
  printf("  deviceName               : %s\n",props.deviceName);
  printf("  deviceType               : %d (%s)\n",props.deviceType,
         props.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU?"DISCRETE":
         props.deviceType==VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU?"INTEGRATED":
         props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU?"CPU (software)":"other");
  printf("  driverID                 : %d %s\n",(int)dp.driverID,driverIdName(dp.driverID));
  printf("  driverName / info        : %s / %s\n",dp.driverName,dp.driverInfo);
  printf("  VK_MSFT_layered_driver   : %s\n",layered?"PRESENT (Dozen marker)":"absent");
  printf("  apiVersion               : %u.%u.%u\n",VK_VERSION_MAJOR(props.apiVersion),
         VK_VERSION_MINOR(props.apiVersion),VK_VERSION_PATCH(props.apiVersion));
  printf("  VERDICT                  : %s\n",
    (dp.driverID==VK_DRIVER_ID_MESA_DOZEN||layered) ? "DZN (real GPU via D3D12)" :
    (dp.driverID==VK_DRIVER_ID_MESA_LLVMPIPE||props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)
      ? "LAVAPIPE (SOFTWARE -- not the 0ep.3 target)" : "OTHER");
  return 0;
}
