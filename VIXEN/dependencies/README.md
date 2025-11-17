# Dependencies

Third-party external libraries fetched via CMake FetchContent.

## Included Libraries

- **GLM** - OpenGL Mathematics library
- **Vulkan Headers** - Khronos Vulkan header files
- **GLI** - Compressed texture loading (DDS/KTX)
- **STB** - Uncompressed image loading (PNG/JPG)
- **Hash Provider** - OpenSSL or stbrumme/hash-library fallback
- **GoogleTest** - Unit testing framework (if BUILD_TESTS enabled)

## Organization

All FetchContent declarations and third-party setup are isolated here to keep the main CMakeLists.txt clean and focused on project structure.
