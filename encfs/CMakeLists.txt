include_directories (${Encfs_SOURCE_DIR}/base)
link_directories (${Encfs_BINARY_DIR}/base)

include_directories (${Encfs_SOURCE_DIR}/cipher)
link_directories (${Encfs_BINARY_DIR}/cipher)

include_directories (${Encfs_SOURCE_DIR}/fs)
link_directories (${Encfs_BINARY_DIR}/fs)

include_directories (${CMAKE_BINARY_DIR}/base)

add_library (encfs-app
    EncfsPasswordReader.cpp
    PosixFsIO.cpp
    ProgramPasswordReader.cpp
    PromptPasswordReader.cpp
    RawFileIO.cpp
    StdinPasswordReader.cpp
)

if (FUSE_FOUND)
# only the encfs console application requires fuse

add_definitions (-DFUSE_USE_VERSION=26)
add_definitions (-D_FILE_OFFSET_BITS=64)

include_directories (${FUSE_INCLUDE_DIR})

add_executable (encfs
    EncFSFuseContext.cpp
    encfs.cpp
    main.cpp
)

target_link_libraries (encfs
    encfs-app
    encfs-fs
    encfs-cipher
    encfs-base
    ${FUSE_LIBRARIES}
)

if (POD2MAN)
    add_custom_target(man ALL
        COMMAND ${POD2MAN} -u --section=1 --release=${ENCFS_VERSION}
        --center="Encrypted Filesystem"
        ${CMAKE_CURRENT_SOURCE_DIR}/encfs.pod
        encfs.1)
    install (FILES ${CMAKE_CURRENT_BINARY_DIR}/encfs.1
        DESTINATION
        share/man/man1)
endif (POD2MAN)

install (TARGETS encfs DESTINATION bin)

endif (FUSE_FOUND)

