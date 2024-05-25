find_path(LIBCLANG_INCLUDE_DIR clang-c
  HINTS
    ENV LIBCLANG_DIR
  PATH_SUFFIXES include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
  )

find_library(LIBCLANG_LIBRARIES
  NAMES libclang clang
  HINTS
    ENV LIBCLANG_DIR
  PATH_SUFFIXES lib
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /sw
  /opt/local
  /opt/csw
  /opt
  )

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBCLANG_FOUND to TRUE if
# all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(
  libclang
  REQUIRED_VARS LIBCLANG_LIBRARIES LIBCLANG_INCLUDE_DIR
  )

mark_as_advanced(LIBCLANG_INCLUDE_DIR LIBCLANG_LIBRARIES)
