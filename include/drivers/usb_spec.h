#ifndef USB_SPEC_H
#define USB_SPEC_H

#include <stdint.h>

typedef struct {
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed)) usb_setup_packet_t;

#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_ADDRESS    0x05
#define USB_REQ_SET_CONFIG     0x09

#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIG        0x02
#define USB_DESC_INTERFACE     0x04
#define USB_DESC_ENDPOINT      0x05

typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint16_t usb_version;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t device_version;
    uint8_t  manufacturer_idx;
    uint8_t  product_idx;
    uint8_t  serial_idx;
    uint8_t  num_configs;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint16_t total_length;
    uint8_t  num_interfaces;
    uint8_t  config_value;
    uint8_t  config_idx;
    uint8_t  attributes;
    uint8_t  max_power;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint8_t  interface_num;
    uint8_t  alternate_setting;
    uint8_t  num_endpoints;
    uint8_t  interface_class;
    uint8_t  interface_subclass;
    uint8_t  interface_protocol;
    uint8_t  interface_idx;
} __attribute__((packed)) usb_interface_descriptor_t;

typedef struct {
    uint8_t  length;
    uint8_t  type;
    uint8_t  endpoint_address;
    uint8_t  attributes;
    uint16_t max_packet_size;
    uint8_t  interval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

#endif


