include_directories (${PROJECT_BINARY_DIR}/base)
add_library (encfs-apputil
    EncfsPasswordReader.cpp
    PosixFsIO.cpp
    ProgramPasswordReader.cpp
    PromptPasswordReader.cpp
    RawFileIO.cpp
    StdinPasswordReader.cpp
)
