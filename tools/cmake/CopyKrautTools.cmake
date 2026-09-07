# CopyKrautTools.cmake — copies the KrautCLI (+ KrautPreview) binaries built
# by the kraut_tools ExternalProject next to the fury executables (the
# FBX2glTF distribution pattern). Invoked POST_BUILD from fury_add_target.
#
# Expected -D vars:
#   KRAUT_OUTPUT_DIR  Kraut's Output/Bin directory (binaries land in a
#                     per-platform/config subdirectory below it)
#   DEST_DIR          $<TARGET_FILE_DIR:fury>
#   WITH_PREVIEW      ON/OFF
#
# The newest binary per name wins when several config subdirs exist
# (multi-config generators build more than one over time).

foreach(TOOL_NAME KrautCLI KrautPreview)
	if(TOOL_NAME STREQUAL "KrautPreview" AND NOT WITH_PREVIEW)
		continue()
	endif()

	set(_best "")
	set(_best_mtime "")
	file(GLOB_RECURSE _candidates
		"${KRAUT_OUTPUT_DIR}/${TOOL_NAME}"
		"${KRAUT_OUTPUT_DIR}/${TOOL_NAME}.exe")
	list(REMOVE_DUPLICATES _candidates)
	foreach(_cand IN LISTS _candidates)
		if(IS_DIRECTORY "${_cand}")
			continue()
		endif()
		# skip shared-library siblings (e.g. libKrautCLI.so would match the glob)
		get_filename_component(_ext "${_cand}" EXT)
		if(NOT _ext STREQUAL "" AND NOT _ext STREQUAL ".exe")
			continue()
		endif()
		file(TIMESTAMP "${_cand}" _mtime "%s" UTC)
		if(_best STREQUAL "" OR _mtime STRGREATER _best_mtime)
			set(_best "${_cand}")
			set(_best_mtime "${_mtime}")
		endif()
	endforeach()

	if(_best STREQUAL "")
		message(WARNING "CopyKrautTools: ${TOOL_NAME} not found under ${KRAUT_OUTPUT_DIR} (kraut_tools not built yet?)")
	else()
		get_filename_component(_name "${_best}" NAME)
		file(COPY "${_best}" DESTINATION "${DEST_DIR}" FILE_PERMISSIONS
			OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
		message(STATUS "CopyKrautTools: ${_name} -> ${DEST_DIR}")
	endif()
endforeach()
