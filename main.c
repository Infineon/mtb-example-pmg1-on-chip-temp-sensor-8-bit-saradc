/******************************************************************************
* File Name: main.c
*
* Description: This is the source code for the PMG1 On-chip temp sensor 8-bit SAR ADC Example
*              for ModusToolbox.
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2022, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/


/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "cybsp.h"
#include "cy_pdl.h"
#include "stdio.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define CY_ASSERT_FAILED          (0u)
#define SW_DEBOUNCE_DELAY         (25u)
#define TEMP_SLOPE                (-742) /* This value can be adjusted to calibrate the slope of the temperature correlation graph*/
#define TEMP_OFFSET               (493)  /* This value can be adjusted to calibrate the offset of the temperature correlation graph*/

/* User Switch Interrupt Configuration */
const cy_stc_sysint_t User_Switch_intr_config =
{
    .intrSrc = CYBSP_USER_SW_IRQ,       /* Source of interrupt signal */
    .intrPriority = 3u,                 /* Interrupt priority */
};

/* Flag to detect switch press event */
volatile uint8_t SwitchPressFlag = 0;

void User_Switch_Interrupt_Handler(void);

/* Part of USBPD driver initialization */
cy_stc_pd_dpm_config_t* get_dpm_connect_stat()
{
    return NULL;    /* This value is not required here, hence NULL is returned */
}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  System entrance point. This function performs
*  - initial setup of device
*  - initialize UART_SCB, USBPD driver and external interrupt for Switch
*  - check for switch press, toggle LED, read ADC value and calculate the Die-temperature
*  - send the temperature value through UART_SCB
*
* Parameters:
*  none
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    cy_stc_scb_uart_context_t UART_context;    /* UART context */
    cy_stc_usbpd_context_t USBPD;              /* USBPD context */

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if(result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* To set data field in USBPD context structure to NULL.
     * Required for uninterrupted USBPD Stack initialization */
    memset((void *)&USBPD, 0, sizeof (cy_stc_usbpd_context_t));

    /* Initialize the USBPD Stack */
   #if defined(CY_DEVICE_CCG3)
    Cy_USBPD_Init(&USBPD, 0, mtb_usbpd_port0_HW, NULL,
                 (cy_stc_usbpd_config_t *)&mtb_usbpd_port0_config, get_dpm_connect_stat);
   #else
    Cy_USBPD_Init(&USBPD, 0, mtb_usbpd_port0_HW, mtb_usbpd_port0_HW_TRIM,
                 (cy_stc_usbpd_config_t *)&mtb_usbpd_port0_config, get_dpm_connect_stat);
   #if PMG1_PD_DUALPORT_ENABLE
    Cy_USBPD_Init(&USBPD, 1, mtb_usbpd_port1_HW, mtb_usbpd_port1_HW_TRIM,
                 (cy_stc_usbpd_config_t *)&mtb_usbpd_port1_config, get_dpm_port1_connect_stat);
   #endif /* PMG1_PD_DUALPORT_ENABLE */
   #endif

    /* Configure and enable the UART peripheral */
    Cy_SCB_UART_Init(CYBSP_UART_HW, &CYBSP_UART_config, &UART_context);
    Cy_SCB_UART_Enable(CYBSP_UART_HW);

    /* Initialize Switch GPIO interrupt */
    result = Cy_SysInt_Init(&User_Switch_intr_config, &User_Switch_Interrupt_Handler);
    if(result != CY_SYSINT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Clear any pending interrupt and enable the User Switch Interrupt */
    NVIC_ClearPendingIRQ(User_Switch_intr_config.intrSrc);
    NVIC_EnableIRQ(User_Switch_intr_config.intrSrc);

    /* Enable global interrupts */
    __enable_irq();

    /* Variables to store ADC outputs */
    uint8_t BJT_val;
    uint8_t BANDGAP;

    /* Variables to store temperature data */
    int8_t temp;
    char_t TEMP[30];

    /* Send a string over serial terminal */
    Cy_SCB_UART_PutString(CYBSP_UART_HW,"\n\nPress user switch (SW2) to display the Die-Temperature\r\n");

    for(;;)
    {
        if(SwitchPressFlag)
        {
            /* Wait for 25 milliseconds for switch de-bounce*/
            Cy_SysLib_Delay(SW_DEBOUNCE_DELAY);

            if(!Cy_GPIO_Read(CYBSP_USER_SW_PORT, CYBSP_USER_SW_PIN))
            {
                BJT_val = Cy_USBPD_Adc_Sample(&USBPD, CY_USBPD_ADC_ID_0, CY_USBPD_ADC_INPUT_BJT);
                BANDGAP = Cy_USBPD_Adc_Sample(&USBPD, CY_USBPD_ADC_ID_0, CY_USBPD_ADC_INPUT_BANDGAP);

                /* Calibration formula: Slope:-742, Offset: 493 (from characterization data) */
                temp = (((TEMP_SLOPE) * ((BJT_val << 16) / BANDGAP)) >> 16) + TEMP_OFFSET;

                /* Conversion from uint32_t to char_t for UART transmit */
                sprintf(TEMP, "Die-Temperature = %d C\r\n", temp);

                /* Send a string over serial terminal */
                Cy_SCB_UART_PutString(CYBSP_UART_HW, TEMP);

                /* Toggle the user LED state */
                Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);
            }
            /* Clear the Switch Press Event */
            SwitchPressFlag = 0;
        }
    }

} 

/*******************************************************************************
* Function Name: User_Switch_Interrupt_Handler
********************************************************************************
*
* Summary:
* This function is executed when interrupt is triggered through the user switch press.
*
*******************************************************************************/
void User_Switch_Interrupt_Handler(void)
{
    /* Set Switch press flag to 1 */
    SwitchPressFlag = 1;

    /* Clear the Interrupt */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_SW_PORT, CYBSP_USER_SW_NUM);
}

/* [] END OF FILE */
