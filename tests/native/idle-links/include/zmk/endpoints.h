/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
enum zmk_transport { ZMK_TRANSPORT_NONE, ZMK_TRANSPORT_USB, ZMK_TRANSPORT_BLE };
struct zmk_endpoint_instance {
    enum zmk_transport transport;
    struct { int profile_index; } ble;
};
struct zmk_endpoint_instance zmk_endpoint_get_selected(void);
