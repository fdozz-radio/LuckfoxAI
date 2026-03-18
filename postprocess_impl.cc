// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
// Реализация функций постобработки для YOLOv5 на LuckFox Pico (RV1106)

#include "yolov5.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <set>
#include <vector>

#define LABEL_NALE_TXT_PATH "./model/coco_80_labels_list.txt"

static const char* coco_classes[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake",
    "chair", "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop",
    "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

const int anchor[3][6] = {{10, 13, 16, 30, 33, 23},
                          {30, 61, 62, 45, 59, 119},
                          {116, 90, 156, 198, 373, 326}};

inline static int clamp(float val, int min, int max) { 
    return val > min ? (val < max ? val : max) : min; 
}

inline static int32_t __clip(float val, float min, float max) {
    float f = val <= min ? min : (val >= max ? max : val);
    return f;
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale) {
    float dst_val = (f32 / scale) + zp;
    int8_t res = (int8_t)__clip(dst_val, -128, 127);
    return res;
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale) { 
    return ((float)qnt - (float)zp) * scale; 
}

static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0, 
                              float xmin1, float ymin1, float xmax1, float ymax1) {
    float w = fmaxf(0.f, fminf(xmax0, xmax1) - fmaxf(xmin0, xmin1) + 1.0f);
    float h = fmaxf(0.f, fminf(ymax0, ymax1) - fmaxf(ymin0, ymin1) + 1.0f);
    float i = w * h;
    float u = (xmax0 - xmin0 + 1.0f) * (ymax0 - ymin0 + 1.0f) + 
              (xmax1 - xmin1 + 1.0f) * (ymax1 - ymin1 + 1.0f) - i;
    return u <= 0.f ? 0.f : (i / u);
}

static int quick_sort_indice_inverse(std::vector<float> &input, int left, int right, 
                                      std::vector<int> &indices) {
    float key;
    int key_index;
    int low = left;
    int high = right;
    if (left < right) {
        key_index = indices[left];
        key = input[left];
        while (low < high) {
            while (low < high && input[high] <= key) {
                high--;
            }
            input[low] = input[high];
            indices[low] = indices[high];
            while (low < high && input[low] >= key) {
                low++;
            }
            input[high] = input[low];
            indices[high] = indices[low];
        }
        input[low] = key;
        indices[low] = key_index;
        quick_sort_indice_inverse(input, left, low - 1, indices);
        quick_sort_indice_inverse(input, low + 1, right, indices);
    }
    return low;
}

static int nms(int validCount, std::vector<float> &outputLocations, 
               std::vector<int> &classIds, std::vector<int> &order,
               int filterId, float threshold) {
    for (int i = 0; i < validCount; ++i) {
        if (order[i] == -1 || classIds[i] != filterId) {
            continue;
        }
        int n = order[i];
        for (int j = i + 1; j < validCount; ++j) {
            int m = order[j];
            if (m == -1 || classIds[j] != filterId) {
                continue;
            }
            float xmin0 = outputLocations[n * 4 + 0];
            float ymin0 = outputLocations[n * 4 + 1];
            float xmax0 = outputLocations[n * 4 + 0] + outputLocations[n * 4 + 2];
            float ymax0 = outputLocations[n * 4 + 1] + outputLocations[n * 4 + 3];

            float xmin1 = outputLocations[m * 4 + 0];
            float ymin1 = outputLocations[m * 4 + 1];
            float xmax1 = outputLocations[m * 4 + 0] + outputLocations[m * 4 + 2];
            float ymax1 = outputLocations[m * 4 + 1] + outputLocations[m * 4 + 3];

            float iou = CalculateOverlap(xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);

            if (iou > threshold) {
                order[j] = -1;
            }
        }
    }
    return 0;
}

// Обработка INT8 выхода для RV1106/RV1103
static int process_i8_rv1106(int8_t *input, int *anchor, int grid_h, int grid_w, 
                             int height, int width, int stride,
                             std::vector<float> &boxes, std::vector<float> &boxScores, 
                             std::vector<int> &classId, float threshold,
                             int32_t zp, float scale) {
    int validCount = 0;
    int8_t thres_i8 = qnt_f32_to_affine(threshold, zp, scale);

    int anchor_per_branch = 3;
    int align_c = PROP_BOX_SIZE * anchor_per_branch;

    for (int h = 0; h < grid_h; h++) {
        for (int w = 0; w < grid_w; w++) {
            for (int a = 0; a < anchor_per_branch; a++) {
                int hw_offset = h * grid_w * align_c + w * align_c + a * PROP_BOX_SIZE;
                int8_t *hw_ptr = input + hw_offset;
                int8_t box_confidence = hw_ptr[4];

                if (box_confidence >= thres_i8) {
                    int8_t maxClassProbs = hw_ptr[5];
                    int maxClassId = 0;
                    for (int k = 1; k < OBJ_CLASS_NUM; ++k) {
                        int8_t prob = hw_ptr[5 + k];
                        if (prob > maxClassProbs) {
                            maxClassId = k;
                            maxClassProbs = prob;
                        }
                    }

                    float box_conf_f32 = deqnt_affine_to_f32(box_confidence, zp, scale);
                    float class_prob_f32 = deqnt_affine_to_f32(maxClassProbs, zp, scale);
                    float limit_score = box_conf_f32 * class_prob_f32;

                    if (limit_score > threshold) {
                        float box_x, box_y, box_w, box_h;

                        box_x = deqnt_affine_to_f32(hw_ptr[0], zp, scale) * 2.0f - 0.5f;
                        box_y = deqnt_affine_to_f32(hw_ptr[1], zp, scale) * 2.0f - 0.5f;
                        box_w = deqnt_affine_to_f32(hw_ptr[2], zp, scale) * 2.0f;
                        box_h = deqnt_affine_to_f32(hw_ptr[3], zp, scale) * 2.0f;
                        box_w = box_w * box_w;
                        box_h = box_h * box_h;

                        box_x = (box_x + w) * (float)stride;
                        box_y = (box_y + h) * (float)stride;
                        box_w *= (float)anchor[a * 2];
                        box_h *= (float)anchor[a * 2 + 1];

                        box_x -= (box_w / 2.0f);
                        box_y -= (box_h / 2.0f);

                        boxes.push_back(box_x);
                        boxes.push_back(box_y);
                        boxes.push_back(box_w);
                        boxes.push_back(box_h);
                        boxScores.push_back(limit_score);
                        classId.push_back(maxClassId);
                        validCount++;
                    }
                }
            }
        }
    }
    return validCount;
}

int init_post_process() {
    // Для LuckFox Pico не требуется дополнительная инициализация
    return 0;
}

void deinit_post_process() {
    // Не требуется очистка
}

const char* coco_cls_to_name(int cls_id) {
    if (cls_id >= 0 && cls_id < 80) {
        return coco_classes[cls_id];
    }
    return "unknown";
}

int post_process(rknn_app_context_t *app_ctx, void *outputs, float conf_threshold, 
                 float nms_threshold, object_detect_result_list *od_results) {
#if defined(RV1106_1103) 
    rknn_tensor_mem **_outputs = (rknn_tensor_mem **)outputs;
#else
    rknn_output *_outputs = (rknn_output *)outputs;
#endif
    
    std::vector<float> filterBoxes;
    std::vector<float> objProbs;
    std::vector<int> classId;
    int validCount = 0;
    int stride = 0;
    int grid_h = 0;
    int grid_w = 0;
    int model_in_w = app_ctx->model_width;
    int model_in_h = app_ctx->model_height;

    memset(od_results, 0, sizeof(object_detect_result_list));

    // Обработка 3 выходов YOLOv5 head
    for (int i = 0; i < 3; i++) {
#if defined(RV1106_1103) 
        grid_h = app_ctx->output_attrs[i].dims[2];
        grid_w = app_ctx->output_attrs[i].dims[1];
        stride = model_in_h / grid_h;
        
        if (app_ctx->is_quant) {
            validCount += process_i8_rv1106((int8_t *)(_outputs[i]->virt_addr), (int *)anchor[i], 
                                           grid_h, grid_w, model_in_h, model_in_w, stride, 
                                           filterBoxes, objProbs, classId, conf_threshold, 
                                           app_ctx->output_attrs[i].zp, app_ctx->output_attrs[i].scale);
        }
#else     
        grid_h = app_ctx->output_attrs[i].dims[2];
        grid_w = app_ctx->output_attrs[i].dims[3];
        stride = model_in_h / grid_h;
        if (app_ctx->is_quant) {
            // Для других платформ
        }
#endif
    }

    if (validCount == 0) {
        return 0;
    }

    // NMS
    std::vector<int> order(validCount);
    for (int i = 0; i < validCount; i++) {
        order[i] = i;
    }
    
    for (int class_id = 0; class_id < OBJ_CLASS_NUM; class_id++) {
        nms(validCount, filterBoxes, classId, order, class_id, nms_threshold);
    }

    // Сбор результатов
    int count = 0;
    for (int i = 0; i < validCount; i++) {
        if (order[i] == -1) continue;
        if (count >= 64) break;

        od_results->results[count].cls_id = classId[order[i]];
        od_results->results[count].prop = objProbs[order[i]];
        od_results->results[count].box.x = (int)filterBoxes[order[i] * 4 + 0];
        od_results->results[count].box.y = (int)filterBoxes[order[i] * 4 + 1];
        od_results->results[count].box.w = (int)filterBoxes[order[i] * 4 + 2];
        od_results->results[count].box.h = (int)filterBoxes[order[i] * 4 + 3];
        count++;
    }

    od_results->count = count;
    return 0;
}
