# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)
if(ZBLP_SPLIT_PREEMPT_TEST)
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/preempt.conf)
endif()
if(ZBLP_SPLIT_ACTIVITY_TEST)
  if(ZBLP_SPLIT_PREEMPT_TEST OR NOT ZBLP_ZMK_ROOT)
    message(FATAL_ERROR "Activity fixture requires the selected ZMK root and its own kernel variant")
  endif()
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/activity.conf)
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zblp_split_links)
if(NOT ZBLP_SPLIT_POWER_SOURCE)
  set(ZBLP_SPLIT_POWER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/split_service.c)
endif()
get_filename_component(ZBLP_SPLIT_POWER_DIRECTORY ${ZBLP_SPLIT_POWER_SOURCE} DIRECTORY)
target_include_directories(app PRIVATE include ${ZBLP_SPLIT_POWER_DIRECTORY})
if(ZBLP_SPLIT_ACTIVITY_TEST)
  set(activity_core_sources
    ${ZBLP_ZMK_ROOT}/app/src/activity.c
    ${ZBLP_ZMK_ROOT}/app/src/event_manager.c
    ${ZBLP_ZMK_ROOT}/app/src/events/activity_state_changed.c
    ${ZBLP_ZMK_ROOT}/app/src/events/position_state_changed.c
    ${ZBLP_ZMK_ROOT}/app/src/events/sensor_event.c)
  target_include_directories(app BEFORE PRIVATE ${ZBLP_ZMK_ROOT}/app/include)
  target_sources(app PRIVATE activity.c ${activity_core_sources})
  target_compile_definitions(app PRIVATE ZBLP_SPLIT_ACTIVITY_TEST=1
    CONFIG_ZMK_LOG_LEVEL=0 CONFIG_ZMK_IDLE_TIMEOUT=60000 CONFIG_ZMK_POINTING=1)
  zephyr_linker_sources(RODATA ${ZBLP_ZMK_ROOT}/app/include/linker/zmk-events.ld)
else()
  target_sources(app PRIVATE main.c)
endif()
target_compile_definitions(app PRIVATE
  ZBLP_SPLIT_POWER_SOURCE="${ZBLP_SPLIT_POWER_SOURCE}"
  CONFIG_ZMK_BLE_LINK_POLICY_LOG_LEVEL=3
  CONFIG_BT_SMP=1 CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=1
  CONFIG_ZMK_SPLIT_BLE_PREF_INT=6 CONFIG_ZMK_SPLIT_BLE_PREF_LATENCY=30
  CONFIG_ZMK_SPLIT_BLE_PREF_TIMEOUT=400 CONFIG_ZMK_BLE_SPLIT_POLICY_IDLE_INTERVAL=24)
if(ZBLP_SPLIT_PREEMPT_TEST)
  target_compile_definitions(app PRIVATE ZBLP_SPLIT_PREEMPT_TEST=1)
endif()
# Apply the fixture's extra warnings to owned sources. The unchanged core keeps
# its production warning policy; its existing sign/unused-parameter warnings
# are not keyboard regressions or runtime control failures.
target_compile_options(app PRIVATE -Wall -Werror)
set_source_files_properties(main.c activity.c PROPERTIES COMPILE_OPTIONS -Wextra)

if(ZBLP_SPLIT_CORE_DYA)
  target_compile_definitions(app PRIVATE ZBLP_SPLIT_CORE_DYA=1)
endif()
