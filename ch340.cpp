#include "ch340.h"
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <iterator>

int CH340::init(int vendor, int product, int baudrate) {
    int err;
    
    err = init_usb(vendor, product);
    if (err != 0) {
        printf("error: %d \n", err);
        return err;
    }
    printf("usb interface initialized \n");

    err = handshake();
    if (err != 0) {
        printf("error: %d \n", err);
        return err;
    }
    printf("ch340g initialized \n");

    err = set_baud(baudrate);
    if (err != 0) {
        printf("error: %d \n", err);
        return err;
    }

    err = up();
    if (err != 0) {
        printf("error: %d \n", err);
        return err;
    }
    printf("ch340g up \n");

    return err;
}

int CH340::init_usb(int vendor, int product) {
    int err;

    err = libusb_init(&ctx);
    if (err != 0) {
        return err;
    }

    libusb_device **dev_list = NULL;
    ssize_t num_devices = libusb_get_device_list(ctx, &dev_list);
    if (num_devices < 0) {
        libusb_exit(ctx);
        return -1;
    }

    bool deviceFound = false;
    for (ssize_t i = 0; i < num_devices; i++) {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev_list[i], &desc) == 0) {
            if (DEBUG) {
                std::cout << "Product ID: " << desc.idProduct << std::endl;
                std::cout << "Vendor: " << desc.idVendor << std::endl;
                std::cout << "Manufacturer: " << desc.iManufacturer << std::endl;
                std::cout << "serial Num: " << desc.iSerialNumber << std::endl;
                std::cout << "Product: " << desc.iProduct << std::endl;
            }
            if (desc.idVendor == vendor && desc.idProduct == product) {
                dev_handle = libusb_open_device_with_vid_pid(ctx, desc.idVendor, desc.idProduct);
                if (dev_handle) {
                    libusb_config_descriptor *config;
                    libusb_get_active_config_descriptor(dev_list[i], &config);

                    int interfaceNum = config->interface[0].altsetting[0].bInterfaceNumber;

                    if (libusb_kernel_driver_active(dev_handle, interfaceNum)) {
                        err = libusb_detach_kernel_driver(dev_handle, interfaceNum);
                        if (err != 0) {
                            std::cout << "Error detaching kernel driver: " << libusb_error_name(err) << std::endl;
                            libusb_close(dev_handle);
                            libusb_exit(ctx);
                            return err;
                        }
                    }
                    err = libusb_claim_interface(dev_handle, interfaceNum);
                    
                    libusb_free_config_descriptor(config);
                    
                    if (err == 0) {
                        deviceFound = true;
                        break;
                    } else {
                        std::cout << "Error claiming interface: " << libusb_error_name(err) << std::endl;
                        libusb_close(dev_handle);
                        dev_handle = NULL;
                        return err;
                    }
                }
                break;
            }
        }

    }

    if (dev_handle == NULL) {
        printf("could not find MCU \n");
        return -1;
    }

    return err;
}

int CH340::handshake() {
  if (dev_handle == NULL) {
    printf("dev_handle not defined \n");
    return -1;
  }

  int err = 0;

  // 1
  err = libusb_control_transfer(dev_handle, CTRL_OUT, 0xa1, 0, 0, NULL, 0, 1000);
  if (err < 0)
  {
    printf("setp 1 failed \n");
    return err;
  }
  // 2
  err = libusb_control_transfer(dev_handle, CTRL_OUT, 0x9a, 0x2518, 0x0050, NULL, 0, 1000);
  if (err < 0)
  {
    printf("step 2 failed \n");
    return err;
  }
  // 3
  err = libusb_control_transfer(dev_handle, CTRL_OUT, 0xa1, 0x501f, 0xd90a, NULL, 0, 1000);
  if (err < 0)
  {
    printf("step 3 failed \n");
    return err;
  }

  return 0;
}

int CH340::set_baud(int baudrate) {
    // List of available baudrates
    // baudrate, msg1, msg2
    static int baud[] = {
        2400,   0xd901, 0x0038,
        4800,   0x6402, 0x001f,
        9600,   0xb202, 0x0013,
        19200,  0xd902, 0x000d,
        38400,  0x6403, 0x000a,
        115200, 0xcc03, 0x0008
    };
    int i = -1;
    for (int j = 0; j < sizeof(baud)/sizeof(baud[0]); j += 3) {
        if (baud[j] == baudrate) {
            i = j;
            break;
        }
    }
    if (i == -1) {
        printf("baudrate not supported \n");
        return -1;
    }

    int err = 0;
    err = libusb_control_transfer(dev_handle, CTRL_OUT, 0x9a, 0x1312, baud[i * 3 + 1], NULL, 0, 1000);
    if (err < 0) {
        printf("could not set baudrate \n");
        return err;
    }

    err = libusb_control_transfer(dev_handle, CTRL_OUT, 0x9a, 0x0f2c, baud[i * 3 + 2], NULL, 0, 1000);
    if (err < 0) {
        printf("could not set baudrate \n");
        return err;
    }
    return err;
}

int CH340::up() {
    int err = 0;
    err = libusb_control_transfer(dev_handle, CTRL_OUT, 0xa4, ~((dtr ? 1 << 5 : 0) | (rts ? 1 << 6 : 0)), 0, NULL, 0, 1000);
    if (err < 0) {
        printf("first handshake failed \n");
        return err;
    }
    return err;
}

int CH340::bulk_write(unsigned char endpoint, unsigned char* data, int length, unsigned int timeout) {
    int err = 0;
    
    int transferred = 0;
    err = libusb_bulk_transfer(dev_handle, endpoint, data, length, &transferred, timeout);
    if (err < 0) {
        printf("error during bulk write \n");
        return err;
    }

    printf("transferred %d bytes \n", transferred);
    
    return err;
}

int CH340::bulk_read(unsigned char endpoint, unsigned char* data, int length, unsigned int timeout) {
    int err = 0;

    int transferred = 0;
    err = libusb_bulk_transfer(dev_handle, endpoint, data, length, &transferred, timeout);
    if (err < 0) {
        printf("error during bulk read \n");
        return err;
    }

    std::cout << "read bytes: " << transferred << std::endl;

    return transferred;
}
