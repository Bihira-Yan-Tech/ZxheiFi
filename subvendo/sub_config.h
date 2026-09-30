/*
 * sub_config.h - Sub Vendo constants (shared box constants: common/box/box_config.h)
 * ZXHEIFI Sub Vendo firmware (v2)
 */
#ifndef SUB_CONFIG_H
#define SUB_CONFIG_H

#define SUB_FIRMWARE_VERSION   "2.0.0-dev"
#define SUB_SETUP_AP_SSID      "ZxheiFi-Sub-Setup"
#define SUB_QUEUE_FILE         "/queue.json"
#define SUB_QUEUE_CAPACITY     20     // same as ZX_COIN_QUEUE_MAX
#define SUB_QUEUE_HEADROOM     2      // relay off this many coins before the queue is full

#endif // SUB_CONFIG_H
