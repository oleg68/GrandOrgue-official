# Copyright 2006 Milan Digital Audio LLC
# Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
# License GPL-2.0 or later (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).

cmake_minimum_required(VERSION 3.10)

IF(NOT CMAKE_CROSSCOMPILING)
  ADD_EXECUTABLE(imageconverter imageconverter.cpp)
  # imageconverter is a build-time tool; it must not use ASan because it runs
  # during the build without the ASan runtime preloaded.
  target_compile_options(imageconverter PRIVATE -fno-sanitize=address)
  target_link_options(imageconverter PRIVATE -fno-sanitize=address)
ENDIF(NOT CMAKE_CROSSCOMPILING)

IF(NOT CMAKE_CROSSCOMPILING) 
  EXPORT(TARGETS imageconverter FILE ${CMAKE_BINARY_DIR}/ImportExecutables.cmake )
ENDIF(NOT CMAKE_CROSSCOMPILING) 

