/*
 * LuckFox Pico Mini + SC3336 Camera + YOLOv5 RKNN + RTSP Stream
 * Полностью переработанное решение с корректной обработкой буферов
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "rtsp_demo.h"
#include "luckfox_mpi.h"
#include "yolov5.h"

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"

// Разрешение выходного потока
#define STREAM_WIDTH  640
#define STREAM_HEIGHT 480

// Разрешение модели YOLOv5
#define MODEL_WIDTH  640
#define MODEL_HEIGHT 640

// Глобальные переменные для масштабирования
static float g_scale_x = 1.0f;
static float g_scale_y = 1.0f;
static int g_pad_left = 0;
static int g_pad_top = 0;

// Преобразование координат из модели в оригинальное изображение
static void convert_coords(int* x1, int* y1, int* x2, int* y2) {
    // Учитываем letterbox padding
    float adj_x1 = (*x1 - g_pad_left) / g_scale_x;
    float adj_y1 = (*y1 - g_pad_top) / g_scale_y;
    float adj_x2 = (*x2 - g_pad_left) / g_scale_x;
    float adj_y2 = (*y2 - g_pad_top) / g_scale_y;
    
    *x1 = (int)adj_x1;
    *y1 = (int)adj_y1;
    *x2 = (int)adj_x2;
    *y2 = (int)adj_y2;
}

// Letterbox преобразование для YOLO
static cv::Mat letterbox(const cv::Mat& src) {
    int src_w = src.cols;
    int src_h = src.rows;
    
    // Вычисляем масштаб
    g_scale_x = (float)MODEL_WIDTH / src_w;
    g_scale_y = (float)MODEL_HEIGHT / src_h;
    float scale = (g_scale_x < g_scale_y) ? g_scale_x : g_scale_y;
    
    int new_w = (int)(src_w * scale);
    int new_h = (int)(src_h * scale);
    
    g_pad_left = (MODEL_WIDTH - new_w) / 2;
    g_pad_top = (MODEL_HEIGHT - new_h) / 2;
    
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
    
    cv::Mat result(MODEL_HEIGHT, MODEL_WIDTH, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(result(cv::Rect(g_pad_left, g_pad_top, new_w, new_h)));
    
    return result;
}

// Обработка сигналов для корректного завершения
static volatile bool g_running = true;

static void signal_handler(int sig) {
    printf("\nReceived signal %d, stopping...\n", sig);
    g_running = false;
}

int main(int argc, char *argv[]) {
    RK_S32 s32Ret = 0;
    int ret;
    
    printf("=== LuckFox Pico Mini YOLOv5 RTSP Server ===\n");
    printf("Camera: SC3336\n");
    printf("Stream: %dx%d\n", STREAM_WIDTH, STREAM_HEIGHT);
    printf("Model: YOLOv5 RKNN\n");
    printf("RTSP: rtsp://<device-ip>/live/0\n\n");
    
    // Установка обработчика сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Остановка стандартных сервисов (если есть)
    system("killall rga_demo 2>/dev/null || true");
    usleep(50000);
    
    // =====================================================
    // 1. Инициализация RKNN модели YOLOv5
    // =====================================================
    const char* model_path = "./model/yolov5.rknn";
    rknn_app_context_t rknn_ctx;
    memset(&rknn_ctx, 0, sizeof(rknn_app_context_t));
    
    printf("[1/6] Loading YOLOv5 model: %s\n", model_path);
    ret = init_yolov5_model(model_path, &rknn_ctx);
    if (ret != 0) {
        printf("ERROR: Failed to load YOLOv5 model!\n");
        return -1;
    }
    printf("      Model loaded successfully\n");
    
    // =====================================================
    // 2. Инициализация системы MPI
    // =====================================================
    printf("[2/6] Initializing RK MPI system...\n");
    ret = RK_MPI_SYS_Init();
    if (ret != RK_SUCCESS) {
        printf("ERROR: RK_MPI_SYS_Init failed! ret=%x\n", ret);
        return -1;
    }
    printf("      MPI system initialized\n");
    
    // =====================================================
    // 3. Инициализация ISP (камера)
    // =====================================================
    printf("[3/6] Initializing ISP for SC3336 camera...\n");
    
    RK_BOOL multi_sensor = RK_FALSE;
    const char* iq_dir = "/etc/iqfiles";
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    
    ret = SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
    if (ret != RK_SUCCESS) {
        printf("ERROR: SAMPLE_COMM_ISP_Init failed! ret=%x\n", ret);
        RK_MPI_SYS_Exit();
        return -1;
    }
    
    ret = SAMPLE_COMM_ISP_Run(0);
    if (ret != RK_SUCCESS) {
        printf("ERROR: SAMPLE_COMM_ISP_Run failed! ret=%x\n", ret);
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        return -1;
    }
    printf("      ISP initialized and running\n");
    
    // =====================================================
    // 4. Инициализация VI (Video Input)
    // =====================================================
    printf("[4/6] Initializing VI channel...\n");
    
    ret = vi_dev_init();
    if (ret != 0) {
        printf("ERROR: vi_dev_init failed!\n");
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        return -1;
    }
    
    ret = vi_chn_init(0, STREAM_WIDTH, STREAM_HEIGHT);
    if (ret != 0) {
        printf("ERROR: vi_chn_init failed!\n");
        RK_MPI_VI_DisableDev(0);
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        return -1;
    }
    printf("      VI channel initialized (%dx%d)\n", STREAM_WIDTH, STREAM_HEIGHT);
    
    // =====================================================
    // 5. Инициализация VENC (Video Encoder - H264)
    // =====================================================
    printf("[5/6] Initializing VENC (H.264)...\n");
    
    RK_CODEC_ID_E codec_type = RK_VIDEO_ID_AVC;
    ret = venc_init(0, STREAM_WIDTH, STREAM_HEIGHT, codec_type);
    if (ret != 0) {
        printf("ERROR: venc_init failed!\n");
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        return -1;
    }
    printf("      VENC initialized\n");
    
    // =====================================================
    // 6. Инициализация RTSP сервера
    // =====================================================
    printf("[6/6] Starting RTSP server on port 554...\n");
    
    rtsp_demo_handle rtsp_demo = create_rtsp_demo(554);
    if (rtsp_demo == NULL) {
        printf("ERROR: create_rtsp_demo failed!\n");
        RK_MPI_VENC_DestroyChn(0);
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        return -1;
    }
    
    rtsp_session_handle rtsp_session = rtsp_new_session(rtsp_demo, "/live/0");
    if (rtsp_session == NULL) {
        printf("ERROR: rtsp_new_session failed!\n");
        rtsp_del_demo(rtsp_demo);
        RK_MPI_VENC_DestroyChn(0);
        RK_MPI_VI_DisableChn(0, 0);
        RK_MPI_VI_DisableDev(0);
        SAMPLE_COMM_ISP_Stop(0);
        RK_MPI_SYS_Exit();
        return -1;
    }
    
    rtsp_set_video(rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
    rtsp_sync_video_ts(rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
    
    printf("      RTSP server ready!\n");
    printf("\n===========================================\n");
    printf(" RTSP URL: rtsp://<device-ip>/live/0\n");
    printf(" Use VLC or FFplay to view the stream\n");
    printf("===========================================\n\n");
    
    // Буфер для encoded stream
    VENC_STREAM_S venc_stream;
    memset(&venc_stream, 0, sizeof(VENC_STREAM_S));
    venc_stream.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
    if (venc_stream.pstPack == NULL) {
        printf("ERROR: Failed to allocate VENC_PACK_S\n");
        goto cleanup;
    }
    memset(venc_stream.pstPack, 0, sizeof(VENC_PACK_S));
    
    // Основной цикл обработки кадров
    VIDEO_FRAME_INFO_S vi_frame;
    uint64_t pts = 0;
    uint32_t time_ref = 0;
    int frame_count = 0;
    int detect_interval = 3; // Детекция каждые N кадров
    
    printf("Starting video processing loop...\n\n");
    
    while (g_running) {
        // Получаем кадр от камеры
        memset(&vi_frame, 0, sizeof(VIDEO_FRAME_INFO_S));
        s32Ret = RK_MPI_VI_GetChnFrame(0, 0, &vi_frame, 2000);
        if (s32Ret != RK_SUCCESS) {
            if (s32Ret != RK_ERR_VI_TIMEOUT) {
                printf("VI GetChnFrame error: %x\n", s32Ret);
            }
            usleep(10000);
            continue;
        }
        
        // Получаем доступ к данным кадра (NV12/NV21)
        void* vi_virt_addr = RK_MPI_MB_Handle2VirAddr(vi_frame.stVFrame.pMbBlk);
        if (vi_virt_addr == NULL) {
            RK_MPI_VI_ReleaseChnFrame(0, 0, &vi_frame);
            continue;
        }
        
        // Создаем OpenCV Mat для YUV420SP
        cv::Mat yuv_img(STREAM_HEIGHT + STREAM_HEIGHT / 2, STREAM_WIDTH, CV_8UC1, vi_virt_addr);
        cv::Mat bgr_img;
        cv::cvtColor(yuv_img, bgr_img, cv::COLOR_YUV420sp2BGR);
        
        // Выполняем детекцию каждые N кадров
        if (frame_count % detect_interval == 0) {
            // Letterbox преобразование
            cv::Mat input_img = letterbox(bgr_img);
            
            // Копируем данные во входной буфер RKNN
            memcpy(rknn_ctx.input_mems[0]->virt_addr, input_img.data, MODEL_WIDTH * MODEL_HEIGHT * 3);
            
            // Инференс
            object_detect_result_list results;
            ret = inference_yolov5_model(&rknn_ctx, &results);
            
            if (ret == 0 && results.count > 0) {
                // Рисуем bounding boxes
                for (int i = 0; i < results.count; i++) {
                    object_detect_result* det = &results.results[i];
                    
                    int x1 = det->box.x;
                    int y1 = det->box.y;
                    int x2 = det->box.x + det->box.w;
                    int y2 = det->box.y + det->box.h;
                    
                    // Конвертируем координаты обратно в оригинальное изображение
                    convert_coords(&x1, &y1, &x2, &y2);
                    
                    // Ограничиваем координаты размерами изображения
                    x1 = (x1 < 0) ? 0 : ((x1 > STREAM_WIDTH) ? STREAM_WIDTH : x1);
                    y1 = (y1 < 0) ? 0 : ((y1 > STREAM_HEIGHT) ? STREAM_HEIGHT : y1);
                    x2 = (x2 < 0) ? 0 : ((x2 > STREAM_WIDTH) ? STREAM_WIDTH : x2);
                    y2 = (y2 < 0) ? 0 : ((y2 > STREAM_HEIGHT) ? STREAM_HEIGHT : y2);
                    
                    const char* class_name = coco_cls_to_name(det->cls_id);
                    float confidence = det->prop * 100.0f;
                    
                    // Рисуем прямоугольник
                    cv::rectangle(bgr_img, cv::Point(x1, y1), cv::Point(x2, y2), 
                                 cv::Scalar(0, 255, 0), 2);
                    
                    // Подпись
                    char label[64];
                    snprintf(label, sizeof(label), "%s %.1f%%", class_name, confidence);
                    
                    int baseline = 0;
                    cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 
                                                        0.5, 1, &baseline);
                    
                    // Фон для текста
                    cv::rectangle(bgr_img, 
                                 cv::Point(x1, y1 - text_size.height - 5),
                                 cv::Point(x1 + text_size.width, y1),
                                 cv::Scalar(0, 255, 0), -1);
                    
                    // Текст
                    cv::putText(bgr_img, label, cv::Point(x1, y1 - 5),
                               cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
                    
                    printf("[%d] %s (%d,%d)-(%d,%d) %.1f%%\n", 
                           i, class_name, x1, y1, x2, y2, confidence);
                }
            }
        }
        
        // Подготовка кадра для энкодера
        VIDEO_FRAME_INFO_S enc_frame;
        memset(&enc_frame, 0, sizeof(VIDEO_FRAME_INFO_S));
        enc_frame.stVFrame.u32Width = STREAM_WIDTH;
        enc_frame.stVFrame.u32Height = STREAM_HEIGHT;
        enc_frame.stVFrame.u32VirWidth = STREAM_WIDTH;
        enc_frame.stVFrame.u32VirHeight = STREAM_HEIGHT;
        enc_frame.stVFrame.enPixelFormat = RK_FMT_YUV420SP;
        enc_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs();
        enc_frame.stVFrame.u32TimeRef = time_ref++;
        
        // Копируем BGR обратно в YUV буфер
        cv::Mat yuv_out(STREAM_HEIGHT + STREAM_HEIGHT / 2, STREAM_WIDTH, CV_8UC1, vi_virt_addr);
        cv::cvtColor(bgr_img, yuv_out, cv::COLOR_BGR2YUV_I420);
        
        // Отправляем кадр в энкодер
        s32Ret = RK_MPI_VENC_SendFrame(0, &enc_frame, 2000);
        if (s32Ret != RK_SUCCESS) {
            printf("VENC SendFrame error: %x\n", s32Ret);
        }
        
        // Освобождаем VI кадр
        RK_MPI_VI_ReleaseChnFrame(0, 0, &vi_frame);
        
        // Получаем закодированный поток
        memset(venc_stream.pstPack, 0, sizeof(VENC_PACK_S));
        s32Ret = RK_MPI_VENC_GetStream(0, &venc_stream, 2000);
        if (s32Ret == RK_SUCCESS) {
            if (venc_stream.pstPack->u32Len > 0) {
                void* pack_data = RK_MPI_MB_Handle2VirAddr(venc_stream.pstPack->pMbBlk);
                if (pack_data != NULL) {
                    // Отправляем через RTSP
                    rtsp_tx_video(rtsp_session, (uint8_t*)pack_data, 
                                 venc_stream.pstPack->u32Len,
                                 venc_stream.pstPack->u64PTS);
                    rtsp_do_event(rtsp_demo);
                }
            }
            
            // Освобождаем поток
            RK_MPI_VENC_ReleaseStream(0, &venc_stream);
        }
        
        frame_count++;
        
        // Небольшая задержка для стабильности
        usleep(5000); // ~200 FPS макс
    }
    
    printf("\nStopping...\n");
    
cleanup:
    // Очистка ресурсов
    if (venc_stream.pstPack) {
        free(venc_stream.pstPack);
    }
    
    printf("Releasing VENC...\n");
    RK_MPI_VENC_StopRecvFrame(0);
    RK_MPI_VENC_DestroyChn(0);
    
    printf("Releasing VI...\n");
    RK_MPI_VI_DisableChn(0, 0);
    RK_MPI_VI_DisableDev(0);
    
    printf("Stopping ISP...\n");
    SAMPLE_COMM_ISP_Stop(0);
    
    printf("Closing RTSP...\n");
    if (rtsp_demo) {
        rtsp_del_demo(rtsp_demo);
    }
    
    printf("Exiting MPI...\n");
    RK_MPI_SYS_Exit();
    
    printf("Releasing RKNN model...\n");
    release_yolov5_model(&rknn_ctx);
    
    printf("Done.\n");
    return 0;
}
