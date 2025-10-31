# ShaderManagement Tests

The ShaderManagement tests are located in the library-local directory:
```
ShaderManagement/tests/
```

This follows the best practice of keeping tests co-located with library source code.

## Running Tests

### Via CTest (from build directory)
```bash
cd build
ctest -R Shader              # Run all ShaderManagement tests
ctest -R SdiLifecycle        # Run SDI lifecycle tests only
ctest -R ShaderCompiler      # Run compiler tests only
ctest -R ShaderCacheManager  # Run cache tests only
```

### Directly
```bash
cd build/ShaderManagement/tests/Debug
./ShaderManagementTests.exe                    # Run all tests
./ShaderManagementTests.exe --gtest_filter="SdiLifecycle*"  # Run specific suite
```

## Test Suites

**ShaderCompilerTest** (14 tests)
- Vertex/fragment shader compilation
- File-based compilation
- Optimization and debug info flags
- SPIR-V validation and disassembly
- Error handling

**ShaderCacheManagerTest** (15 tests)
- Cache storage and retrieval
- Persistence across instances
- Key management and invalidation
- Performance testing

**SdiLifecycleTest** (6 tests, 5 enabled)
- Complete GLSL → SPIR-V → SDI workflow
- Descriptor set and binding validation
- Registry integration (1 disabled due to mutex issue)
- Hot-reload simulation
- Error handling

## VS Code Test Explorer

If ShaderManagement tests don't appear in VS Code Test Explorer:
1. Reload the CMake project: `Ctrl+Shift+P` → "CMake: Delete Cache and Reconfigure"
2. Refresh test explorer
3. Tests should appear under "ShaderManagement Tests"

Alternatively, run tests via terminal (always works):
```bash
cd build && ctest -R Shader --output-on-failure
```

## Integration

This `tests/ShaderManagement/` directory exists as a symbolic link for VS Code's benefit. The actual test implementation and build configuration is in `ShaderManagement/tests/CMakeLists.txt`.
