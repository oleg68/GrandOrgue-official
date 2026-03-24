# Copyright 2006 Milan Digital Audio LLC
# Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
# License GPL-2.0 or later
# (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).

function(BUILD_EXECUTABLE TARGET)
  if(NOT APPLE)
    target_link_options(${TARGET} PRIVATE "LINKER:--as-needed")
  endif()
  file(MAKE_DIRECTORY ${BINDIR})
  if (OBJECT_FIXUP_REQUIRED STREQUAL "ON")
    ADD_CUSTOM_COMMAND(TARGET ${TARGET} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_NAME:${TARGET}>" "${BINDIR}/$<TARGET_FILE_NAME:${TARGET}>")
  else()
    set_target_properties(${TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BINDIR}")
  endif()
  install(TARGETS ${TARGET} RUNTIME DESTINATION "${BININSTDIR}")

  if(UNIX AND NOT APPLE AND GO_SPLIT_DEBUG_SYMBOLS)
    install(CODE "
      # \$ENV{DESTDIR} is empty for TGZ (CMAKE_INSTALL_PREFIX is the staging dir),
      # but set to the staging root for RPM (CMAKE_INSTALL_PREFIX stays /usr).
      set(_bin \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${BININSTDIR}/${TARGET}\")
      # Extract debug info into a separate .dbg file alongside the binary.
      execute_process(COMMAND ${CMAKE_OBJCOPY} --only-keep-debug \"\${_bin}\" \"\${_bin}.dbg\")
      # objcopy needs to read the .dbg file to compute its CRC for the debuglink section.
      # It looks for the file by bare name in WORKING_DIRECTORY, so we set that to the
      # directory containing both the binary and the .dbg file.
      get_filename_component(_dir \"\${_bin}\" DIRECTORY)
      execute_process(COMMAND ${CMAKE_OBJCOPY} --strip-debug
        --add-gnu-debuglink=${TARGET}.dbg \"\${_bin}\"
        WORKING_DIRECTORY \"\${_dir}\")
    ")
  endif()

  if(CV2PDB_EXE AND GO_SPLIT_DEBUG_SYMBOLS)
    add_custom_command(
      OUTPUT "${BINDIR}/${TARGET}.pdb"
      DEPENDS ${TARGET}
      COMMAND
	${CMAKE_COMMAND}
	  -E env "WINEPATH=Z:${VC_PATH}"
	  wine "${CV2PDB_EXE}"
	  "$<TARGET_FILE:${TARGET}>"
	  "${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_PREFIX:${TARGET}>$<TARGET_FILE_BASE_NAME:${TARGET}>-tmp$<TARGET_FILE_SUFFIX:${TARGET}>"
	  "${BINDIR}/${TARGET}.pdb"
    )
    add_custom_target(${TARGET}.pdb ALL DEPENDS "${BINDIR}/${TARGET}.pdb")
    install(FILES "${BINDIR}/${TARGET}.pdb" DESTINATION "${BININSTDIR}")
  endif()
endfunction()
