cmake_minimum_required(VERSION 3.20)
project(clevo-fan-control VERSION 1.0.0 LANGUAGES CXX)

# Устанавливаем стандарт C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Опции сборки
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_DEBUG "Enable debug output" OFF)
option(INSTALL_SYSTEM_WIDE "Install system-wide (requires sudo)" ON)

# Список исходных файлов
set(SOURCES
    src/main.cpp
)

# Добавляем исполняемый файл
add_executable(clevo-fan-control ${SOURCES})

# Подключаем библиотеки
target_link_libraries(clevo-fan-control
    m
    dl
    pthread
)

# Определения препроцессора
target_compile_definitions(clevo-fan-control PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    VERSION=\"${PROJECT_VERSION}\"
)

# Опции для отладки
if(ENABLE_DEBUG)
    target_compile_options(clevo-fan-control PRIVATE -g -O0)
else()
    target_compile_options(clevo-fan-control PRIVATE -O2)
endif()

# Установка
if(INSTALL_SYSTEM_WIDE)
    install(TARGETS clevo-fan-control
        RUNTIME DESTINATION /usr/local/bin
    )
    
    # Установка systemd service файла
    configure_file(
        "${CMAKE_SOURCE_DIR}/clevo-fan-control.service.in"
        "${CMAKE_BINARY_DIR}/clevo-fan-control.service"
        @ONLY
    )
    
    install(FILES
        "${CMAKE_BINARY_DIR}/clevo-fan-control.service"
        DESTINATION /etc/systemd/system
    )
endif()

# Информационное сообщение
message(STATUS "========================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build directory: ${CMAKE_BINARY_DIR}")
message(STATUS "Debug mode: ${ENABLE_DEBUG}")
message(STATUS "")
message(STATUS "Build commands:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make -j\$(nproc)")
message(STATUS "  sudo make install")
message(STATUS "========================================")