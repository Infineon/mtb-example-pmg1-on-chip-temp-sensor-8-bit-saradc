/******************************************************************************
* File Name: main.c
*
* Description: This is the source code for the PMG1 On-chip temp sensor 8-bit SAR ADC Example
*              for ModusToolbox.
*
* Related Document: See README.md
*
*******************************************************************************
* Copyright 2022-2024, Cypress Semiconductor Corporation (an Infineon company) or
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
#include <inttypes.h>

/*******************************************************************************
* Macros
*******************************************************************************/
#define CY_ASSERT_FAILED          (0u)
#define SW_DEBOUNCE_DELAY         (25u)
#define TEMP_SLOPE                (-742) /* This value can be adjusted to calibrate the slope of the temperature correlation graph*/
#define TEMP_OFFSET               (493)  /* This value can be adjusted to calibrate the offset of the temperature correlation graph*/

/* Debug print macro to enable UART print */
#define DEBUG_PRINT               (0u)

/*******************************************************************************
* Global Variable
*******************************************************************************/
cy_stc_scb_uart_context_t UART_context;

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

/* Initialize  port 1 for dual port designs or using EVAL_PMG1-S3_DUALDRP kit, else disable */

#if PMG1_PD_DUALPORT_ENABLE

cy_stc_pd_dpm_config_t* get_dpm_port1_connect_stat()
{
    return NULL;    /* This value is not required here, hence NULL is returned */
}

#endif

#if DEBUG_PRINT
/* Variable used for tracking the print status */
volatile bool ENTER_LOOP = true;

/*******************************************************************************
* Function Name: check_status
********************************************************************************
* Summary:
*  Prints the error message.
*
* Parameters:
*  error_msg - message to print if any error encountered.
*  status - status obtained after evaluation.
*
* Return:
*  void
*
*******************************************************************************/
void check_status(char *message, cy_rslt_t status)
{
    char error_msg[50];

    sprintf(error_msg, "Error Code: 0x%08" PRIX32 "\n", status);

    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\nFAIL: ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, message);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, error_msg);
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\r\n=====================================================\r\n");
}
#endif

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
    cy_stc_usbpd_context_t USBPD;              /* USBPD context */
    cy_en_usbpd_status_t usbpd_result;
    cy_en_sysint_status_t intr_result;

    /* Variables to store ADC outputs */
    uint8_t BJT_val;
    uint8_t BANDGAP;

    /* Variables to store temperature data */
    int8_t temp;
    char_t TEMP[30];

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

    /* Configure and enable the UART peripheral */
    Cy_SCB_UART_Init(CYBSP_UART_HW, &CYBSP_UART_config, &UART_context);
    Cy_SCB_UART_Enable(CYBSP_UART_HW);

#if DEBUG_PRINT
    /* Sequence to clear screen */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "\x1b[2J\x1b[;H");

    /* Print "On-chip temp sensor 8-bit SAR ADC" */
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "****************** ");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "PMG1 MCU: On-chip temp sensor 8-bit SAR ADC");
    Cy_SCB_UART_PutString(CYBSP_UART_HW, "****************** \r\n\n");
#endif

    /* Initialize the USBPD Stack */
   #if defined(CY_DEVICE_CCG3)
    usbpd_result = Cy_USBPD_Init(&USBPD, 0, mtb_usbpd_port0_HW, NULL,
                 (cy_stc_usbpd_config_t *)&mtb_usbpd_port0_config, get_dpm_connect_stat);
   #else
    usbpd_result = Cy_USBPD_Init(&USBPD, 0, mtb_usbpd_port0_HW, mtb_usbpd_port0_HW_TRIM,
                 (cy_stc_usbpd_config_t *)&mtb_usbpd_port0_config, get_dpm_connect_stat);
   #if PMG1_PD_DUALPORT_ENABLE
    usbpd_result = Cy_USBPD_Init(&USBPD, 1, mtb_usbpd_port1_HW, mtb_usbpd_port1_HW_TRIM,
                 (cy_stc_usbpd_config_t *)&mtb_usbpd_port1_config, get_dpm_port1_connect_stat);
   #endif /* PMG1_PD_DUALPORT_ENABLE */
   #endif

    if (usbpd_result != CY_USBPD_STAT_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_USBPD_Init failed with error code", usbpd_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Initialize Switch GPIO interrupt */
    intr_result = Cy_SysInt_Init(&User_Switch_intr_config, &User_Switch_Interrupt_Handler);
    if(intr_result != CY_SYSINT_SUCCESS)
    {
#if DEBUG_PRINT
        check_status("API Cy_SysInt_Init failed with error code", intr_result);
#endif
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Clear any pending interrupt and enable the User Switch Interrupt */
    NVIC_ClearPendingIRQ(User_Switch_intr_config.intrSrc);
    NVIC_EnableIRQ(User_Switch_intr_config.intrSrc);

    /* Enable global interrupts */
    __enable_irq();

    /* Send a string over serial terminal */
    Cy_SCB_UART_PutString(CYBSP_UART_HW,"\n Press user switch (SW2) to display the Die-Temperature\r\n");

    for(;;)
    {
        if(SwitchPressFlag)
        {
            /* Wait for 25 milliseconds for switch de-bounce*/
            Cy_SysLib_Delay(SW_DEBOUNCE_DELAY);

            /* Capturing the PIN state for press and release detection */
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

#if DEBUG_PRINT
        if (ENTER_LOOP)
        {
            Cy_SCB_UART_PutString(CYBSP_UART_HW, "Entered for loop\r\n");
            ENTER_LOOP = false;
        }
#endif
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
