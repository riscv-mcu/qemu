// ================================================================
// NVDLA Open Source Project
// 
// Copyright(c) 2016 - 2017 NVIDIA Corporation.  Licensed under the
// NVDLA Open Hardware License; Check "LICENSE" which comes with 
// this distribution for more information.
// ================================================================

// File Name: qbox_riscv.c

#include <tlm2c/tlm2c.h>
#include "qbox/qboxbase.h"

struct QBOXRISCV
{
  QBOXBase *base;
};
typedef struct QBOXRISCV QBOXRISCV;

QBOXRISCV riscv_handle;

static void qbox_riscv_init(void)
{
    qbox_add_riscv_arguments(riscv_handle.base);
    qbox_add_extra_arguments(riscv_handle.base);
}

Model *tlm2c_elaboration(Environment *environment)
{
  /* Entry point called by the bridge. */
  tlm2c_set_environment(environment);

  riscv_handle.base = qbox_base_init();

  qbox_riscv_init();

  return (Model *)(qbox_get_handle());
}

