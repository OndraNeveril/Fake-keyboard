#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/msc.h>  // for MSC support helpers
#include <string.h>

/* USB device descriptor */
static const struct usb_device_descriptor dev_descr = {
    .bLength = USB_DT_DEVICE_SIZE,
    .bDescriptorType = USB_DT_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x1234,
    .idProduct = 0x5678,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

/* USB MSC Bulk-only interface endpoints */
static const struct usb_endpoint_descriptor msc_endpoints[] = {
    {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x81, /* IN endpoint */
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 0,
    },
    {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x01, /* OUT endpoint */
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 0,
    },
};

/* USB MSC interface */
static const struct usb_interface_descriptor msc_iface = {
    .bLength = USB_DT_INTERFACE_SIZE,
    .bDescriptorType = USB_DT_INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 2,
    .bInterfaceClass = USB_CLASS_MSC,  /* 0x08 */
    .bInterfaceSubClass = 0x06,        /* SCSI transparent */
    .bInterfaceProtocol = 0x50,        /* Bulk-only transport */
    .iInterface = 0,
    .endpoint = msc_endpoints,
};

/* USB interface array */
static const struct usb_interface ifaces[] = {
    {
        .num_altsetting = 1,
        .altsetting = &msc_iface,
    }
};

/* Configuration descriptor */
static const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength = 0, /* libopencm3 calculates this */
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfiguration = 0,
    .bmAttributes = 0xC0,
    .bMaxPower = 0x32,
    .interface = ifaces,
};

/* RAM disk storage - 32 sectors, 512 bytes each */
#define MSC_RAMDISK_SECTORS 32
#define MSC_RAMDISK_SECTOR_SIZE 512
static uint8_t msc_ramdisk[MSC_RAMDISK_SECTORS * MSC_RAMDISK_SECTOR_SIZE];

/* usbd device handle */
static usbd_device *usbd_dev;

/* MSC callbacks */
int msc_read_sector(uint32_t lba, uint8_t *buf, uint32_t count) {
    if ((lba + count) > MSC_RAMDISK_SECTORS)
        return -1;
    memcpy(buf, &msc_ramdisk[lba * MSC_RAMDISK_SECTOR_SIZE], count * MSC_RAMDISK_SECTOR_SIZE);
    return 0;
}

int msc_write_sector(uint32_t lba, const uint8_t *buf, uint32_t count) {
    if ((lba + count) > MSC_RAMDISK_SECTORS)
        return -1;
    memcpy(&msc_ramdisk[lba * MSC_RAMDISK_SECTOR_SIZE], buf, count * MSC_RAMDISK_SECTOR_SIZE);
    return 0;
}

int msc_get_sector_count(void) {
    return MSC_RAMDISK_SECTORS;
}

int msc_get_sector_size(void) {
    return MSC_RAMDISK_SECTOR_SIZE;
}

/* MSC callback struct */
static const struct usb_msc_callbacks msc_cb = {
    .read_sector = msc_read_sector,
    .write_sector = msc_write_sector,
    .get_sector_count = msc_get_sector_count,
    .get_sector_size = msc_get_sector_size,
};

/* USB control request handler */
static int msc_control_request(usbd_device *dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
                               usbd_control_complete_callback *complete) {
    return usb_msc_control_request(dev, req, buf, len, complete);
}

/* USB set configuration handler */
static void msc_set_config(usbd_device *dev, uint16_t wValue) {
    (void)wValue;
    /* Setup MSC bulk endpoints */
    usbd_ep_setup(dev, 0x81, USB_ENDPOINT_ATTR_BULK, 64, NULL);
    usbd_ep_setup(dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, NULL);
    
    /* Register MSC class control callback */
    usbd_register_control_callback(
        dev,
        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
        USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
        msc_control_request);

    usb_msc_init(dev, &msc_cb);
}

/* Clock setup and GPIO setup left as usual (depends on STM32 family) */
/* ... */

/* Main */
int main(void) {
    rcc_clock_setup_in_hse_8mhz_out_72mhz();
    rcc_periph_clock_enable(RCC_GPIOA);
    
    /* Setup USB pins (PA11 = USB DM, PA12 = USB DP) */
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO11);
    gpio_set_mode(GPIOA, GPIO_MODE_AF_PP, GPIO_CNF_OUTPUT_PUSH_PULL, GPIO12);
    
    /* Initialize USB device */
    usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev_descr, &config,
                        usb_strings, 3,
                        NULL, NULL);
    
    usbd_register_set_config_callback(usbd_dev, msc_set_config);

    while (1) {
        usbd_poll(usbd_dev);
    }

    return 0;
}

