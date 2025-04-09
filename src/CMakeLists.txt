list(APPEND FILES
  DataArray1D.h
  Exception.h
  ncvalues.cpp
  netcdf.cpp
  netcdfcpp.h
  Announce.h
  Announce.cpp
  Defines.h
  DataArray1D.h
  DataArray2D.h
  Subscript.h
  CoordTransforms.h
  GridElements.h
  GridElements.cpp
  GaussQuadrature.h
  GaussQuadrature.cpp
  STLStringHelper.h
  LegendrePolynomial.h
  LegendrePolynomial.cpp
  Units.h
  Constants.h
  meshrender.cpp
  ncvalues.h
  stb_image.h
  kdtree.h
  kdtree.cpp
)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${NetCDF_C_INCLUDE_DIR}
  ${GLEW_INCLUDE_DIRS}
)

add_executable(meshrender ${FILES})
target_include_directories(meshrender PRIVATE ${NetCDF_C_INCLUDE_DIR} ${GLEW_INCLUDE_DIRS})
target_link_libraries(meshrender PRIVATE NetCDF::NetCDF_C glfw GLEW::glew)

install(
  TARGETS
    meshrender
  RUNTIME DESTINATION bin
)

