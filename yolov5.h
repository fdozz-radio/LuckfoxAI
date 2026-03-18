// Copyright (c) 2023 by Rockchip Electronics Co., Ltd. All Rights Reserved.
#ifndef __YOLOV5_H__
#define __YOLOV5_H__

#include "rknn_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBJ_CLASS_NUM 80
#define NMS_THRESH 0.45f
#define BOX_THRESH 0.25f
#define PROP_BOX_SIZE (5 + OBJ_CLASS_NUM)

// RV1106/RV1103 специфичный флаг
#define RV1106_1103

typedef struct {
    int x;
    int y;
    int w;
    int h;
} BoxIntRect;

typedef struct {
    int cls_id;
    float prop;
    BoxIntRect box;
} object_detect_result;

typedef struct {
    int count;
    object_detect_result results[64];
} object_detect_result_list;

typedef struct {
    rknn_context rknn_ctx;
    rknn_tensor_mem *input_mems[1];
    rknn_tensor_mem *output_mems[3];
    rknn_tensor_attr *input_attrs;
    rknn_tensor_attr *output_attrs;
    rknn_input_output_num io_num;
    bool is_quant;
    int model_channel;
    int model_width;
    int model_height;
} rknn_app_context_t;

int init_yolov5_model(const char* model_path, rknn_app_context_t* app_ctx);
int release_yolov5_model(rknn_app_context_t* app_ctx);
int inference_yolov5_model(rknn_app_context_t* app_ctx, object_detect_result_list* od_results);
int init_post_process();
void deinit_post_process();
const char* coco_cls_to_name(int cls_id);
int post_process(rknn_app_context_t* app_ctx, void* outputs, float conf_threshold, 
                 float nms_threshold, object_detect_result_list* od_results);

#ifdef __cplusplus
}
#endif

#endif // __YOLOV5_H__
