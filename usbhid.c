#include <ctype.h>
#include <stdlib.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>

/* Define this to include the DFU APP interface. */
#define INCLUDE_DFU_INTERFACE

#ifdef INCLUDE_DFU_INTERFACE
#include <libopencm3/cm3/scb.h>
#include <libopencm3/usb/dfu.h>
#endif

static usbd_device *usbd_dev;

const struct usb_device_descriptor dev_descr = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,
	.idProduct = 0x5710,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

static const uint8_t hid_report_descriptor[] = {
	0x05, 0x01, // Usage Page (Generic Desktop)
	0x09, 0x06, // Usage (Keyboard)
	0xA1, 0x01, // Collection (Application)
	0x05, 0x07, //   Usage Page (Key Codes)
	0x19, 0xE0, //   Usage Minimum (224)
	0x29, 0xE7, //   Usage Maximum (231)
	0x15, 0x00, //   Logical Minimum (0)
	0x25, 0x01, //   Logical Maximum (1)
	0x75, 0x01, //   Report Size (1)
	0x95, 0x08, //   Report Count (8)
	0x81, 0x02, //   Input (Data, Variable, Absolute) ; Modifier byte
	0x95, 0x01, //   Report Count (1)
	0x75, 0x08, //   Report Size (8)
	0x81, 0x01, //   Input (Constant) ; Reserved byte
	0x95, 0x06, //   Report Count (6)
	0x75, 0x08, //   Report Size (8)
	0x15, 0x00, //   Logical Minimum (0)
	0x25, 0x65, //   Logical Maximum (101)
	0x05, 0x07, //   Usage Page (Key Codes)
	0x19, 0x00, //   Usage Minimum (0)
	0x29, 0x65, //   Usage Maximum (101)
	0x81, 0x00, //   Input (Data, Array)
	0xC0        // End Collection
};

static const struct {
	struct usb_hid_descriptor hid_descriptor;
	struct {
		uint8_t bReportDescriptorType;
		uint16_t wDescriptorLength;
	} __attribute__((packed)) hid_report;
} __attribute__((packed)) hid_function = {
	.hid_descriptor = {
		.bLength = sizeof(hid_function),
		.bDescriptorType = USB_DT_HID,
		.bcdHID = 0x0100,
		.bCountryCode = 0,
		.bNumDescriptors = 1,
	},
	.hid_report = {
		.bReportDescriptorType = USB_DT_REPORT,
		.wDescriptorLength = sizeof(hid_report_descriptor),
	}
};

const struct usb_endpoint_descriptor hid_endpoint = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x81,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 8,
	.bInterval = 10,
};

const struct usb_interface_descriptor hid_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = 1, /* boot */
	.bInterfaceProtocol = 1, 
	.iInterface = 0,

	.endpoint = &hid_endpoint,

	.extra = &hid_function,
	.extralen = sizeof(hid_function),
};

#ifdef INCLUDE_DFU_INTERFACE
const struct usb_dfu_descriptor dfu_function = {
	.bLength = sizeof(struct usb_dfu_descriptor),
	.bDescriptorType = DFU_FUNCTIONAL,
	.bmAttributes = USB_DFU_CAN_DOWNLOAD | USB_DFU_WILL_DETACH,
	.wDetachTimeout = 255,
	.wTransferSize = 1024,
	.bcdDFUVersion = 0x011A,
};

const struct usb_interface_descriptor dfu_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 0,
	.bInterfaceClass = 0xFE,
	.bInterfaceSubClass = 1,
	.bInterfaceProtocol = 1,
	.iInterface = 0,

	.extra = &dfu_function,
	.extralen = sizeof(dfu_function),
};
#endif

const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = &hid_iface,
#ifdef INCLUDE_DFU_INTERFACE
}, {
	.num_altsetting = 1,
	.altsetting = &dfu_iface,
#endif
}};

const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
#ifdef INCLUDE_DFU_INTERFACE
	.bNumInterfaces = 2,
#else
	.bNumInterfaces = 1,
#endif
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0xC0,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static const char *usb_strings[] = {
	"Black Sphere Technologies",
	"HID Demo",
	"DEMO",
};

/* Buffer to be used for control requests. */
uint8_t usbd_control_buffer[128];

static enum usbd_request_return_codes hid_control_request(usbd_device *dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
			void (**complete)(usbd_device *, struct usb_setup_data *))
{
	(void)complete;
	(void)dev;

	if((req->bmRequestType != 0x81) ||
	   (req->bRequest != USB_REQ_GET_DESCRIPTOR) ||
	   (req->wValue != 0x2200))
		return USBD_REQ_NOTSUPP;

	/* Handle the HID report descriptor. */
	*buf = (uint8_t *)hid_report_descriptor;
	*len = sizeof(hid_report_descriptor);

	return USBD_REQ_HANDLED;
}

#ifdef INCLUDE_DFU_INTERFACE
static void dfu_detach_complete(usbd_device *dev, struct usb_setup_data *req)
{
	(void)req;
	(void)dev;

	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO10);
	gpio_set(GPIOA, GPIO10);
	scb_reset_core();
}

static enum usbd_request_return_codes dfu_control_request(usbd_device *dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
			void (**complete)(usbd_device *, struct usb_setup_data *))
{
	(void)buf;
	(void)len;
	(void)dev;

	if ((req->bmRequestType != 0x21) || (req->bRequest != DFU_DETACH))
		return USBD_REQ_NOTSUPP; /* Only accept class request. */

	*complete = dfu_detach_complete;

	return USBD_REQ_HANDLED;
}
#endif

static void hid_set_config(usbd_device *dev, uint16_t wValue)
{
	(void)wValue;
	(void)dev;

	usbd_ep_setup(dev, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 4, NULL);

	usbd_register_control_callback(
				dev,
				USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				hid_control_request);
#ifdef INCLUDE_DFU_INTERFACE
	usbd_register_control_callback(
				dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				dfu_control_request);
#endif

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	/* SysTick interrupt every N clock pulses: set reload to N-1 */
	systick_set_reload(49999);
	systick_interrupt_enable();
	systick_counter_enable();
}

int main(void)
{
	rcc_clock_setup_pll(&rcc_hsi_configs[RCC_CLOCK_HSI_48MHZ]);

	rcc_periph_clock_enable(RCC_GPIOA);
	/*
	 * This is a somewhat common cheap hack to trigger device re-enumeration
	 * on startup.  Assuming a fixed external pullup on D+, (For USB-FS)
	 * setting the pin to output, and driving it explicitly low effectively
	 * "removes" the pullup.  The subsequent USB init will "take over" the
	 * pin, and it will appear as a proper pullup to the host.
	 * The magic delay is somewhat arbitrary, no guarantees on USBIF
	 * compliance here, but "it works" in most places.
	 */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	gpio_clear(GPIOA, GPIO12);
	for (unsigned i = 0; i < 800000; i++) {
		__asm__("nop");
	}

	usbd_dev = usbd_init(&st_usbfs_v1_usb_driver, &dev_descr, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(usbd_dev, hid_set_config);

	while (1)
		usbd_poll(usbd_dev);
}

void string_formating(char a, uint8_t *buf){
    if (isdigit(a)) {
        if (a == '0') {
            buf[2] = 39;
        } else {
            int b = a - '0';
            buf[2] = 29 + b;
        }	
    } else if (isalpha(a)) {
        if (isupper(a)) {
            buf[0] = 2;
            int b = (int)a;
            buf[2] = b - 61;
        } else {
            int b = (int)a;
            buf[2] = b - 93;
        }
    } else if (a == ' '){
	buf[2] = 44;
    } else if (a == '='){
	buf[2] = 46;
    } else if (a == ':'){
	buf[0] = 2;
	buf[2] = 51;
    } else if (a == '/'){
	buf[2] = 56;
    } else if (a == '.'){
	buf[2] = 55;
    } else if (a == '?'){
	buf[0] = 2;
	buf[2] = 56;
    } else if (a == '\n'){
	buf[2] = 88;
    } else if (a == '@'){
	buf[0] = 0x08;
    } else if (a == '#'){
	buf[0] = 0x01;
	buf[2] = 22;
    } else if (a == '&'){
	buf[0] = 0x04;
	buf[2] = 61;	
    } else if (a == '('){
	buf[0] = 2;
	buf[2] = 38;
    } else if (a == ')'){
	buf[0] = 2;
	buf[2] = 39;
    } else if (a == '\''){
	buf[2] = 52;
    } else if (a == '!'){
	buf[0] = 2;
	buf[2] = 30;
    }
}
static int d = 1000;
static char t0[] = "  @";
static char t1[] = "cmd\n";
static char t2[] = "notepad script.py\n";
static char t3[] = "print('Hello world!')#&";
static char t4[] = "python script.py\n"; // firefox https://www.youtube.com/watch?v=dQw4w9WgXcQ\n";
static int tick = 0;

void sys_tick_handler(void)
{
		uint8_t buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	if ((tick > d) && (tick < d + 2*sizeof(t0))) {
		if (tick % 2 == 0){
			unsigned int t = (tick - d)/2;
			string_formating(t0[t], (uint8_t*) buf);
		}
		usbd_ep_write_packet(usbd_dev, 0x81, buf, 8);
	}
	if ((tick > 2*d) && (tick < 2*d + 2*sizeof(t1))) {
		if (tick % 2 == 0){
			unsigned int t = (tick - 2*d)/2;
			string_formating(t1[t], (uint8_t*) buf);
		}
		usbd_ep_write_packet(usbd_dev, 0x81, buf, 8);
	}
	if ((tick > 3*d) && (tick < 3*d + 2*sizeof(t2))) {
		if (tick % 2 == 0){
			unsigned int t = (tick - 3*d)/2;
			string_formating(t2[t], (uint8_t*) buf);
		}
		usbd_ep_write_packet(usbd_dev, 0x81, buf, 8);
	}
	if ((tick > 4*d) && (tick < 4*d + 2*sizeof(t3))) {
		if (tick % 2 == 0){
			unsigned int t = (tick - 4*d)/2;
			string_formating(t3[t], (uint8_t*) buf);
		}
		usbd_ep_write_packet(usbd_dev, 0x81, buf, 8);
	}if ((tick > 5*d) && (tick < 5*d + 2*sizeof(t4))) {
		if (tick % 2 == 0){
			unsigned int t = (tick - 5*d)/2;
			string_formating(t4[t], (uint8_t*) buf);
		}
		usbd_ep_write_packet(usbd_dev, 0x81, buf, 8);
	}
	tick++;
}
