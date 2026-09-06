/**
  ******************************************************************************
  * @file    kws_dscnn_data_params.h
  * @author  AST Embedded Analytics Research Platform
  * @date    2026-09-07T06:06:52+0800
  * @brief   AI Tool Automatic Code Generator for Embedded NN computing
  ******************************************************************************
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */

#ifndef KWS_DSCNN_DATA_PARAMS_H
#define KWS_DSCNN_DATA_PARAMS_H

#include "ai_platform.h"

/*
#define AI_KWS_DSCNN_DATA_WEIGHTS_PARAMS \
  (AI_HANDLE_PTR(&ai_kws_dscnn_data_weights_params[1]))
*/

#define AI_KWS_DSCNN_DATA_CONFIG               (NULL)


#define AI_KWS_DSCNN_DATA_ACTIVATIONS_SIZES \
  { 72292, }
#define AI_KWS_DSCNN_DATA_ACTIVATIONS_SIZE     (72292)
#define AI_KWS_DSCNN_DATA_ACTIVATIONS_COUNT    (1)
#define AI_KWS_DSCNN_DATA_ACTIVATION_1_SIZE    (72292)



#define AI_KWS_DSCNN_DATA_WEIGHTS_SIZES \
  { 41992, }
#define AI_KWS_DSCNN_DATA_WEIGHTS_SIZE         (41992)
#define AI_KWS_DSCNN_DATA_WEIGHTS_COUNT        (1)
#define AI_KWS_DSCNN_DATA_WEIGHT_1_SIZE        (41992)



#define AI_KWS_DSCNN_DATA_ACTIVATIONS_TABLE_GET() \
  (&g_kws_dscnn_activations_table[1])

extern ai_handle g_kws_dscnn_activations_table[1 + 2];



#define AI_KWS_DSCNN_DATA_WEIGHTS_TABLE_GET() \
  (&g_kws_dscnn_weights_table[1])

extern ai_handle g_kws_dscnn_weights_table[1 + 2];


#endif    /* KWS_DSCNN_DATA_PARAMS_H */
