
add_definitions(
    -DUSE_WIN32_OPENGL)

spec2def(d3d9.dll d3d9.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    buffer.c
    d3d9_main.c
    device.c
    directx.c
    query.c
    shader.c
    stateblock.c
    surface.c
    swapchain.c
    texture.c
    vertexdeclaration.c
    volume.c)

list(APPEND PCH_SKIP_SOURCE
    guid.c)

add_library(d3d9 MODULE
    ${SOURCE}
    ${PCH_SKIP_SOURCE}
    version.rc
    ${CMAKE_CURRENT_BINARY_DIR}/d3d9_stubs.c
    ${CMAKE_CURRENT_BINARY_DIR}/d3d9.def)

if(MSVC)
    # Disable C5286: implicit conversion from enum type 'x' to enum type 'y';
    target_compile_options(d3d9 PRIVATE /wd5286)
endif()

set_module_type(d3d9 win32dll UNICODE)
target_link_libraries(d3d9 wine)
add_importlibs(d3d9 d3dwine user32 msvcrt kernel32 ntdll)
add_pch(d3d9 precomp.h "${PCH_SKIP_SOURCE}")
add_cd_file(TARGET d3d9 DESTINATION reactos/system32 FOR all)
set_wine_module_FIXME(d3d9) # CORE-5743: No ARRAY_SIZE and CONST_VTABLE
