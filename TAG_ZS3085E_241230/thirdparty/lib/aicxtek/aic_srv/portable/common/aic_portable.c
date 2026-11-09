/******************************************************************************/
/*                                                                            */
/*    Copyright 2023 by AICXTEK TECHNOLOGIES CO.,LTD. All rights reserved.    */
/*                                                                            */
/******************************************************************************/

#include <aic_init.h>
#include <aic_portable.h>
#include <aic_type.h>
#include <aic_ctrl.h>
#include <init.h>

static int aic_srv_init_entry(const struct device *arg)
{
    aic_init();
    aic_ctrl_poweron();

    printk("aic init sucess!\n");

    return 0;
}
SYS_INIT(aic_srv_init_entry, APPLICATION, 50);
