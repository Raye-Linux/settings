cmake_minimum_required(VERSION 3.22)
project(raye-settings VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 DBus Multimedia Network Test)
# put the QML module under qrc:/qt/qml, where the engine looks for it
qt_policy(SET QTP0001 NEW)
find_package(KF6 6.0 REQUIRED COMPONENTS
    CoreAddons I18n KCMUtils Kirigami Config DBusAddons KIO Solid Notifications
    WindowSystem NetworkManagerQt BluezQt)
find_package(KF6Screen REQUIRED)
find_package(KF6PulseAudioQt REQUIRED)
find_package(packagekitqt6 REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(LayerShellQt REQUIRED)
pkg_check_modules(Flatpak REQUIRED IMPORTED_TARGET flatpak)

# The helpers live where the polkit actions and the pages name them,
# /usr/libexec/raye-settings, whatever the multiarch layout says.
set(RAYE_LIBEXECDIR ${CMAKE_INSTALL_PREFIX}/libexec/raye-settings)

# Every page of Settings is a file dropped in a folder, and the build
# finds it: a page, a backend, a helper, an icon or a test never needs
# a line in this file.
file(GLOB_RECURSE RAYE_QML_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} src/qml/*.qml src/qml/*.js)
file(GLOB RAYE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM RAYE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
file(GLOB RAYE_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)
file(GLOB RAYE_BACKEND_SOURCES CONFIGURE_DEPENDS src/backends/*.cpp)
file(GLOB RAYE_SEARCH_SOURCES CONFIGURE_DEPENDS src/search/*.cpp)
file(GLOB RAYE_RUNNER_SOURCES CONFIGURE_DEPENDS src/runner/*.cpp)
file(GLOB RAYE_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
file(GLOB RAYE_TEST_SCRIPTS CONFIGURE_DEPENDS tests/test_*.sh)
list(SORT RAYE_QML_FILES)
list(SORT RAYE_TEST_SOURCES)
list(SORT RAYE_TEST_SCRIPTS)

# A page is named in data/layout.json the way it sits under src/qml
# ("pages/system/DisplayPage.qml"). Drop the src/qml prefix from the
# resource so the router can look the file up by that same name.
# A QML type is named after its file, wherever the file is: two files
# with the same name in two folders would be the same type, so stop
# here and say which ones.
set(RAYE_QML_TYPES)
foreach(file ${RAYE_QML_FILES})
    string(REPLACE "src/qml/" "" alias ${file})
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
    get_filename_component(type ${file} NAME_WE)
    get_filename_component(extension ${file} LAST_EXT)
    if(extension STREQUAL ".qml")
        if(type IN_LIST RAYE_QML_TYPES)
            message(FATAL_ERROR "Two QML files are named ${type}.qml; one of them needs another name (${file})")
        endif()
        list(APPEND RAYE_QML_TYPES ${type})
        # Theme and the like: "pragma Singleton" at the top is enough
        file(STRINGS ${file} singleton REGEX "^pragma[ \t]+Singleton" LIMIT_COUNT 1)
        if(singleton)
            set_source_files_properties(${file} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        endif()
    endif()
endforeach()

# Everything Settings is made of, its QML module included. The window,
# the tests and the small programs below all link this one library, so
# a C++ class marked QML_ELEMENT works in pages and in tests alike.
add_library(raye-settings-core STATIC
    ${RAYE_SOURCES} ${RAYE_UI_SOURCES} ${RAYE_BACKEND_SOURCES} ${RAYE_SEARCH_SOURCES})
qt_add_qml_module(raye-settings-core
    URI org.raye.settings
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/org/raye/settings
    PLUGIN_TARGET raye-settings-qmlplugin
    CLASS_NAME RayeSettingsPlugin
    QML_FILES ${RAYE_QML_FILES}
)
target_include_directories(raye-settings-core PUBLIC src)
# a backend starts its helper as RAYE_LIBEXECDIR "/storage-clean"
target_compile_definitions(raye-settings-core PUBLIC RAYE_LIBEXECDIR="${RAYE_LIBEXECDIR}")
foreach(folder ui backends search)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${folder})
        target_include_directories(raye-settings-core PUBLIC src/${folder})
    endif()
endforeach()
target_link_libraries(raye-settings-core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network
    KF6::ConfigCore KF6::ConfigGui KF6::CoreAddons KF6::I18n KF6::KIOCore KF6::Solid
    KF6::Notifications KF6::WindowSystem KF6::NetworkManagerQt KF6::BluezQt KF6::DBusAddons
    KF6::KCMUtilsQuick KF6::Screen KF6::PulseAudioQt PK::packagekitqt6 PkgConfig::Flatpak crypt)

# The module is a static plugin: whoever wants the pages imports it.
set(RAYE_QML_IMPORT ${CMAKE_CURRENT_BINARY_DIR}/qmlimport.cpp)
file(WRITE ${RAYE_QML_IMPORT}.in
    "// written by CMakeLists.txt\n#include <QtQml/qqmlextensionplugin.h>\nQ_IMPORT_QML_PLUGIN(RayeSettingsPlugin)\n")
configure_file(${RAYE_QML_IMPORT}.in ${RAYE_QML_IMPORT} COPYONLY)

# The version shown in About is the one of the package when there is
# one, the project's otherwise.
set(RAYE_SETTINGS_VERSION ${PROJECT_VERSION})
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS debian/changelog)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog changelog LIMIT_COUNT 1)
    if(changelog MATCHES "\\(([^)]+)\\)")
        set(RAYE_SETTINGS_VERSION ${CMAKE_MATCH_1})
    endif()
endif()

add_executable(raye-settings src/main.cpp ${RAYE_QML_IMPORT})
target_link_libraries(raye-settings PRIVATE raye-settings-core raye-settings-qmlplugin)
target_compile_definitions(raye-settings PRIVATE
    RAYE_SETTINGS_VERSION="${RAYE_SETTINGS_VERSION}"
    RAYE_SETTINGS_LAYOUT_FILE="${KDE_INSTALL_FULL_DATADIR}/raye-settings/layout.json")
install(TARGETS raye-settings ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# The Start menu asks Settings for results; the runner answers on D-Bus.
# It stays up while the user types, so it gets the search and what the
# search reads (layout, hardware probes) and nothing of the pages.
if(RAYE_RUNNER_SOURCES)
    set(runner_search ${RAYE_SEARCH_SOURCES})
    list(REMOVE_ITEM runner_search ${CMAKE_CURRENT_SOURCE_DIR}/src/search/searchmodel.cpp)
    add_executable(raye-settings-runner ${RAYE_RUNNER_SOURCES} ${runner_search} src/layout.cpp src/router.cpp)
    target_include_directories(raye-settings-runner PRIVATE src src/search)
    target_link_libraries(raye-settings-runner PRIVATE Qt6::Core Qt6::DBus Qt6::Qml KF6::CoreAddons KF6::I18n)
    install(TARGETS raye-settings-runner DESTINATION ${RAYE_LIBEXECDIR})
endif()

# Small programs of their own, started by Settings or by the system:
# src/<folder>/ makes the program, with a QML module of its own when
# the folder holds QML files.
foreach(extra recovery:raye-recovery fontpreview:raye-fontpreview btagent:raye-bluetooth-agent)
    string(REPLACE ":" ";" parts ${extra})
    list(GET parts 0 folder)
    list(GET parts 1 program)
    file(GLOB sources CONFIGURE_DEPENDS src/${folder}/*.cpp)
    if(NOT sources)
        continue()
    endif()
    add_executable(${program} ${sources})
    file(GLOB_RECURSE qml CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} src/${folder}/*.qml)
    if(qml)
        foreach(file ${qml})
            string(REPLACE "src/${folder}/" "" alias ${file})
            set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${alias})
        endforeach()
        qt_add_qml_module(${program}
            URI org.raye.${folder}
            VERSION 1.0
            QML_FILES ${qml})
    endif()
    target_link_libraries(${program} PRIVATE raye-settings-core)
    install(TARGETS ${program} ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
endforeach()

install(FILES data/layout.json DESTINATION ${KDE_INSTALL_DATADIR}/raye-settings)
file(GLOB RAYE_DESKTOP_FILES CONFIGURE_DEPENDS data/*.desktop)
install(FILES ${RAYE_DESKTOP_FILES} DESTINATION ${KDE_INSTALL_APPDIR})
# Meta+I: the global shortcuts daemon reads the default keys of an app
# from a link to its entry, as it did for KDE System Settings
install(CODE "
    set(dir \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATADIR}/kglobalaccel\")
    file(MAKE_DIRECTORY \"\${dir}\")
    file(CREATE_LINK ../applications/org.raye.settings.desktop \"\${dir}/org.raye.settings.desktop\" SYMBOLIC)
    message(STATUS \"Installing: \${dir}/org.raye.settings.desktop\")
")
# take the place of KDE's commands, which the package moves aside
install(PROGRAMS data/systemsettings data/kcmshell6 DESTINATION ${KDE_INSTALL_BINDIR})

# What each page tells the search about itself.
file(GLOB RAYE_SEARCH_DATA CONFIGURE_DEPENDS data/search/*.json)
if(RAYE_SEARCH_DATA)
    install(FILES ${RAYE_SEARCH_DATA} DESTINATION ${KDE_INSTALL_DATADIR}/raye-settings/search)
endif()

# What a page may ask for with the password of an administrator.
file(GLOB RAYE_POLKIT_ACTIONS CONFIGURE_DEPENDS data/polkit/*.policy)
if(RAYE_POLKIT_ACTIONS)
    install(FILES ${RAYE_POLKIT_ACTIONS} DESTINATION ${KDE_INSTALL_DATADIR}/polkit-1/actions)
endif()

# The programs those actions run, and the session of the recovery
# screen.
file(GLOB RAYE_HELPERS CONFIGURE_DEPENDS data/libexec/* data/recovery/*)
if(RAYE_HELPERS)
    install(PROGRAMS ${RAYE_HELPERS} DESTINATION ${RAYE_LIBEXECDIR})
endif()

# Raye's own glyphs, laid out as an icon theme (hicolor/scalable/apps).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data/icons)
    install(DIRECTORY data/icons/ DESTINATION ${KDE_INSTALL_ICONDIR})
endif()

# Account pictures to choose from.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/data/icons-avatars)
    install(DIRECTORY data/icons-avatars/ DESTINATION ${KDE_INSTALL_DATADIR}/raye-settings/avatars)
endif()

# SDDM's automatic sign-in defaults, read first: a desktop switch's one
# automatic sign-in really ends when its file goes (desktop-session).
install(FILES data/sddm/00-raye-autologin-defaults.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/sddm/sddm.conf.d)

# Removed at boot: a desktop switch's one automatic sign-in (desktop-session).
file(GLOB RAYE_TMPFILES CONFIGURE_DEPENDS data/tmpfiles/*.conf)
if(RAYE_TMPFILES)
    install(FILES ${RAYE_TMPFILES} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d)
endif()

# Started after signing in (the offer of Files from Windows, the apps
# of the other desktop after a switch).
file(GLOB RAYE_AUTOSTART CONFIGURE_DEPENDS data/autostart/*.desktop)
if(RAYE_AUTOSTART)
    install(FILES ${RAYE_AUTOSTART} DESTINATION ${KDE_INSTALL_AUTOSTARTDIR})
endif()

file(GLOB RAYE_USER_UNITS CONFIGURE_DEPENDS data/systemd/user/*)
if(RAYE_USER_UNITS)
    install(FILES ${RAYE_USER_UNITS} DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()

# Device Portal, in System > Advanced
add_subdirectory(portal)

file(GLOB RAYE_RUNNER_PLUGIN CONFIGURE_DEPENDS data/krunner/*.desktop)
if(RAYE_RUNNER_PLUGIN)
    install(FILES ${RAYE_RUNNER_PLUGIN} DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins)
endif()
file(GLOB RAYE_RUNNER_SERVICE CONFIGURE_DEPENDS data/krunner/*.service)
if(RAYE_RUNNER_SERVICE)
    install(FILES ${RAYE_RUNNER_SERVICE} DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
endif()

# po/<language>/raye-settings.po, one folder per language; until the
# first one lands there is nothing to install (and no empty folder)
file(GLOB RAYE_CATALOGUES CONFIGURE_DEPENDS po/*/*.po)
if(RAYE_CATALOGUES)
    ki18n_install(po)
endif()

if(BUILD_TESTING)
    enable_testing()
    # The programs below come from a glob, so a renamed or missing
    # tests/ folder does not break the build: it registers the two tests
    # that do not come from the glob (appstreamtest, qml), ctest says
    # "2 tests passed" and the other suites are gone without a word.
    # debian/rules' CTEST_NO_TESTS_ACTION=error cannot see it -- two is
    # not zero. Measured 2026-11-06 in the resolute chroot: tests/ moved
    # aside, 43 registered tests became 2. So say it here, where the
    # build still stops.
    if(NOT RAYE_TEST_SOURCES)
        message(FATAL_ERROR "BUILD_TESTING is on and tests/test_*.cpp matched nothing: "
                            "the tests folder is missing or renamed. Build with "
                            "-DBUILD_TESTING=OFF to build without tests.")
    endif()
    # Each test may load pages too: it gets the QML module, and is told
    # where the sources and the layout are.
    foreach(source ${RAYE_TEST_SOURCES})
        get_filename_component(name ${source} NAME_WE)
        string(REPLACE "test_" "" name ${name})
        add_executable(test-${name} ${source} ${RAYE_QML_IMPORT})
        target_link_libraries(test-${name} PRIVATE raye-settings-core raye-settings-qmlplugin Qt6::Test)
        target_compile_definitions(test-${name} PRIVATE
            LAYOUT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/layout.json"
            SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME ${name} COMMAND test-${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")
    endforeach()
    # Identify checks the layer surface of each number window
    target_link_libraries(test-display_identify PRIVATE LayerShellQt::Interface)
    # the switch transition covers each screen with an overlay layer surface
    target_link_libraries(test-desktopenvironment PRIVATE LayerShellQt::Interface)
    # helpers are tried as they ship: sh tests/test_x.sh, with the
    # source and build folders in the environment
    foreach(script ${RAYE_TEST_SCRIPTS})
        get_filename_component(name ${script} NAME_WE)
        string(REPLACE "test_" "" name ${name})
        add_test(NAME ${name} COMMAND sh ${script})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT
            "SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR};BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR};LANGUAGE=en")
    endforeach()
    # the interface itself: every QML file of the module must load
    add_test(NAME qml COMMAND raye-settings --check-qml)
    set_tests_properties(qml PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LANGUAGE=en")
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
