/** @defgroup usb_file USB

@ingroup STM32F1xx

@brief <b>libopencm3 STM32F1xx USB</b>

*/

/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2010 Gareth McMullin <gareth@blacksphere.co.nz>
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
/** @addtogroup usb_file */
/** @{ */

#include <string.h>
#include <libopencm3/usb/usbd.h>
#include "usb_private.h"

/** The buffer used for control messages, unless overwritten in the local project. */
u8 usbd_control_buffer[128] __attribute__((weak));

/** @brief Main initialization entry point.

Initialize the USB firmware library to implement the USB device described
by the descriptors provided.

It is required that the 48MHz USB clock is already available.

@param driver TODO
@param dev Pointer to USB device descriptor. This must not be changed while
	   the device is in use.
@param conf Pointer to array of USB configuration descriptors. These must
	    not be changed while the device is in use. The length of this
	    array is determined by the bNumConfigurations field in the
	    device descriptor.
@param strings The strings returned to the host.  Index 0 maps to USB index 1.
@param num_strings The number of strings defined.

@return Pointer to the usb_device struct.
*/
usbd_device *usbd_init(const usbd_driver *driver,
		       const struct usb_device_descriptor *dev,
		const struct usb_config_descriptor *conf,
		       const char **strings, int num_strings)
{
	usbd_device *usbd_dev;

	usbd_dev = driver->init();

	usbd_dev->driver = driver;
	usbd_dev->desc = dev;
	usbd_dev->config = conf;
	usbd_dev->strings = strings;
	usbd_dev->num_strings = num_strings;
	usbd_dev->ctrl_buf = usbd_control_buffer;
	usbd_dev->ctrl_buf_len = sizeof(usbd_control_buffer);

	usbd_dev->user_callback_ctr[0][USB_TRANSACTION_SETUP] =
	    _usbd_control_setup;
	usbd_dev->user_callback_ctr[0][USB_TRANSACTION_OUT] =
	    _usbd_control_out;
	usbd_dev->user_callback_ctr[0][USB_TRANSACTION_IN] =
	    _usbd_control_in;

	return usbd_dev;
}

/** @brief Registers a callback function for the USB event 'RESET' takes place.

@param[in] usbd_dev The USB device to interact with.
@param[in] callback The callback.
*/
void usbd_register_reset_callback(usbd_device *usbd_dev, void (*callback)(void))
{
	usbd_dev->user_callback_reset = callback;
}

/** @brief Registers a callback function for the USB event 'SUSPEND' takes place.

@param[in] usbd_dev The USB device to interact with.
@param[in] callback The callback.
*/
void usbd_register_suspend_callback(usbd_device *usbd_dev,
				    void (*callback)(void))
{
	usbd_dev->user_callback_suspend = callback;
}

/** @brief Registers a callback function for the USB event 'RESUME' takes place.

@param[in] usbd_dev The USB device to interact with.
@param[in] callback The callback.
*/
void usbd_register_resume_callback(usbd_device *usbd_dev,
				   void (*callback)(void))
{
	usbd_dev->user_callback_resume = callback;
}

/** @brief Registers a callback function for the USB event 'SOF' takes place.

@note This is called every 1ms, so be very careful in this routine.

@param[in] usbd_dev The USB device to interact with.
@param[in] callback The callback.
*/
void usbd_register_sof_callback(usbd_device *usbd_dev, void (*callback)(void))
{
	usbd_dev->user_callback_sof = callback;
}

/** @brief Sets the size of the control buffer.

@param[in] usbd_dev The USB device to interact with.
@param[in] size The size of the control buffer in bytes.
*/
void usbd_set_control_buffer_size(usbd_device *usbd_dev, u16 size)
{
	usbd_dev->ctrl_buf_len = size;
}

/** @brief Resets the USB subsystem back to a USB 'RESET' state.

@param[in] usbd_dev The USB device to interact with.
*/
void _usbd_reset(usbd_device *usbd_dev)
{
	usbd_dev->current_address = 0;
	usbd_dev->current_config = 0;
	usbd_ep_setup(usbd_dev, 0, USB_ENDPOINT_ATTR_CONTROL, 64, NULL);
	usbd_dev->driver->set_address(usbd_dev, 0);

	if (usbd_dev->user_callback_reset)
		usbd_dev->user_callback_reset();
}

/* Functions to wrap the low-level driver */

/** @brief Called by the main program periodically to service the USB subsystem.

@param[in] usbd_dev The USB device to interact with.
 */
void usbd_poll(usbd_device *usbd_dev)
{
	usbd_dev->driver->poll(usbd_dev);
}

/** @brief Disconnects the driver

@param[in] usbd_dev The USB device to interact with.
@param[in] disconnected TBD
 */
void usbd_disconnect(usbd_device *usbd_dev, bool disconnected)
{
	/* not all drivers support disconnection */
	if (usbd_dev->driver->disconnect)
		usbd_dev->driver->disconnect(usbd_dev, disconnected);
}

/** @brief Sets up an endpoint.

@note Control endpoints ignore the direction bit (bit 7).
@note If bit 7 of the addr is '1' then the endpoint is IN.
@note If bit 7 of the addr is '0' then the endpoint is OUT.

@param[in] usbd_dev The USB device to interact with.
@param[in] addr The address to assign the endpoint.
@param[in] max_size The endpoint size.
@param[in] callback The function to call when an endpoint either has data
	            present (OUT), or completed sending the data (IN).
*/
void usbd_ep_setup(usbd_device *usbd_dev, u8 addr, u8 type, u16 max_size,
		   void (*callback)(usbd_device *usbd_dev, u8 ep))
{
	usbd_dev->driver->ep_setup(usbd_dev, addr, type, max_size, callback);
}

/** @brief Writes a single packet of data to the host.

@note The data must be no larger then the endpoint max_size.

@param[in] usbd_dev The USB device to interact with.
@param[in] addr The endpoint address write to.
@param[in] buf The data to write.
@param[in] len The number of bytes to write.

@return The number of bytes written.
*/
u16 usbd_ep_write_packet(usbd_device *usbd_dev, u8 addr,
			 const void *buf, u16 len)
{
	return usbd_dev->driver->ep_write_packet(usbd_dev, addr, buf, len);
}

/** @brief Reads a single packet of data from the host.

@note The data will be no larger then the endpoint max_size or the len,
      whichever is smaller.

@param[in] usbd_dev The USB device to interact with.
@param[in] addr The endpoint address read from.
@param[out] buf The location to read into.
@param[in] len The maximum number of bytes read.

@return The number of bytes read.
*/
u16 usbd_ep_read_packet(usbd_device *usbd_dev, u8 addr, void *buf, u16 len)
{
	return usbd_dev->driver->ep_read_packet(usbd_dev, addr, buf, len);
}

/** @brief Sets the USB 'STALL' status for the specified endpoint.

@param[in] usbd_dev The USB device to interact with.
@param[in] addr The endpoint address to change.
@param[in] stall If 0 remove the 'STALL' condition, otherwise apply the
	         'STALL' condition to the endpoint.
*/
void usbd_ep_stall_set(usbd_device *usbd_dev, u8 addr, u8 stall)
{
	usbd_dev->driver->ep_stall_set(usbd_dev, addr, stall);
}

/** @brief Gets the USB 'STALL' status for the specified endpoint.

@param[in] usbd_dev The USB device to interact with.
@param[in] addr The endpoint address to get.

@return 0 if not stalled, 1 otherwise.
*/
u8 usbd_ep_stall_get(usbd_device *usbd_dev, u8 addr)
{
	return usbd_dev->driver->ep_stall_get(usbd_dev, addr);
}

/** @brief Sets the USB endpoint status to 'NAK' or 'VALID'.

@param[in] usbd_dev The USB device to interact with.
@param[in] addr The endpoint address to get.
@param[in] nak If 0, set the endpoint to 'NAK', otherwise set the endpoint
	       to 'VALID'.
*/
void usbd_ep_nak_set(usbd_device *usbd_dev, u8 addr, u8 nak)
{
	usbd_dev->driver->ep_nak_set(usbd_dev, addr, nak);
}
/**@}*/
