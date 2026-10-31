# SPDX-License-Identifier: GPL-3.0-only
if(CONFIG_ZMK_BLE_LINK_POLICY)
  target_sources(app PRIVATE src/link_service.c src/link_policy.c)
endif()
