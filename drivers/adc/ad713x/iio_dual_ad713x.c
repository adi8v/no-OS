/***************************************************************************//**
 *   @file   iio_ad713x.c
 *   @brief  Implementation of iio_ad713x.c.
 *   @author Cristian Pop (cristian.pop@analog.com)
********************************************************************************
 * Copyright 2020(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifdef IIO_SUPPORT

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "no-os/error.h"
#include "no-os/util.h"
#include "iio_types.h"
#include "spi_engine.h"
#include "iio_dual_ad713x.h"
#include "no-os/delay.h"

/******************************************************************************/
/*************************** Types Declarations *******************************/
/******************************************************************************/

#define BITS_PER_SAMPLE 32

static struct scan_type adc_scan_type = {
	.sign = 'u',
	.realbits = BITS_PER_SAMPLE,
	.storagebits = BITS_PER_SAMPLE,
	.shift = 0,
	.is_big_endian = false
};

#define IIO_AD713X_CHANNEL(_idx) {\
	.name = "ch" # _idx,\
	.ch_type = IIO_VOLTAGE,\
	.channel = _idx,\
	.scan_index = _idx,\
	.indexed = true,\
	.scan_type = &adc_scan_type,\
	.ch_out = false,\
}

static struct iio_channel iio_adc_channels[] = {
	IIO_AD713X_CHANNEL(0),
	IIO_AD713X_CHANNEL(1),
	IIO_AD713X_CHANNEL(2),
	IIO_AD713X_CHANNEL(3),
	IIO_AD713X_CHANNEL(4),
	IIO_AD713X_CHANNEL(5),
	IIO_AD713X_CHANNEL(6),
	IIO_AD713X_CHANNEL(7),
	IIO_AD713X_CHANNEL(8)
};

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/

static int32_t _iio_ad713x_prepare_transfer(struct iio_ad713x *desc,
		uint32_t mask)
{
	if (!desc)
		return -EINVAL;

	desc->mask = mask;

	return SUCCESS;
}

static int32_t _iio_ad713x_read_dev(struct iio_ad713x *desc, uint32_t *buff,
				    uint32_t nb_samples)
{
	struct spi_engine_offload_message *msg;
	uint32_t bytes;
	uint32_t data;
	int32_t  ret;
	uint8_t  ch;
	uint32_t i;
	uint32_t j;
	uint32_t *rx;

	if (!desc)
		return FAILURE;

	bytes = nb_samples * desc->iio_dev_desc.num_ch *
		(BITS_PER_SAMPLE / 8);
	msg = desc->spi_engine_offload_message;
	ret = spi_engine_offload_transfer(desc->spi_eng_desc, *msg, bytes);
	if (ret < 0)
		return ret;

	if (desc->dcache_invalidate_range)
		desc->dcache_invalidate_range(msg->rx_addr, bytes);

	rx = (uint32_t *)desc->spi_engine_offload_message->rx_addr;
	for (i = 0, j = 0; i < nb_samples; i++)
		for (ch = 0; ch < desc->iio_dev_desc.num_ch; ch++)
			if (desc->mask & BIT(ch)) {
				data = rx[i * desc->iio_dev_desc.num_ch + ch];
				data <<= 1;
				data &= 0xffffff00;
				data >>= 8;
				buff[j++] = data;
			}

	return nb_samples;
}

/**
 * @brief Get iio device descriptor.
 * @param desc - Descriptor.
 * @param dev_descriptor - iio device descriptor.
 */
void iio_dual_ad713x_get_dev_descriptor(struct iio_ad713x *desc,
					struct iio_device **dev_descriptor)
{
	*dev_descriptor = &desc->iio_dev_desc;
}

/**
 * @brief Init for reading/writing and parameterization of a
 * iio_ad713x device.
 * @param desc - Descriptor.
 * @param param - Configuration structure.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t iio_dual_ad713x_init(struct iio_ad713x **desc,
			     struct iio_ad713x_init_par *param)
{
	struct iio_ad713x *iio_ad713x;

	iio_ad713x = (struct iio_ad713x *)calloc(1, sizeof(struct iio_ad713x));
	if (!iio_ad713x)
		return FAILURE;

	iio_ad713x->spi_eng_desc = param->spi_eng_desc;
	iio_ad713x->spi_engine_offload_message = param->spi_engine_offload_message;
	iio_ad713x->dcache_invalidate_range = param->dcache_invalidate_range;

	iio_ad713x->iio_dev_desc = (struct iio_device) {
		.num_ch = param->num_channels,
		.channels = iio_adc_channels,
		.pre_enable = (int32_t (*)())_iio_ad713x_prepare_transfer,
		.read_dev = (int32_t (*)())_iio_ad713x_read_dev
	};

	*desc = iio_ad713x;

	return SUCCESS;
}

/**
 * @brief Release resources.
 * @param desc - Descriptor.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t iio_dual_ad713x_remove(struct iio_ad713x *desc)
{
	if (!desc)
		return FAILURE;

	free(desc);

	return SUCCESS;
}

#endif
