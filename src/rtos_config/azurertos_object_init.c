/*
* Copyright (c) 2016 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

/***********************************************************************************************************************
 * File Name    : azure_object_init.c
 * Description  : define Objects initialization  
 **********************************************************************************************************************/
/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "azurertos_object_init.h"
/* Start user code for user includes. Do not edit comment generated here */
/* End user code. Do not edit comment generated here */
/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private global variables and functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * ThreadX object control blocks definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Stacks definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Function Name: tx_application_define_user
 * Description  : This function initializes Azure RTOS objects.
 * Arguments    : None.
 * Return Value : None.
 * Note         : If this function is not called anywhere yet, please call it manually in application code
                  (for e.g. call it in tx_application_define())
 **********************************************************************************************************************/
void tx_application_define_user (void)
{
    /************** thread creation *************************/

    /************** queue creation **************************/

    /************** Semaphore creation **********************/

    /************** Mutex creation **************************/

    /************** Event flags creation ********************/

    /************** Application timer creation **************/
    /* Start user code for user initialization code. Do not edit comment generated here */
    /* End user code. Do not edit comment generated here */
} /* End of function tx_application_define_user()*/

/* Start user code for others user code. Do not edit comment generated here */
/* End user code. Do not edit comment generated here */
