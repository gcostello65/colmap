set(FOLDER_NAME "vulkan")

COLMAP_ADD_LIBRARY(
        NAME colmap_vulkan
        SRCS
        vulkan_context.h
        vulkan_context.cc
        vma.cc
        PRIVATE_LINK_LIBS
        Vulkan::Vulkan
)

install(TARGETS colmap_vulkan
        EXPORT colmap-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

target_link_libraries(colmap_vulkan
        PUBLIC
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
)


target_include_directories(colmap_vulkan SYSTEM PRIVATE
        ${vma_SOURCE_DIR}/include
)

if(VULKAN_ENABLED)
    find_program(COLMAP_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin")
    if(COLMAP_GLSLC)
        set(_shader_compiler "${COLMAP_GLSLC}")
        set(_shader_flags --target-env=vulkan1.1)
    else()
        find_program(COLMAP_GLSLANG_VALIDATOR glslangValidator
                HINTS "$ENV{VULKAN_SDK}/bin")
        if(NOT COLMAP_GLSLANG_VALIDATOR)
            message(FATAL_ERROR "Vulkan shaders require glslc or glslangValidator")
        endif()
        set(_shader_compiler "${COLMAP_GLSLANG_VALIDATOR}")
        set(_shader_flags -V --target-env vulkan1.1)
    endif()

    set(COLMAP_VULKAN_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(GLOB _shader_sources CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp")
    set(_shader_outputs)
    foreach(_shader IN LISTS _shader_sources)
        get_filename_component(_shader_name "${_shader}" NAME)
        set(_spirv "${COLMAP_VULKAN_SHADER_DIR}/${_shader_name}.spv")
        add_custom_command(
                OUTPUT "${_spirv}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${COLMAP_VULKAN_SHADER_DIR}"
                COMMAND "${_shader_compiler}" ${_shader_flags} "${_shader}" -o "${_spirv}"
                DEPENDS "${_shader}" "${_shader_compiler}"
                COMMENT "Compiling Vulkan shader ${_shader_name}"
                VERBATIM
        )
        list(APPEND _shader_outputs "${_spirv}")
    endforeach()
    add_custom_target(colmap_vulkan_shaders DEPENDS ${_shader_outputs})
    add_dependencies(colmap_vulkan colmap_vulkan_shaders)
    target_compile_definitions(colmap_vulkan PRIVATE
            COLMAP_VULKAN_SHADER_DIR="${COLMAP_VULKAN_SHADER_DIR}")
endif()
