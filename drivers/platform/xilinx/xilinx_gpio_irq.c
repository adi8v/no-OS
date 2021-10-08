/***************************************************************************//**
 *   @file   xilinx/xilinx_gpio_irq.c
 *   @brief  Implementation of Xilinx GPIO IRQ Generic Driver.
 *   @author Porumb Andrei (andrei.porumb@analog.com)
********************************************************************************
 * Copyright 2021(c) Analog Devices, Inc.
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

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include <stdlib.h>
#include "xparameters.h"
#include "error.h"
#include "gpio_irq_extra.h"
#include "xgpiops.h"
#include "util.h"
#include "irq.h"

/******************************************************************************/
/************************ Functions Definitions *******************************/
/******************************************************************************/
/**
 * @brief Function called when GPIO IRQ occurs.
 * @param param - GPIO IRQ desc's extra field
 * @return - None.
 */
static void irq_handler(struct xil_gpio_irq_desc *param)
{
	int32_t i;
	struct callback_desc *callback_desc;

	callback_desc = param->callback;

	for(i = 0; i < ARRAY_SIZE(param->pin_nb); i++) {
		if(param->pin_nb[i] != 0xFFFFU
		    && XGpioPs_IntrGetStatusPin(param->my_Gpio, param->pin_nb[i])) {
			XGpioPs_IntrDisablePin(param->my_Gpio, param->pin_nb[i]);
			XGpioPs_IntrClearPin(param->my_Gpio, param->pin_nb[i]);
			break;
		}
	}

	callback_desc->callback(callback_desc->ctx, 0U, NULL);

	XGpioPs_IntrEnablePin(param->my_Gpio, param->pin_nb[i]);
}

/**
 * @brief Disable specific GPIO IRQ pin.
 * @param desc - The GPIO IRQ controller descriptor.
 * @param irq_id - Pin number.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t xil_gpio_irq_disable(struct irq_ctrl_desc *desc, uint32_t irq_id)
{
	int32_t i;
	struct xil_gpio_irq_desc *extra;
	extra = desc->extra;

	XGpioPs_IntrDisablePin(extra->my_Gpio, irq_id);

	for(i = 0; i < ARRAY_SIZE(extra->pin_nb); i++) {
		if(extra->pin_nb[i] == irq_id) {
			extra->pin_nb[i] = 0xFFFFU;
			break;
		}
	}

	return SUCCESS;
}

/**
 * @brief Initialize the GPIO IRQ controller.
 * @param desc - The GPIO IRQ controller descriptor.
 * @param param - The GPIO IRQ controller initial parameters.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t xil_gpio_irq_ctrl_init(struct irq_ctrl_desc **desc,
			       const struct irq_init_param *param)
{
	int32_t i;
	int32_t status;
	struct irq_ctrl_desc *descriptor;
	XGpioPs *my_Gpio;
	static XGpioPs_Config *GPIO_Config;
	struct xil_gpio_irq_desc *xil_dev;
	struct callback_desc callback;

	xil_dev = (struct xil_gpio_irq_desc *)calloc(1, sizeof(*xil_dev));
	if(!xil_dev)
		return FAILURE;

	my_Gpio = (XGpioPs *)calloc(1, sizeof(*my_Gpio));
	if(!my_Gpio) {
		free(xil_dev);
		return FAILURE;
	}

	descriptor = (struct irq_ctrl_desc *)calloc(1, sizeof(*descriptor));
	if(!descriptor)
		goto error;

	GPIO_Config = XGpioPs_LookupConfig(((struct xil_gpio_irq_init_param *)
					    param->extra)->gpio_device_id);
	status = XGpioPs_CfgInitialize(my_Gpio, GPIO_Config,
				       GPIO_Config->BaseAddr);
	if(status)
		goto error;

	descriptor->extra = xil_dev;
	xil_dev->parent_desc = ((struct xil_gpio_irq_init_param *)
				param->extra)->parent_desc;
	xil_dev->callback = ((struct xil_gpio_irq_init_param *)param->extra)->callback;
	xil_dev->my_Gpio = my_Gpio;

	descriptor->irq_ctrl_id = param->irq_ctrl_id;
	status = irq_trigger_level_set(xil_dev->parent_desc, descriptor->irq_ctrl_id,
				       IRQ_EDGE_RISING);
	if(status)
		goto error;
	status = irq_enable(xil_dev->parent_desc, descriptor->irq_ctrl_id);
	if(status)
		goto error;

	callback.callback = &irq_handler;
	callback.ctx = descriptor->extra;
	status = irq_register_callback(xil_dev->parent_desc, descriptor->irq_ctrl_id,
				       &callback);
	if(status)
		goto error;

	for(i = 0; i < ARRAY_SIZE(xil_dev->pin_nb); i++) {
		xil_dev->pin_nb[i] = 0xFFFFU; //dummy
	}

	*desc = descriptor;

	return SUCCESS;

error:
	free(my_Gpio);
	free(xil_dev);
	free(descriptor);
	return ENOMEM;
}

/**
 * @brief Set GPIO interrupt trigger level.
 * @param desc - The GPIO IRQ controller descriptor.
 * @param irq_id - Pin number.
 * @param trig - New trigger level for the GPIO interrupt.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t xil_gpio_irq_trigger_level_set(struct irq_ctrl_desc *desc,
				       uint32_t irq_id,
				       enum irq_trig_level trig)
{
	int32_t trig_level[5] = {4U, 3U, 1U, 0U, 2U};
	struct xil_gpio_irq_desc *extra;
	extra = desc->extra;

	XGpioPs_SetIntrTypePin(extra->my_Gpio, irq_id, trig_level[trig]);

	return SUCCESS;
}

/**
 * @brief Enable specific GPIO IRQ pin.
 * @param desc - The GPIO IRQ controller descriptor.
 * @param irq_id - Pin number.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t xil_gpio_irq_enable(struct irq_ctrl_desc *desc, uint32_t irq_id)
{
	int32_t i;
	struct xil_gpio_irq_desc *extra;
	extra = desc->extra;

	XGpioPs_SetDirectionPin(extra->my_Gpio, irq_id, 0);
	XGpioPs_IntrEnablePin(extra->my_Gpio, irq_id);

	for(i = 0; i < ARRAY_SIZE(extra->pin_nb); i++) {
		if(extra->pin_nb[i] == 0xFFFFU) {
			extra->pin_nb[i] = irq_id;
			break;
		}
	}

	return SUCCESS;
}

/**
 * @brief Remove the GPIO IRQ controller.
 * @param desc - The GPIO IRQ controller descriptor.
 * @return SUCCESS in case of success, FAILURE otherwise.
 */
int32_t xil_gpio_irq_ctrl_remove(struct irq_ctrl_desc *desc)
{
	struct xil_gpio_irq_desc *extra;

	if (!desc)
		return -EINVAL;
	extra = desc->extra;

	free(extra->my_Gpio);
	free(extra);
	free(desc);

	return SUCCESS;
}

/**
 * @brief Xilinx platform specific GPIO IRQ platform ops structure
 */
const struct irq_platform_ops xil_gpio_irq_ops = {
	.init = &xil_gpio_irq_ctrl_init,
	.enable = &xil_gpio_irq_enable,
	.disable = &xil_gpio_irq_disable,
	.trigger_level_set = &xil_gpio_irq_trigger_level_set,
	.remove = &xil_gpio_irq_ctrl_remove
};
