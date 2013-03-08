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

#ifndef __MASS_H
#define __MASS_H

typedef struct _usbd_mass_storage usbd_mass_storage;

/* Definitions of Mass Storage Class from:
 *
 * (A) "Universal Serial Bus Mass Storage Class Bulk-Only Transport
 *      Revision 1.0"
 *
 * (B) "Universal Serial Bus Mass Storage Class Specification Overview
 *      Revision 1.0"
 */

/* (A) Table 4.5: Mass Storage Device Class Code */
#define USB_CLASS_MASS			0x08

/* (B) Table 2.1: Class Subclass Code */
#define USB_MASS_SUBCLASS_RBC		0x01
#define USB_MASS_SUBCLASS_ATAPI		0x02
#define USB_MASS_SUBCLASS_UFI		0x04
#define USB_MASS_SUBCLASS_SCSI		0x06
#define USB_MASS_SUBCLASS_LOCKABLE	0x07
#define USB_MASS_SUBCLASS_IEEE1667	0x08

/* (B) Table 3.1 Mass Storage Interface Class Control Protocol Codes */
#define USB_MASS_PROTOCOL_CBI		0x00
#define USB_MASS_PROTOCOL_CBI_ALT	0x01
#define USB_MASS_PROTOCOL_BBB		0x50

/* (B) Table 4.1 Mass Storage Request Codes */
#define USB_MASS_REQ_CODES_ADSC		0x00
#define USB_MASS_REQ_CODES_GET		0xFC
#define USB_MASS_REQ_CODES_PUT		0xFD
#define USB_MASS_REQ_CODES_GML		0xFE
#define USB_MASS_REQ_CODES_BOMSR	0xFF

/* (A) Table 3.1/3.2 Class-Specific Request Codes */
#define USB_MASS_REQ_BULK_ONLY_RESET	0xFF
#define USB_MASS_REQ_GET_MAX_LUN	0xFE

usbd_mass_storage *usb_mass_init(usbd_device *usbd_dev,
				 u8 ep_in, u8 ep_in_size,
				 u8 ep_out, u8 ep_out_size,
				 const char *vendor_id,
				 const char *product_id,
				 const char *product_revision_level,
                 const u32 block_count,
				 int (*read_block)(u32 lba, u8 *copy_to),
				 int (*write_block)(u32 lba, const u8 *copy_from));

#endif
