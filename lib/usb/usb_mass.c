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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libopencm3/cm3/common.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/mass.h>
#include "usb_private.h"

/* Definitions of Mass Storage Class from:
 *
 * (A) "Universal Serial Bus Mass Storage Class Bulk-Only Transport
 *      Revision 1.0"
 *
 * (B) "Universal Serial Bus Mass Storage Class Specification Overview
 *      Revision 1.0"
 */

/* (A) Table 3.1/3.2 Class-Specific Request Codes */
#define USB_MASS_REQ_BULK_ONLY_RESET	0xFF
#define USB_MASS_REQ_GET_MAX_LUN	0xFE

#define CBW_SIGNATURE			0x43425355
#define CBW_STATUS_SUCCESS		0
#define CBW_STATUS_FAILED		1
#define CBW_STATUS_PHASE_ERROR		2

/* Required SCSI Commands */
#define SCSI_FORMAT_UNIT		0x04
#define SCSI_INQUIRY			0x12
#define SCSI_READ_6			0x08
#define SCSI_READ_10			0x28
#define SCSI_READ_CAPACITY		0x25
#define SCSI_REPORT_LUNS		0xA0
#define SCSI_REQUEST_SENSE		0x03
#define SCSI_SEND_DIAGNOSTIC		0x1D
#define SCSI_TEST_UNIT_READY		0x00
#define SCSI_WRITE_6			0x0A

/* Optional SCSI Commands */
#define SCSI_MODE_SELECT_6			0x15
#define SCSI_MODE_SELECT_10			0x55
#define SCSI_MODE_SENSE_6			0x1A
#define SCSI_MODE_SENSE_10			0x5A
#define SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL	0x1E
#define SCSI_READ_12				0xA8
#define SCSI_READ_FORMAT_CAPACITIES		0x23
#define SCSI_READ_TOC_PMA_ATIP			0x43
#define SCSI_START_STOP_UNIT			0x1B
#define SCSI_SYNCHRONIZE_CACHE			0x35
#define SCSI_VERIFY				0x2F
#define SCSI_WRITE_10				0x2A
#define SCSI_WRITE_12				0xAA

/* The sense codes */
enum sbc_sense_key {
	SBC_SENSE_KEY_NO_SENSE			= 0x00,
	SBC_SENSE_KEY_RECOVERED_ERROR		= 0x01,
	SBC_SENSE_KEY_NOT_READY			= 0x02,
	SBC_SENSE_KEY_MEDIUM_ERROR		= 0x03,
	SBC_SENSE_KEY_HARDWARE_ERROR		= 0x04,
	SBC_SENSE_KEY_ILLEGAL_REQUEST		= 0x05,
	SBC_SENSE_KEY_UNIT_ATTENTION		= 0x06,
	SBC_SENSE_KEY_DATA_PROTECT		= 0x07,
	SBC_SENSE_KEY_BLANK_CHECK		= 0x08,
	SBC_SENSE_KEY_VENDOR_SPECIFIC		= 0x09,
	SBC_SENSE_KEY_COPY_ABORTED		= 0x0A,
	SBC_SENSE_KEY_ABORTED_COMMAND		= 0x0B,
	SBC_SENSE_KEY_VOLUME_OVERFLOW		= 0x0D,
	SBC_SENSE_KEY_MISCOMPARE		= 0x0E
};

enum sbc_asc {
	SBC_ASC_NO_ADDITIONAL_SENSE_INFORMATION	= 0x00,
	SBC_ASC_PERIPHERAL_DEVICE_WRITE_FAULT	= 0x03,
	SBC_ASC_LOGICAL_UNIT_NOT_READY		= 0x04,
	SBC_ASC_UNRECOVERED_READ_ERROR		= 0x11,
	SBC_ASC_INVALID_COMMAND_OPERATION_CODE	= 0x20,
	SBC_ASC_LBA_OUT_OF_RANGE		= 0x21,
	SBC_ASC_INVALID_FIELD_IN_CDB		= 0x24,
	SBC_ASC_WRITE_PROTECTED			= 0x27,
	SBC_ASC_NOT_READY_TO_READY_CHANGE	= 0x28,
	SBC_ASC_FORMAT_ERROR			= 0x31,
	SBC_ASC_MEDIUM_NOT_PRESENT		= 0x3A
};

enum sbc_ascq {
	SBC_ASCQ_NA				= 0x00,
	SBC_ASCQ_FORMAT_COMMAND_FAILED		= 0x01,
	SBC_ASCQ_INITIALIZING_COMMAND_REQUIRED	= 0x02,
	SBC_ASCQ_OPERATION_IN_PROGRESS		= 0x07
};

enum trans_event {
	EVENT_CBW_VALID,
	EVENT_NEED_STATUS
};

struct usb_mass_cbw {
	u32 dCBWSignature;
	u32 dCBWTag;
	u32 dCBWDataTransferLength;
	u8  bmCBWFlags;
	u8  bCBWLUN;
	u8  bCBWCBLength;
	u8  CBWCB[16];
} __attribute__((packed));

struct usb_mass_csw {
	u32 dCSWSignature;
	u32 dCSWTag;
	u32 dCSWDataResidue;
	u8  bCSWStatus;
} __attribute__((packed));

struct sbc_sense_info {
	u8 key;
	u8 asc;
	u8 ascq;
};

struct usb_mass_trans {
	u8 cbw_cnt;	/* Read until 31 bytes */
	union {
		struct usb_mass_cbw cbw;
		u8 buf[1];
	} cbw;

	u32 bytes_to_read;
	u32 bytes_to_write;
	u32 count;		/* Either read until equal to bytes_to_read or
				   write until equal to bytes_to_write. */
	u8 *data;
	u8 msd_buf[512];

	bool csw_valid;
	u8 csw_sent;		/* Write until 13 bytes */
	union {
		struct usb_mass_csw csw;
		u8 buf[1];
	} csw;
};

struct _usbd_mass_storage {
	usbd_device *usbd_dev;
	u8 ep_in;
	u8 ep_in_size;
	u8 ep_out;
	u8 ep_out_size;

	const char *vendor_id;
	const char *product_id;
	const char *product_revision_level;
	u32 block_count;
	u8 *lba_0_ptr;

	struct usb_mass_trans trans;
	struct sbc_sense_info sense;
};

static usbd_mass_storage _mass_storage;

static const u8 _spc3_inquiry_response[36] = {
	0x00,	/* Byte 0: Peripheral Qualifier = 0, Peripheral Device Type = 0 */
	0x80,	/* Byte 1: RMB = 1, Reserved = 0 */
	0x05,	/* Byte 2: Version = 0 */
	0x02,	/* Byte 3: Obsolete = 0, NormACA = 0, HiSup = 0, Response Data Format = 2 */
	0x20,	/* Byte 4: Additional Length (n-4) = 31 + 4 */
	0x00,	/* Byte 5: SCCS = 0, ACC = 0, TPGS = 0, 3PC = 0, Reserved = 0, Protect = 0 */
	0x80,	/* Byte 6: BQue = 1, EncServ = 0, VS = 0, MultiP = 0, MChngr = 0, Obsolete = 0, Addr16 = 0 */
	0x00,	/* Byte 7: Obsolete = 0, Wbus16 = 0, Sync = 0, Linked = 0, CmdQue = 0, VS = 0 */
		/* Byte 8 - Byte 15: Vendor Identification */
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/* Byte 16 - Byte 31: Product Identification */
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
		/* Byte 32 - Byte 35: Product Revision Level */
	0x20, 0x20, 0x20, 0x20
};

static const u8 _spc3_request_sense[18] = {
	0x70,	/* Byte 0: VALID = 0, Response Code = 112 */
	0x00,	/* Byte 1: Obsolete = 0 */
	0x00,	/* Byte 2: Filemark = 0, EOM = 0, ILI = 0, Reserved = 0, Sense Key = 0 */
		/* Byte 3 - Byte 6: Information = 0 */
	0, 0, 0, 0,
	0x0a,	/* Byte 7: Additional Sense Length = 10 */
		/* Byte 8 - Byte 11: Command Specific Info = 0 */
	0, 0, 0, 0,
	0x00,	/* Byte 12: Additional Sense Code (ASC) = 0 */
	0x00,	/* Byte 13: Additional Sense Code Qualifier (ASCQ) = 0 */
	0x00,	/* Byte 14: Field Replaceable Unit Code (FRUC) = 0 */
	0x00,	/* Byte 15: SKSV = 0, SenseKeySpecific[0] = 0 */
	0x00,	/* Byte 16: SenseKeySpecific[0] = 0 */
	0x00	/* Byte 17: SenseKeySpecific[0] = 0 */
};

static int mass_control_request(usbd_device *usbd_dev,
				struct usb_setup_data *req, u8 **buf, u16 *len,
				void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
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

static void set_sbc_status(usbd_mass_storage *ms,
			   enum sbc_sense_key key, 
			   enum sbc_asc asc,
			   enum sbc_ascq ascq)
{
	ms->sense.key = (u8) key;
	ms->sense.asc = (u8) asc;
	ms->sense.ascq = (u8) ascq;
}

static void set_sbc_status_good(usbd_mass_storage *ms)
{
	set_sbc_status(ms,
		       SBC_SENSE_KEY_NO_SENSE,
		       SBC_ASC_NO_ADDITIONAL_SENSE_INFORMATION,
		       SBC_ASCQ_NA);
}

static u8 *get_cbw_buf(struct usb_mass_trans *trans)
{
	return &trans->cbw.cbw.CBWCB[0];
}

static void scsi_read_6(usbd_mass_storage *ms,
			struct usb_mass_trans *trans,
			enum trans_event event)
{
	if (EVENT_CBW_VALID == event) {
		u32 lba;
		u32 blocks;
		u8 *buf;

		buf = get_cbw_buf(trans);
		lba = (buf[2] << 8) | buf[3];
		blocks = buf[4];

		/* both are in terms of 512 byte blocks, so shift by 9 */
		trans->data = &ms->lba_0_ptr[lba << 9];
		trans->bytes_to_write = blocks << 9;
	}
}


static void scsi_read_10(usbd_mass_storage *ms,
			 struct usb_mass_trans *trans,
			 enum trans_event event)
{
	if (EVENT_CBW_VALID == event) {
		u32 lba;
		u32 blocks;
		u8 *buf;

		buf = get_cbw_buf(trans);
		lba = (buf[2] << 24) | (buf[3] << 16) | (buf[4] << 8) | buf[5];
		blocks = (buf[7] << 8) | buf[8];

		/* both are in terms of 512 byte blocks, so shift by 9 */
		trans->data = &ms->lba_0_ptr[lba << 9];
		trans->bytes_to_write = blocks << 9;
		set_sbc_status_good(ms);
	}
}

static void scsi_read_capacity(usbd_mass_storage *ms,
			       struct usb_mass_trans *trans,
			       enum trans_event event)
{
	if (EVENT_CBW_VALID == event) {
		trans->msd_buf[0] = ms->block_count >> 24;
		trans->msd_buf[1] = 0xff & (ms->block_count >> 16);
		trans->msd_buf[2] = 0xff & (ms->block_count >> 8);
		trans->msd_buf[3] = 0xff & ms->block_count;

		/* Block size: 512 */
		trans->msd_buf[4] = 0;
		trans->msd_buf[5] = 0;
		trans->msd_buf[6] = 2;
		trans->msd_buf[7] = 0;
		trans->bytes_to_write = 8;
		set_sbc_status_good(ms);
	}
}

static void scsi_request_sense(usbd_mass_storage *ms,
			       struct usb_mass_trans *trans,
			       enum trans_event event)
{
	if (EVENT_CBW_VALID == event) {
		u8 *buf;

		buf = &trans->cbw.cbw.CBWCB[0];
		trans->bytes_to_write = buf[4];	/* allocation length */
		memcpy(trans->msd_buf, _spc3_request_sense, sizeof(_spc3_request_sense));

		trans->msd_buf[2] = ms->sense.key;
		trans->msd_buf[12] = ms->sense.asc;
		trans->msd_buf[13] = ms->sense.ascq;
	}
}

static void scsi_mode_sense_6(usbd_mass_storage *ms,
			      struct usb_mass_trans *trans,
			      enum trans_event event)
{
	if (EVENT_CBW_VALID == event) {
		u8 *buf;
		u8 page_code;
		//u8 allocation_length;

		buf = &trans->cbw.cbw.CBWCB[0];
		page_code = buf[2];
		//allocation_length = buf[4];

		//if (0x1C == page_code) {	/* Informational Exceptions */
			trans->bytes_to_write = 4;

			trans->msd_buf[0] = 3;	/* Num bytes that follow */
			trans->msd_buf[1] = 0;	/* Medium Type */
			trans->msd_buf[2] = 0;	/* Device specific param */
			trans->csw.csw.dCSWDataResidue = 4;
#if 0
		} else if (0x01 == page_code) {	/* Error recovery */
		} else if (0x3F == page_code) {	/* All */
		} else {
			/* Error */
			trans->csw.csw.bCSWStatus = CBW_STATUS_FAILED;
			set_sbc_status(ms,
				       SBC_SENSE_KEY_ILLEGAL_REQUEST,
				       SBC_ASC_INVALID_FIELD_IN_CDB,
				       SBC_ASCQ_NA);
		}
#endif
	}
}

static void scsi_inquiry(usbd_mass_storage *ms,
			 struct usb_mass_trans *trans,
			 enum trans_event event)
{
	if (EVENT_CBW_VALID == event) {
		u8 evpd;
		//u8 page_code;			// Needed?
		//u16 allocation_length;	// Needed?
		//u8 control;			// Needed?
		u8 *buf;

		buf = get_cbw_buf(trans);
		evpd = 1 & buf[1];
		//page_code = buf[2];
		//allocation_length = (buf[3] << 8) | buf[4];
		//control = buf[5];

		if (0 == evpd) {
			size_t len;
			trans->bytes_to_write = sizeof(_spc3_inquiry_response);
			memcpy(trans->msd_buf, _spc3_inquiry_response, sizeof(_spc3_inquiry_response));

			len = strlen(ms->vendor_id);
			len = MIN(len, 8);
			memcpy(&trans->msd_buf[8], ms->vendor_id, len);

			len = strlen(ms->product_id);
			len = MIN(len, 16);
			memcpy(&trans->msd_buf[16], ms->product_id, len);

			len = strlen(ms->product_revision_level);
			len = MIN(len, 4);
			memcpy(&trans->msd_buf[32], ms->product_revision_level, len);

			trans->csw.csw.dCSWDataResidue = sizeof(_spc3_inquiry_response);

			set_sbc_status_good(ms);
		} else {
			/* TODO: Add VPD 0x83 support */
			/* TODO: Add VPD 0x00 support */
		}
	}
}

static void scsi_command(usbd_mass_storage *ms,
			 struct usb_mass_trans *trans,
			 enum trans_event event)
	
{
	if (EVENT_CBW_VALID == event) {
		/* Setup the default success */
		trans->csw_sent = 0;
		trans->csw.csw.dCSWSignature = CBW_SIGNATURE;
		trans->csw.csw.dCSWTag = trans->cbw.cbw.dCBWTag;
		trans->csw.csw.dCSWDataResidue = 0;
		trans->csw.csw.bCSWStatus = CBW_STATUS_SUCCESS;

		trans->bytes_to_write = 0;
		trans->bytes_to_read = 0;
		trans->count = 0;
		trans->data = trans->msd_buf;
	}

	switch (trans->cbw.cbw.CBWCB[0]) {
	case SCSI_PREVENT_ALLOW_MEDIUM_REMOVAL:
	case SCSI_TEST_UNIT_READY:
		/* Do nothing, just send the success. */
		set_sbc_status_good(ms);
		break;
	case SCSI_REQUEST_SENSE:
		scsi_request_sense(ms, trans, event);
		break;
	case SCSI_MODE_SENSE_6:
		scsi_mode_sense_6(ms, trans, event);
		break;
	case SCSI_READ_6:
		scsi_read_6(ms, trans, event);
		break;
	case SCSI_INQUIRY:
		scsi_inquiry(ms, trans, event);
		break;
	case SCSI_READ_CAPACITY:
		scsi_read_capacity(ms, trans, event);
		break;
	case SCSI_READ_10:
		scsi_read_10(ms, trans, event);
		break;
	//case SCSI_WRITE_6:
	//case SCSI_SEND_DIAGNOSTIC:
	//case SCSI_REPORT_LUNS:
	default:
		set_sbc_status(ms, SBC_SENSE_KEY_ILLEGAL_REQUEST,
					SBC_ASC_INVALID_COMMAND_OPERATION_CODE,
					SBC_ASCQ_NA);

		trans->bytes_to_write = 0;
		trans->bytes_to_read = 0;
		trans->data = trans->msd_buf;
		trans->csw.csw.bCSWStatus = CBW_STATUS_FAILED;
		break;
	}
}

static void mass_data_rx_cb(usbd_device *usbd_dev, u8 ep)
{
	usbd_mass_storage *ms;
	struct usb_mass_trans *trans;
	int len, max_len, left;
	void *p;

	ms = &_mass_storage;
	trans = &ms->trans;

	/* RX only */
	left = sizeof(struct usb_mass_cbw) - trans->cbw_cnt;
	if (0 < left) {
		max_len = MIN(ms->ep_out_size, left);
		p = &trans->cbw.buf[trans->cbw_cnt];
		len = usbd_ep_read_packet(usbd_dev, ep, p, max_len);
		trans->cbw_cnt += len;

		if (sizeof(struct usb_mass_cbw) == trans->cbw_cnt) {
			scsi_command(ms, trans, EVENT_CBW_VALID);
			if (trans->count < trans->bytes_to_read) {
				/* We must wait until there is something to
				 * read again. */
				return;
			}
		}
	}

	if (trans->count < trans->bytes_to_read) {
		left = trans->bytes_to_read - trans->count;
		max_len = MIN(ms->ep_out_size, left);
		p = &trans->data[trans->count];
		len = usbd_ep_read_packet(usbd_dev, ep, p, max_len);
		trans->count += len;
	} else if (trans->count < trans->bytes_to_write) {
		left = trans->bytes_to_write - trans->count;
		max_len = MIN(ms->ep_out_size, left);
		p = &trans->data[trans->count];
		len = usbd_ep_write_packet(usbd_dev, ms->ep_in, p, max_len);
		trans->count += len;
	} else {
		if (false == trans->csw_valid) {
			scsi_command(ms, trans, EVENT_NEED_STATUS);
			trans->csw_valid = true;
		}

		left = sizeof(struct usb_mass_csw) - trans->csw_sent;
		if (0 < left) {
			max_len = MIN(ms->ep_out_size, left);
			p = &trans->csw.buf[trans->csw_sent];
			if (0xa5 == trans->csw.buf[0]) {
				memset(trans->csw.buf, 0xc7, sizeof(struct usb_mass_csw));
			}
			len = usbd_ep_write_packet(usbd_dev, ms->ep_in, p, max_len);
			trans->csw_sent += len;
		}
	}
}

static void mass_data_tx_cb(usbd_device *usbd_dev, u8 ep)
{
	usbd_mass_storage *ms;
	struct usb_mass_trans *trans;
	int len, max_len, left;
	void *p;

	ms = &_mass_storage;
	trans = &ms->trans;

	if (trans->count < trans->bytes_to_write) {
		left = trans->bytes_to_write - trans->count;
		max_len = MIN(ms->ep_out_size, left);
		p = &trans->data[trans->count];
		len = usbd_ep_write_packet(usbd_dev, ep, p, max_len);
		trans->count += len;
	} else {
		if (false == trans->csw_valid) {
			scsi_command(ms, trans, EVENT_NEED_STATUS);
			trans->csw_valid = true;
		}

		left = sizeof(struct usb_mass_csw) - trans->csw_sent;
		if (0 < left) {
			max_len = MIN(ms->ep_out_size, left);
			p = &trans->csw.buf[trans->csw_sent];
			if (0xa5 == trans->csw.buf[0]) {
				memset(trans->csw.buf, 0xb6, sizeof(struct usb_mass_csw));
			}
			len = usbd_ep_write_packet(usbd_dev, ep, p, max_len);
			trans->csw_sent += len;
		} else if (sizeof(struct usb_mass_csw) == trans->csw_sent) {
			/* End of transaction */
			trans->cbw_cnt = 0;
			trans->bytes_to_read = 0;
			trans->bytes_to_write = 0;
			trans->count = 0;
			trans->csw_sent = 0;
			trans->csw_valid = false;
			trans->data = trans->msd_buf;
			memset(trans->csw.buf, 0xa5, sizeof(struct usb_mass_csw));
		}
	}
}

static void mass_set_config(usbd_device *usbd_dev, u16 wValue)
{
	usbd_mass_storage *ms = &_mass_storage;

	(void)wValue;

	usbd_ep_setup(usbd_dev, ms->ep_in, USB_ENDPOINT_ATTR_BULK,
		      ms->ep_in_size, mass_data_tx_cb);
	usbd_ep_setup(usbd_dev, ms->ep_out, USB_ENDPOINT_ATTR_BULK,
		      ms->ep_out_size, mass_data_rx_cb);

	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				mass_control_request);
}

/** @brief Initializes the USB Mass Storage subsystem.

@note Currently you can only have this profile active.

@param[in] usbd_dev The USB device to associate the Mass Storage with.
@param[in] ep_in The USB 'IN' endpoint.
@param[in] ep_in_size The maximum endpoint size.  Valid values: 8, 16, 32 or 64
@param[in] ep_out The USB 'OUT' endpoint.
@param[in] ep_out_size The maximum endpoint size.  Valid values: 8, 16, 32 or 64

@return Pointer to the usbd_mass_storage struct.
*/
usbd_mass_storage *usb_mass_init(usbd_device *usbd_dev,
				 u8 ep_in, u8 ep_in_size,
				 u8 ep_out, u8 ep_out_size,
				 const char *vendor_id,
				 const char *product_id,
				 const char *product_revision_level)
{
	static u8 buffer[512 * 20];
	int i;
	/* Error Check? */
#if 0
	if (0x80 & ep_out) {
		return NULL;
	}

	if (!(0x80 & ep_in)) {
		return NULL;
	}

	if ((8 != ep_in_size) && (16 != ep_in_size) &&
	    (32 != ep_in_size) && (64 != ep_in_size))
	{
		return NULL;
	}

	if ((8 != ep_out_size) && (16 != ep_out_size) &&
	    (32 != ep_out_size) && (64 != ep_out_size)) {
		return NULL;
	}
#endif
	for( i = 0; i < 20; i++ ) {
		memset(&buffer[512 * i], i, 512);
	}

	_mass_storage.usbd_dev = usbd_dev;
	_mass_storage.ep_in = ep_in;
	_mass_storage.ep_in_size = ep_in_size;
	_mass_storage.ep_out = ep_out;
	_mass_storage.ep_out_size = ep_out_size;
	_mass_storage.vendor_id = vendor_id;
	_mass_storage.product_id = product_id;
	_mass_storage.product_revision_level = product_revision_level;
	_mass_storage.block_count = 20 - 1;
	_mass_storage.lba_0_ptr = buffer;

	_mass_storage.trans.cbw_cnt = 0;
	_mass_storage.trans.bytes_to_read = 0;
	_mass_storage.trans.bytes_to_write = 0;
	_mass_storage.trans.count = 0;
	_mass_storage.trans.csw_valid = false;
	_mass_storage.trans.csw_sent = 0;

	set_sbc_status_good(&_mass_storage);

	usbd_register_set_config_callback(usbd_dev, mass_set_config);

	return &_mass_storage;
}
