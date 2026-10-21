# Fetch a pinned upstream archive into a local payload directory.
#
# The payload directory carries a marker file naming the version it holds, so
# a version bump is detected without touching the network: a local copy whose
# marker matches the pin is left alone, a copy with a missing or different
# marker is replaced.
#
# fetch_pinned_archive(
#   DEST_DIR <dir>          # directory the payload is unpacked into
#   VERSION  <string>       # pinned version, recorded in the marker file
#   URL      <url>          # archive to download
#   ARCHIVE  <type>         # TAR_GZ | TAR_BZ2 | TAR_XZ | TAR | ZIP
#   [MARKER  <name>]        # marker file name inside DEST_DIR (.pinned-version)
#   [HASH    <sha256>]      # expected sha256 of the archive
#   [SUBPATH <rel>]         # payload path inside the archive (auto-detected if absent)
# )
#
# Auto-detection looks for the directory inside the archive that holds both a
# bin/ and a lib/ folder, which is the layout the pinned archives use.

function(fetch_pinned_archive)
	cmake_parse_arguments(FPA "" "DEST_DIR;VERSION;URL;ARCHIVE;MARKER;HASH;SUBPATH" "" ${ARGN})

	if(NOT FPA_DEST_DIR OR NOT FPA_VERSION OR NOT FPA_URL OR NOT FPA_ARCHIVE)
		message(FATAL_ERROR "fetch_pinned_archive: DEST_DIR, VERSION, URL and ARCHIVE are required")
	endif()
	if(NOT FPA_MARKER)
		set(FPA_MARKER ".pinned-version")
	endif()

	set(marker "${FPA_DEST_DIR}/${FPA_MARKER}")
	if(EXISTS "${marker}")
		file(READ "${marker}" current)
		string(STRIP "${current}" current)
		if(current STREQUAL FPA_VERSION)
			message(STATUS "fetch_pinned_archive: ${FPA_DEST_DIR} already holds ${FPA_VERSION}")
			return()
		endif()
		message(STATUS "fetch_pinned_archive: ${FPA_DEST_DIR} holds ${current}, pin is ${FPA_VERSION} - refetching")
	elseif(EXISTS "${FPA_DEST_DIR}")
		message(STATUS "fetch_pinned_archive: ${FPA_DEST_DIR} has no version marker - refetching to verify")
	else()
		message(STATUS "fetch_pinned_archive: fetching ${FPA_VERSION} from ${FPA_URL}")
	endif()

	set(work "${CMAKE_CURRENT_BINARY_DIR}/fetch_pinned_archive")
	set(archive "${work}/${FPA_ARCHIVE}")
	file(REMOVE_RECURSE "${work}")
	file(MAKE_DIRECTORY "${work}")

	if(FPA_HASH)
		file(DOWNLOAD ${FPA_URL} "${archive}" EXPECTED_HASH "SHA256=${FPA_HASH}"
			STATUS download_status SHOW_PROGRESS TIMEOUT 600)
	else()
		file(DOWNLOAD ${FPA_URL} "${archive}" STATUS download_status SHOW_PROGRESS TIMEOUT 600)
	endif()
	list(GET download_status 0 download_code)
	if(NOT download_code EQUAL 0)
		list(GET download_status 1 download_message)
		file(REMOVE_RECURSE "${work}")
		message(FATAL_ERROR "fetch_pinned_archive: download failed (${download_code}): ${download_message}")
	endif()

	set(extract "${work}/extract")
	file(MAKE_DIRECTORY "${extract}")
	file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${extract}")

	if(FPA_SUBPATH)
		set(payload "${extract}/${FPA_SUBPATH}")
	else()
		set(payload "")
		file(GLOB candidates RELATIVE "${extract}" "${extract}/*" "${extract}/*/*")
		foreach(candidate ${candidates})
			if(IS_DIRECTORY "${extract}/${candidate}" AND EXISTS "${extract}/${candidate}/bin" AND EXISTS "${extract}/${candidate}/lib")
				set(payload "${extract}/${candidate}")
				break()
			endif()
		endforeach()
		if(NOT payload)
			set(payload "${extract}")
		endif()
	endif()
	if(NOT IS_DIRECTORY "${payload}")
		file(REMOVE_RECURSE "${work}")
		message(FATAL_ERROR "fetch_pinned_archive: no payload directory in ${FPA_URL}")
	endif()

	file(REMOVE_RECURSE "${FPA_DEST_DIR}")
	file(MAKE_DIRECTORY "${FPA_DEST_DIR}")
	file(GLOB payload_children RELATIVE "${payload}" "${payload}/*")
	foreach(child ${payload_children})
		file(COPY "${payload}/${child}" DESTINATION "${FPA_DEST_DIR}")
	endforeach()

	file(WRITE "${marker}" "${FPA_VERSION}\n")
	file(REMOVE_RECURSE "${work}")
	message(STATUS "fetch_pinned_archive: ${FPA_DEST_DIR} now holds ${FPA_VERSION}")
endfunction()
