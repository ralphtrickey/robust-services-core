set(PROJECT_NAME cb)

################################################################################
# Source groups
################################################################################
set(Header_Files
    "BcAddress.h"
    "BcCause.h"
    "BcProgress.h"
    "BcProtocol.h"
    "BcRouting.h"
    "BcSessions.h"
    "CbModule.h"
    "ProxyBcSessions.h"
    "ServiceCodeRegistry.h"
)
source_group("Header Files" FILES ${Header_Files})

set(Source_Files
    "BcAddress.cpp"
    "BcCause.cpp"
    "BcEvents.cpp"
    "BcFactory.cpp"
    "BcHandlers.cpp"
    "BcProgress.cpp"
    "BcProtocol.cpp"
    "BcRouting.cpp"
    "BcService.cpp"
    "BcSsm.cpp"
    "BcStates.cpp"
    "BcTriggers.cpp"
    "CbModule.cpp"
    "DigitString.cpp"
    "ProxyBcSessions.cpp"
    "ServiceCodeRegistry.cpp"
    "TestCallFactory.cpp"
)
source_group("Source Files" FILES ${Source_Files})

set(ALL_FILES
    ${Header_Files}
    ${Source_Files}
)

################################################################################
# Target
################################################################################
add_library(${PROJECT_NAME} STATIC ${ALL_FILES})

use_props(${PROJECT_NAME} "${CMAKE_CONFIGURATION_TYPES}" "${DEFAULT_CXX_PROPS}")

set(ROOT_NAMESPACE cb)

if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VS_GLOBAL_KEYWORD "Win32Proj"
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE "TRUE"
    )
endif()

################################################################################
# Include directories
################################################################################
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../nb;"
    "${CMAKE_CURRENT_SOURCE_DIR}/../nw;"
    "${CMAKE_CURRENT_SOURCE_DIR}/../sb;"
    "${CMAKE_CURRENT_SOURCE_DIR}/../st;"
    "${CMAKE_CURRENT_SOURCE_DIR}/../mb"
)

################################################################################
# Compile definitions
################################################################################
if("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "ARM")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Debug>:"
            "_DEBUG"
        ">"
        "$<$<CONFIG:Release>:"
            "NDEBUG"
        ">"
        "WIN32;"
        "_LIB;"
        "UNICODE;"
        "_UNICODE"
    )
elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "x64")
    if(MSVC)
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            "$<$<CONFIG:Debug>:"
                "_DEBUG"
            ">"
            "$<$<CONFIG:Release>:"
                "NDEBUG"
            ">"
            "_LIB;"
            "UNICODE;"
            "_UNICODE"
        )
    endif()
elseif("${CMAKE_VS_PLATFORM_NAME}" STREQUAL "Win32")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "$<$<CONFIG:Debug>:"
            "_DEBUG"
        ">"
        "$<$<CONFIG:Release>:"
            "NDEBUG"
        ">"
        "WIN32;"
        "_LIB;"
        "UNICODE;"
        "_UNICODE"
    )
endif()

################################################################################
# Compile and link options
################################################################################
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /Oi;
            /Gy
        >
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:
            /OPT:REF;
            /OPT:NOICF
        >
        /SUBSYSTEM:WINDOWS
    )
endif()

################################################################################
# Dependencies
################################################################################
add_dependencies(${PROJECT_NAME}
    nt
)

# Link with other targets.
target_link_libraries(${PROJECT_NAME} PUBLIC
    mb
    nb
    nw
    sb
    st
)
