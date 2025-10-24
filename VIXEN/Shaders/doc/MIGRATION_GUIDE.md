# Migration Guide: Legacy API to Modern Builder API

This guide helps you migrate from the legacy VulkanShader API to the modern builder pattern API.

## Table of Contents
1. [Overview](#overview)
2. [Why Migrate?](#why-migrate)
3. [Migration Patterns](#migration-patterns)
4. [Common Scenarios](#common-scenarios)
5. [Step-by-Step Migration](#step-by-step-migration)
6. [Troubleshooting](#troubleshooting)

---

## Overview

The VulkanShader library has been significantly enhanced with new features while maintaining backward compatibility. The legacy API (BuildShader, BuildShaderModuleWithSPV) remains functional, but the new builder pattern API provides many advantages.

### Compatibility Promise

- **Legacy API remains functional** - Your existing code will continue to work
- **No breaking changes** - Legacy methods are marked deprecated but still supported
- **Gradual migration** - You can migrate one shader at a time

---

## Why Migrate?

### Benefits of New API

| Feature | Legacy API | New API |
|---------|-----------|---------|
| Multiple shader stages | ❌ Only vertex + fragment | ✅ All stages (compute, geometry, tess) |
| Preprocessor defines | ❌ Not supported | ✅ Full support |
| Shader caching | ❌ Not available | ✅ Automatic caching |
| Hot reloading | ❌ Not available | ✅ Built-in support |
| Include files | ❌ Not supported | ✅ Full #include support |
| Custom entry points | ❌ Only "main" | ✅ Any entry point |
| Optimization control | ❌ No control | ✅ Full control |
| Builder pattern | ❌ Multi-step calls | ✅ Chainable API |
| Error logging | ⚠️ Basic | ✅ Comprehensive |

---

## Migration Patterns

### Pattern 1: BuildShader (GLSL Source)

**Before (Legacy):**
```cpp
VulkanShader shader;
shader.BuildShader(vertexSourceCode, fragmentSourceCode);
```

**After (Modern):**
```cpp
VulkanShader shader;
shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertexSourceCode)
      .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentSourceCode)
      .Build();
```

---

### Pattern 2: BuildShaderModuleWithSPV (Pre-compiled SPIR-V)

**Before (Legacy):**
```cpp
uint32_t* vertSpv = /* ... */;
size_t vertSize = /* ... */;
uint32_t* fragSpv = /* ... */;
size_t fragSize = /* ... */;

VulkanShader shader;
shader.BuildShaderModuleWithSPV(vertSpv, vertSize, fragSpv, fragSize);
```

**After (Modern):**
```cpp
std::vector<uint32_t> vertSpirv(vertSpv, vertSpv + (vertSize / sizeof(uint32_t)));
std::vector<uint32_t> fragSpirv(fragSpv, fragSpv + (fragSize / sizeof(uint32_t)));

VulkanShader shader;
shader.AddStageSPV(VK_SHADER_STAGE_VERTEX_BIT, vertSpirv)
      .AddStageSPV(VK_SHADER_STAGE_FRAGMENT_BIT, fragSpirv)
      .Build();
```

---

### Pattern 3: Loading from Files

**Before (Custom File Loading):**
```cpp
// You had to write your own file loading
std::string vertSource = LoadTextFile("shader.vert");
std::string fragSource = LoadTextFile("shader.frag");

VulkanShader shader;
shader.BuildShader(vertSource.c_str(), fragSource.c_str());
```

**After (Built-in File Loading):**
```cpp
VulkanShader shader;
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
      .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
      .Build();
```

---

## Common Scenarios

### Scenario 1: Basic Vertex + Fragment Shader

**Before:**
```cpp
class Renderer {
    VulkanShader shader;

    void InitShaders() {
        std::string vertCode = ReadFile("../Shaders/Draw.vert");
        std::string fragCode = ReadFile("../Shaders/Draw.frag");
        shader.BuildShader(vertCode.c_str(), fragCode.c_str());
    }

    void Cleanup() {
        shader.DestroyShader();
    }
};
```

**After:**
```cpp
class Renderer {
    VulkanShader shader;

    void InitShaders() {
        shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "../Shaders/Draw.vert")
              .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "../Shaders/Draw.frag")
              .EnableCache("./shader_cache")  // Bonus: Add caching!
              .Build();
    }

    void Cleanup() {
        shader.DestroyShader();
    }
};
```

---

### Scenario 2: Using Wrapper ReadFile Function

**Before:**
```cpp
void CreateShader() {
    // Using the project's ReadFile wrapper
    bool vertSuccess = false;
    auto vertData = ReadFile("shader.vert.spv", &vertSuccess);
    bool fragSuccess = false;
    auto fragData = ReadFile("shader.frag.spv", &fragSuccess);

    if (vertSuccess && fragSuccess) {
        VulkanShader shader;
        shader.BuildShaderModuleWithSPV(
            reinterpret_cast<uint32_t*>(vertData.data()), vertData.size(),
            reinterpret_cast<uint32_t*>(fragData.data()), fragData.size()
        );
    }
}
```

**After:**
```cpp
void CreateShader() {
    VulkanShader shader;

    // Much simpler - file loading is built-in
    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert.spv")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag.spv")
          .Build();

    // Or load GLSL directly
    shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
          .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
          .Build();
}
```

---

### Scenario 3: Conditional Compilation

**Before (Not Possible):**
```cpp
// You had to maintain separate shader files for each variant
VulkanShader shaderWithTextures;
shaderWithTextures.BuildShader(
    ReadFile("shader_textured.vert").c_str(),
    ReadFile("shader_textured.frag").c_str()
);

VulkanShader shaderWithoutTextures;
shaderWithoutTextures.BuildShader(
    ReadFile("shader_untextured.vert").c_str(),
    ReadFile("shader_untextured.frag").c_str()
);
```

**After (Single Source with Defines):**
```cpp
// Use single shader source with defines
VulkanShader shaderWithTextures;
shaderWithTextures.AddDefine("USE_TEXTURES", "1")
                  .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
                  .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
                  .Build();

VulkanShader shaderWithoutTextures;
shaderWithoutTextures.AddDefine("USE_TEXTURES", "0")
                     .AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shader.vert")
                     .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shader.frag")
                     .Build();
```

---

## Step-by-Step Migration

### Step 1: Identify Usage

Find all places where you use the legacy API:

```bash
# Search for legacy API usage
grep -r "BuildShader(" ./src
grep -r "BuildShaderModuleWithSPV(" ./src
```

### Step 2: Update Includes

No changes needed - same header file:

```cpp
#include "VulkanShader.h"  // Still the same
```

### Step 3: Replace Legacy Calls

Replace each legacy call with the builder pattern:

**Legacy:**
```cpp
shader.BuildShader(vertSource, fragSource);
```

**Modern:**
```cpp
shader.AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertSource)
      .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragSource)
      .Build();
```

### Step 4: Add New Features (Optional)

Take advantage of new features:

```cpp
shader.AddDefine("PLATFORM", "VULKAN")
      .EnableCache("./shader_cache")
      .AddStage(VK_SHADER_STAGE_VERTEX_BIT, vertSource)
      .AddStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragSource)
      .Build();
```

### Step 5: Test Thoroughly

Verify that:
- Shaders compile successfully
- Rendering looks identical
- No performance regression
- Error handling works

---

## Detailed Examples

### Example 1: Migrating VulkanRenderer::CreateShaders

**Before:**
```cpp
void VulkanRenderer::CreateShaders() {
#ifdef AUTO_COMPILE_GLSL_TO_SPV
    // Read shader source files
    bool vertShaderResult = false;
    bool fragShaderResult = false;

    std::vector<unsigned char> vertShaderCode =
        ReadFile("../Shaders/Draw.vert", &vertShaderResult);
    std::vector<unsigned char> fragShaderCode =
        ReadFile("../Shaders/Draw.frag", &fragShaderResult);

    shaderObj = std::make_unique<VulkanShader>();
    shaderObj->BuildShader(
        reinterpret_cast<const char*>(vertShaderCode.data()),
        reinterpret_cast<const char*>(fragShaderCode.data())
    );
#else
    // Read SPIR-V files
    bool vertShaderResult = false;
    bool fragShaderResult = false;

    std::vector<unsigned char> vertShaderCode =
        ReadFile("../Shaders/Draw.vert.spv", &vertShaderResult);
    std::vector<unsigned char> fragShaderCode =
        ReadFile("../Shaders/Draw.frag.spv", &fragShaderResult);

    shaderObj = std::make_unique<VulkanShader>();
    shaderObj->BuildShaderModuleWithSPV(
        reinterpret_cast<uint32_t*>(vertShaderCode.data()),
        vertShaderCode.size(),
        reinterpret_cast<uint32_t*>(fragShaderCode.data()),
        fragShaderCode.size()
    );
#endif
}
```

**After:**
```cpp
void VulkanRenderer::CreateShaders() {
    shaderObj = std::make_unique<VulkanShader>();

    // Auto-detects .spv or .glsl based on file extension
    // Compiles GLSL automatically if AUTO_COMPILE_GLSL_TO_SPV is defined
    shaderObj->AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "../Shaders/Draw.vert")
             .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "../Shaders/Draw.frag")
             .EnableCache("../Shaders/.cache")
             .Build();

    if (!shaderObj->IsInitialized()) {
        throw std::runtime_error("Failed to create shaders");
    }
}
```

**Benefits:**
- 60% less code
- No `#ifdef` needed
- Automatic file type detection
- Built-in error checking
- Caching support

---

### Example 2: Multiple Shaders in Application

**Before:**
```cpp
class Application {
    VulkanShader mainShader;
    VulkanShader shadowShader;
    VulkanShader uiShader;

    void LoadShaders() {
        // Main shader
        auto mainVert = LoadTextFile("main.vert");
        auto mainFrag = LoadTextFile("main.frag");
        mainShader.BuildShader(mainVert.c_str(), mainFrag.c_str());

        // Shadow shader
        auto shadowVert = LoadTextFile("shadow.vert");
        auto shadowFrag = LoadTextFile("shadow.frag");
        shadowShader.BuildShader(shadowVert.c_str(), shadowFrag.c_str());

        // UI shader
        auto uiVert = LoadTextFile("ui.vert");
        auto uiFrag = LoadTextFile("ui.frag");
        uiShader.BuildShader(uiVert.c_str(), uiFrag.c_str());
    }
};
```

**After:**
```cpp
class Application {
    VulkanShader mainShader;
    VulkanShader shadowShader;
    VulkanShader uiShader;

    void LoadShaders() {
        // Main shader
        mainShader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "main.vert")
                  .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "main.frag")
                  .EnableCache()
                  .Build();

        // Shadow shader
        shadowShader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "shadow.vert")
                    .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "shadow.frag")
                    .EnableCache()
                    .Build();

        // UI shader
        uiShader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "ui.vert")
                .AddStageFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "ui.frag")
                .EnableCache()
                .Build();
    }
};
```

---

## Troubleshooting

### Problem: Compilation Errors After Migration

**Symptom:**
```
error: 'BuildShader' is deprecated
```

**Solution:**
This is just a warning. The code still works. To remove the warning, complete the migration to the new API.

---

### Problem: Shaders Don't Compile

**Symptom:**
```
Build() returns false
```

**Solution:**
Add logging to see detailed error messages:

```cpp
auto logger = std::make_shared<Logger>("ShaderDebug");
shader.SetLogger(logger);

if (!shader.Build()) {
    std::cout << logger->ExtractLogs() << std::endl;
}
```

---

### Problem: Performance Regression

**Symptom:**
Startup time increased after migration

**Solution:**
Enable caching:

```cpp
shader.EnableCache("./shader_cache")
      .Build();
```

First run will be slower (compilation), subsequent runs will be faster (cache hit).

---

### Problem: File Not Found Errors

**Symptom:**
```
Shader file not found: shader.vert
```

**Solution:**
Check file paths are correct. The new API uses paths relative to working directory:

```cpp
// If your working directory is /build
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "../shaders/shader.vert")
```

Or use absolute paths:

```cpp
shader.AddStageFromFile(VK_SHADER_STAGE_VERTEX_BIT, "/full/path/to/shader.vert")
```

---

## Migration Checklist

Use this checklist when migrating each shader:

- [ ] Replaced `BuildShader` with `AddStage` + `Build`
- [ ] Replaced `BuildShaderModuleWithSPV` with `AddStageSPV` + `Build`
- [ ] Updated file loading to use `AddStageFromFile`
- [ ] Added error checking with `IsInitialized()`
- [ ] Enabled caching with `EnableCache()`
- [ ] Added logging with `SetLogger()`
- [ ] Tested shader in application
- [ ] Verified rendering is identical
- [ ] Removed old file loading code if using `AddStageFromFile`

---

## Best Practices After Migration

1. **Always check `Build()` return value**
   ```cpp
   if (!shader.Build()) {
       // Handle error
   }
   ```

2. **Use caching in production**
   ```cpp
   shader.EnableCache("./shader_cache");
   ```

3. **Add logging for debugging**
   ```cpp
   shader.SetLogger(appLogger);
   ```

4. **Use defines for variants**
   ```cpp
   shader.AddDefine("QUALITY_LEVEL", std::to_string(qualityLevel));
   ```

5. **Enable hot reloading in development**
   ```cpp
   #ifdef DEBUG
   if (shader.HasSourceChanged()) {
       shader.HotReload();
   }
   #endif
   ```

---

## Summary

The migration from legacy to modern API is straightforward and brings significant benefits:

- ✅ Same functionality, better API
- ✅ Backward compatible
- ✅ Many new features available
- ✅ Cleaner, more maintainable code
- ✅ Better error handling
- ✅ Performance improvements with caching

Start migrating one shader at a time, and gradually take advantage of the new features!
