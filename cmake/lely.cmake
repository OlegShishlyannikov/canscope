set(LELY_INSTALL_DIR ${CMAKE_BINARY_DIR}/lely-install)
file(MAKE_DIRECTORY ${LELY_INSTALL_DIR}/include)

if(BUILD_SHARED_LIBS)
  set(LELY_SHARED_FLAG "--enable-shared;--disable-static")
  set(LELY_LIB_TYPE SHARED)
  set(LELY_LIB_SUFFIX .so)
else()
  set(LELY_SHARED_FLAG "--disable-shared;--enable-static")
  set(LELY_LIB_TYPE STATIC)
  set(LELY_LIB_SUFFIX .a)
endif()

set(LELY_CFLAGS "-Wno-error -Wno-implicit-function-declaration -Wno-unterminated-string-initialization -DATOMIC_VAR_INIT=")
set(LELY_CXXFLAGS "-Wno-error")

if(CMAKE_CROSSCOMPILING)
  set(LELY_HOST_FLAG "--host=${CMAKE_C_COMPILER_TARGET}")
  set(LELY_CROSS_FLAGS "--target=${CMAKE_C_COMPILER_TARGET}")
  if(CMAKE_SYSROOT)
    string(APPEND LELY_CROSS_FLAGS " --sysroot=${CMAKE_SYSROOT}")
  endif()
  set(LELY_CC "${CMAKE_C_COMPILER} ${LELY_CROSS_FLAGS}")
  set(LELY_CXX "${CMAKE_CXX_COMPILER} ${LELY_CROSS_FLAGS}")
else()
  set(LELY_HOST_FLAG "")
  set(LELY_CC "")
  set(LELY_CXX "")
endif()

include(ExternalProject)
set(LELY_BYPRODUCTS)
foreach(_lib can co coapp util ev io2)
  list(APPEND LELY_BYPRODUCTS ${LELY_INSTALL_DIR}/lib/liblely-${_lib}${LELY_LIB_SUFFIX})
endforeach()

set(LELY_ENV_VARS "CFLAGS=${LELY_CFLAGS}" "CXXFLAGS=${LELY_CXXFLAGS}")
if(LELY_CC)
  list(APPEND LELY_ENV_VARS "CC=${LELY_CC}" "CXX=${LELY_CXX}" "LDFLAGS=-fuse-ld=lld")
endif()

ExternalProject_Add(lely_core_ext
  SOURCE_DIR        ${lely_core_SOURCE_DIR}
  BUILD_IN_SOURCE   TRUE
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env ${LELY_ENV_VARS}
                    autoreconf -i ${lely_core_SOURCE_DIR}
            COMMAND ${CMAKE_COMMAND} -E env ${LELY_ENV_VARS}
                    ${lely_core_SOURCE_DIR}/configure
                    --prefix=${LELY_INSTALL_DIR}
                    ${LELY_HOST_FLAG}
                    ${LELY_SHARED_FLAG}
                    --disable-dependency-tracking
                    --disable-python
                    --disable-tests
                    --disable-doc
  BUILD_COMMAND     ${CMAKE_COMMAND} -E env ${LELY_ENV_VARS}
                    make -j
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS  ${LELY_BYPRODUCTS}
)

foreach(_lib can co coapp util ev io2)
  add_library(lely::${_lib} ${LELY_LIB_TYPE} IMPORTED GLOBAL)
  set_target_properties(lely::${_lib} PROPERTIES
    IMPORTED_LOCATION ${LELY_INSTALL_DIR}/lib/liblely-${_lib}${LELY_LIB_SUFFIX}
    INTERFACE_INCLUDE_DIRECTORIES ${LELY_INSTALL_DIR}/include
  )
  add_dependencies(lely::${_lib} lely_core_ext)
endforeach()
