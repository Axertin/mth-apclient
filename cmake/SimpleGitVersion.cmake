# SimpleGitVersion.cmake - GitHub Flow friendly versioning

function(add_git_version_info target)
  find_package(Git QUIET)

  if(NOT GIT_FOUND)
    set(VERSION_STRING "0.0.0-unknown")
    set(VERSION_HASH "unknown")
    set(VERSION_BRANCH "unknown")
    set(VERSION_MAJOR 0)
    set(VERSION_MINOR 0)
    set(VERSION_PATCH 0)
  else()
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_DESCRIBE
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_BRANCH
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )

    set(VERSION_MAJOR 0)
    set(VERSION_MINOR 0)
    set(VERSION_PATCH 0)

    if(GIT_DESCRIBE MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([a-zA-Z][a-zA-Z0-9.-]*))?")
      set(VERSION_MAJOR ${CMAKE_MATCH_1})
      set(VERSION_MINOR ${CMAKE_MATCH_2})
      set(VERSION_PATCH ${CMAKE_MATCH_3})
      set(VERSION_PRERELEASE ${CMAKE_MATCH_5})

      set(BASE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
      if(VERSION_PRERELEASE)
        set(BASE_VERSION "${BASE_VERSION}-${VERSION_PRERELEASE}")
      endif()

      if(GIT_DESCRIBE MATCHES "^v?[0-9]+\\.[0-9]+\\.[0-9]+(-[a-zA-Z][a-zA-Z0-9.-]*)?$")
        set(VERSION_STRING "${BASE_VERSION}")
      else()
        if(GIT_DESCRIBE MATCHES "^v?[0-9]+\\.[0-9]+\\.[0-9]+(-[a-zA-Z][a-zA-Z0-9.-]*)?-([0-9]+)-g[a-f0-9]+")
          set(COMMITS_SINCE_TAG ${CMAKE_MATCH_2})
          if(VERSION_PRERELEASE)
            set(VERSION_STRING "${BASE_VERSION}.${COMMITS_SINCE_TAG}")
          else()
            set(VERSION_STRING "${BASE_VERSION}-dev.${COMMITS_SINCE_TAG}")
          endif()
        else()
          if(VERSION_PRERELEASE)
            set(VERSION_STRING "${BASE_VERSION}")
          else()
            set(VERSION_STRING "${BASE_VERSION}-dev")
          endif()
        endif()

        if(GIT_DESCRIBE MATCHES "dirty$")
          set(VERSION_STRING "${VERSION_STRING}-dirty")
        endif()
      endif()
    else()
      execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE COMMIT_COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
      )
      if(NOT COMMIT_COUNT)
        set(COMMIT_COUNT 0)
      endif()
      set(VERSION_STRING "0.0.${COMMIT_COUNT}-dev")
      set(VERSION_PATCH ${COMMIT_COUNT})
    endif()

    set(VERSION_HASH ${GIT_HASH})
    set(VERSION_BRANCH ${GIT_BRANCH})
  endif()

  set(VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
  set(VERSION_HEADER "${VERSION_HEADER_DIR}/mth_version.h")

  file(MAKE_DIRECTORY ${VERSION_HEADER_DIR})
  file(WRITE ${VERSION_HEADER}
    "#pragma once
#include <string_view>
namespace mth::version {
    inline constexpr std::string_view string{\"${VERSION_STRING}\"};
    inline constexpr int major{${VERSION_MAJOR}};
    inline constexpr int minor{${VERSION_MINOR}};
    inline constexpr int patch{${VERSION_PATCH}};
    inline constexpr std::string_view hash{\"${VERSION_HASH}\"};
    inline constexpr std::string_view branch{\"${VERSION_BRANCH}\"};
}
")

  target_include_directories(${target} PUBLIC ${VERSION_HEADER_DIR})
  file(WRITE ${CMAKE_BINARY_DIR}/version.txt "${VERSION_STRING}")
  message(STATUS "mth-apclient version: ${VERSION_STRING} (${VERSION_HASH})")
endfunction()
