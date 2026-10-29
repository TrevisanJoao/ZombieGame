# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/dev/project/ZombieGame/build/_deps/stb_image-src")
  file(MAKE_DIRECTORY "C:/dev/project/ZombieGame/build/_deps/stb_image-src")
endif()
file(MAKE_DIRECTORY
  "C:/dev/project/ZombieGame/build/_deps/stb_image-build"
  "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix"
  "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix/tmp"
  "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix/src/stb_image-populate-stamp"
  "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix/src"
  "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix/src/stb_image-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix/src/stb_image-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/dev/project/ZombieGame/build/_deps/stb_image-subbuild/stb_image-populate-prefix/src/stb_image-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
