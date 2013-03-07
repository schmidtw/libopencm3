/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2013 Weston Schmidt <weston_schmidt@alumni.purdue.edu>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/f1/gpio.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/mass.h>
#include <libopencm3/cm3/scb.h>

static const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0110,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0x0483,
	.idProduct = 0x5740,
	.bcdDevice = 0x0200,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor mass_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x81,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = 0x02,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}};

static const struct usb_interface_descriptor mass_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_MASS,
	.bInterfaceSubClass = USB_MASS_SUBCLASS_SCSI,
	.bInterfaceProtocol = USB_MASS_PROTOCOL_BBB,
	.iInterface = 0,

	.endpoint = mass_endp,

	.extra = NULL,
	.extralen = 0
}};

static const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = mass_iface,
}};

static const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0x80,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

static const char *usb_strings[] = {
	"Black Sphere Technologies",
	"Mass Storage Demo",
	"0123456789ABCDEF",
};

#if 0
static int mass_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, u8 **buf,
		u16 *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)usbd_dev;

	switch (req->bRequest) {
	case USB_MASS_REQ_BULK_ONLY_RESET:
		/* Do any special reset code here. */
		return USBD_REQ_HANDLED;
	case USB_MASS_REQ_GET_MAX_LUN:
		/* Return the number of LUNs.  We use 0. */
		*buf[0] = 0;
		*len = 1;
		return USBD_REQ_HANDLED;
	}

	return USBD_REQ_NOTSUPP;
}

static bool is_valid_cbw(struct usb_mass_cbw *cbw, u16 size)
{
	if ((size == sizeof(struct usb_mass_cbw)) &&
	    (USB_MASS_CBW_SIGNATURE == cbw->dCBWSignature))
	{
		return true;
	}

	return false;
}

static bool is_meaningful_cbw(struct usb_mass_cbw *cbw)
{
	if ((cbw->bCBWLUN < 16) &&
	    ((0 < cbw->bCBWCBLength) && (cbw->bCBWCBLength <= 16)) &&
	    (0 == (0x7f & cbw->bmCBWFlags)))
	{
		return true;
	}

	return false;
}

static void csw_set_failure(struct usb_mass_csw *csw, u32 tag)
{
	csw->dCSWSignature = USB_MASS_CBW_SIGNATURE;
	csw->dCSWTag = tag
	csw->dCSWDataResidue = 0x24;
	csw->bCSWStatus = 1;
}

static void mass_data_rx_cb(usbd_device *usbd_dev, u8 ep)
{
	static bool expect_cbw = true;
	u8 buf[64];
	int len;
	struct usb_mass_cbw *cbw;
	struct usb_mass_csw csw;

	(void)ep;

	len = usbd_ep_read_packet(usbd_dev, ep, buf, 64);

	if (true == expect_cbw) {
		if (31 != len) {
			goto error;
		}

		cbw = (struct usb_mass_cbw*) buf;

		if (USB_MASS_CBW_SIGNATURE != cbw->dCBWSignature) {
			goto error;
		}

		csw.dCSWSignature = USB_MASS_CBW_SIGNATURE;
		csw.dCSWTag = cbw->dCBWTag;
		csw.dCSWDataResidue = 0x24;
		csw.bCSWStatus = 0;

		usbd_ep_write_packet(usbd_dev, 0x81, &csw, sizeof(csw));
	} else {
	}

	return;

error:
	usbd_ep_stall_set(usbd_dev, 0x02, 1);
}

static void mass_set_config(usbd_device *usbd_dev, u16 wValue)
{
	(void)wValue;
	(void)usbd_dev;

	usbd_ep_setup(usbd_dev, 0x02, USB_ENDPOINT_ATTR_BULK, 64, mass_data_rx_cb);
	usbd_ep_setup(usbd_dev, 0x81, USB_ENDPOINT_ATTR_BULK, 64, NULL);

	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				mass_control_request);
}
#endif

int main(void)
{
	int i;

	usbd_device *usbd_dev;

	SCB_VTOR = (u32) 0x08005000;

	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN |
				    RCC_APB2ENR_IOPCEN);

	/* Setup GPIOC Pin 12 to pull up the D+ high, so autodect works
	 * with the bootloader.  The circuit is active low. */
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	gpio_clear(GPIOC, GPIO12);

	/* Setup GPIOA Pin 5 for the LED */
	gpio_set(GPIOA, GPIO5);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO5);

	usbd_dev = usbd_init(&stm32f103_usb_driver, &dev, &config, usb_strings, 3);
	usb_mass_init(usbd_dev, 0x81, 64, 0x02, 64, "Wes", "TD-1", "0.00");
	//usbd_register_set_config_callback(usbd_dev, mass_set_config);

	for (i = 0; i < 0x800000; i++)
		__asm__("nop");
	gpio_clear(GPIOA, GPIO5);

	while (1)
		usbd_poll(usbd_dev);
}
