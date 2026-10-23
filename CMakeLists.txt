cmake_minimum_required(VERSION 3.21)
project(samba-manager-ng VERSION 4.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CheckPIESupported)
check_pie_supported()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(SM_BUILD_TESTS "Compilar os testes" ON)
option(SM_BUILD_UI "Compilar a interface" ON)
option(SM_SANITIZE "AddressSanitizer + UBSan (debug)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
if(SM_BUILD_UI)
  find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Concurrent)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -fstack-protector-strong)
add_compile_definitions(SM_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)
if(SM_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- core: só QtCore (é o que roda como root no auxiliar) ----
add_library(sm_core STATIC
  src/core/config.cpp
  src/core/sysutils.cpp
  src/core/textdiff.cpp
  src/core/smbconf.cpp
  src/core/backups.cpp
  src/core/selinux.cpp
  src/core/ops.cpp
  src/core/protocol.cpp
)
target_include_directories(sm_core PUBLIC src)
target_link_libraries(sm_core PUBLIC Qt6::Core)

include(GNUInstallDirs)

# ---- auxiliar root (aberto pela interface via pkexec) ----
add_executable(samba-manager-ng-helper src/helper/main.cpp)
target_link_libraries(samba-manager-ng-helper PRIVATE sm_core)
target_link_options(samba-manager-ng-helper PRIVATE -Wl,-z,relro,-z,now)

# ---- interface ----
if(SM_BUILD_UI)
add_library(sm_ui STATIC
  src/ui/theme.cpp
  src/ui/widgets.cpp
  src/ui/dialogs.cpp
  src/ui/mainwindow.cpp
  src/ui/pages/servicepage.cpp
  src/ui/pages/sharespage.cpp
  src/ui/pages/userspage.cpp
  src/ui/pages/globalpage.cpp
  src/ui/pages/statuspage.cpp
  src/ui/pages/backupspage.cpp
)
target_link_libraries(sm_ui PUBLIC sm_core Qt6::Widgets Qt6::Concurrent)

add_executable(samba-manager-ng src/ui/main.cpp)
target_link_libraries(samba-manager-ng PRIVATE sm_ui)
qt_add_resources(samba-manager-ng icons PREFIX "/" BASE packaging/icons
  FILES packaging/icons/samba-manager-ng.png)
install(TARGETS samba-manager-ng RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS samba-manager-ng-helper RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/samba-manager-ng)

if(SM_BUILD_TESTS)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(test_core tests/test_core.cpp)
  target_link_libraries(test_core PRIVATE sm_core Qt6::Test)
  add_test(NAME core COMMAND test_core)

  if(SM_BUILD_UI)
    add_executable(test_ui tests/test_ui.cpp)
    target_link_libraries(test_ui PRIVATE sm_ui Qt6::Test)
    add_test(NAME ui COMMAND test_ui)
    set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
