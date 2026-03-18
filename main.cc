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

#define DISP_WIDTH  1280
#define DISP_HEIGHT 720

// disp size
int width    = DISP_WIDTH;
int height   = DISP_HEIGHT;

// model size
int model_width = 640;
int model_height = 640;	
float scale;
int leftPadding;
int topPadding;

cv::Mat letterbox(cv::Mat input)
{
    float scaleX = (float)model_width  / (float)width; 
    float scaleY = (float)model_height / (float)height; 
    scale = scaleX < scaleY ? scaleX : scaleY;
    
    int inputWidth   = (int)((float)width * scale);
    int inputHeight  = (int)((float)height * scale);

    leftPadding = (model_width  - inputWidth) / 2;
    topPadding  = (model_height - inputHeight) / 2;	

    cv::Mat inputScale;
    cv::resize(input, inputScale, cv::Size(inputWidth, inputHeight), 0, 0, cv::INTER_LINEAR);	
    cv::Mat letterboxImage(640, 640, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Rect roi(leftPadding, topPadding, inputWidth, inputHeight);
    inputScale.copyTo(letterboxImage(roi));

    return letterboxImage; 	
}

void mapCoordinates(int *x, int *y) {	
    int mx = *x - leftPadding;
    int my = *y - topPadding;

    *x = (int)((float)mx / scale);
    *y = (int)((float)my / scale);
}

int main(int argc, char *argv[]) {
    system("RkLunch-stop.sh");
    usleep(100000); // Даем время на остановку сервисов
    
    RK_S32 s32Ret = 0; 
    int sX, sY, eX, eY; 
        
    // Rknn model
    char text[32];
    rknn_app_context_t rknn_app_ctx;	
    object_detect_result_list od_results;
    int ret;
    const char *model_path = "./model/yolov5.rknn";
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));	
    
    ret = init_yolov5_model(model_path, &rknn_app_ctx);
    if (ret != 0) {
        printf("ERROR: init_yolov5_model failed!\n");
        return -1;
    }
    printf("init rknn model success!\n");
    
    ret = init_post_process();
    if (ret != 0) {
        printf("ERROR: init_post_process failed!\n");
        return -1;
    }

    // h264_frame	
    VENC_STREAM_S stFrame;	
    memset(&stFrame, 0, sizeof(VENC_STREAM_S));
    stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));
    if (stFrame.pstPack == NULL) {
        printf("ERROR: malloc for VENC_PACK_S failed!\n");
        return -1;
    }
    memset(stFrame.pstPack, 0, sizeof(VENC_PACK_S));
    
    RK_U64 H264_PTS = 0;
    RK_U32 H264_TimeRef = 0; 
    VIDEO_FRAME_INFO_S stViFrame;
    memset(&stViFrame, 0, sizeof(VIDEO_FRAME_INFO_S));
    
    // Create Pool
    MB_POOL_CONFIG_S PoolCfg;
    memset(&PoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
    PoolCfg.u64MBSize = width * height * 3; // Для RGB888
    PoolCfg.u32MBCnt = 3; // Увеличиваем количество буферов
    PoolCfg.enAllocType = MB_ALLOC_TYPE_DMA;
    
    MB_POOL src_Pool = RK_MPI_MB_CreatePool(&PoolCfg);
    if (src_Pool == MB_INVALID_POOLID) { // Исправлено: MB_INVALID_POOLID
        printf("ERROR: RK_MPI_MB_CreatePool failed!\n");
        free(stFrame.pstPack);
        return -1;
    }
    printf("Create Pool success !\n");	

    // Get MB from Pool 
    MB_BLK src_Blk = RK_MPI_MB_GetMB(src_Pool, width * height * 3, RK_TRUE);
    if (src_Blk == MB_INVALID_HANDLE) { // Исправлено: MB_INVALID_HANDLE
        printf("ERROR: RK_MPI_MB_GetMB failed!\n");
        RK_MPI_MB_DestroyPool(src_Pool);
        free(stFrame.pstPack);
        return -1;
    }
    
    // Build h264_frame
    VIDEO_FRAME_INFO_S h264_frame;
    memset(&h264_frame, 0, sizeof(VIDEO_FRAME_INFO_S));
    h264_frame.stVFrame.u32Width = width;
    h264_frame.stVFrame.u32Height = height;
    h264_frame.stVFrame.u32VirWidth = width;
    h264_frame.stVFrame.u32VirHeight = height;
    h264_frame.stVFrame.enPixelFormat = RK_FMT_RGB888; 
    h264_frame.stVFrame.u32FrameFlag = 160;
    h264_frame.stVFrame.pMbBlk = src_Blk;
    
    unsigned char *data = (unsigned char *)RK_MPI_MB_Handle2VirAddr(src_Blk);
    if (data == NULL) {
        printf("ERROR: RK_MPI_MB_Handle2VirAddr failed!\n");
        RK_MPI_MB_ReleaseMB(src_Blk);
        RK_MPI_MB_DestroyPool(src_Pool);
        free(stFrame.pstPack);
        return -1;
    }
    
    cv::Mat frame(cv::Size(width, height), CV_8UC3, data);

    // rkaiq init
    RK_BOOL multi_sensor = RK_FALSE;	
    const char *iq_dir = "/etc/iqfiles";
    rk_aiq_working_mode_t hdr_mode = RK_AIQ_WORKING_MODE_NORMAL;
    
    ret = SAMPLE_COMM_ISP_Init(0, hdr_mode, multi_sensor, iq_dir);
    if (ret != RK_SUCCESS) {
        printf("ERROR: SAMPLE_COMM_ISP_Init failed!\n");
        RK_MPI_MB_ReleaseMB(src_Blk);
        RK_MPI_MB_DestroyPool(src_Pool);
        free(stFrame.pstPack);
        return -1;
    }
    
    ret = SAMPLE_COMM_ISP_Run(0);
    if (ret != RK_SUCCESS) {
        printf("ERROR: SAMPLE_COMM_ISP_Run failed!\n");
        return -1;
    }

    // rkmpi init
    if (RK_MPI_SYS_Init() != RK_SUCCESS) {
        printf("ERROR: rk mpi sys init fail!\n");
        return -1;
    }

    // rtsp init	
    rtsp_demo_handle g_rtsplive = NULL;
    rtsp_session_handle g_rtsp_session = NULL;
    
    g_rtsplive = create_rtsp_demo(554);
    if (g_rtsplive == NULL) {
        printf("ERROR: create_rtsp_demo failed!\n");
        return -1;
    }
    
    g_rtsp_session = rtsp_new_session(g_rtsplive, "/live/0");
    if (g_rtsp_session == NULL) {
        printf("ERROR: rtsp_new_session failed!\n");
        return -1;
    }
    
    rtsp_set_video(g_rtsp_session, RTSP_CODEC_ID_VIDEO_H264, NULL, 0);
    rtsp_sync_video_ts(g_rtsp_session, rtsp_get_reltime(), rtsp_get_ntptime());
    
    // vi init
    ret = vi_dev_init();
    if (ret != 0) {
        printf("ERROR: vi_dev_init failed!\n");
        return -1;
    }
    
    ret = vi_chn_init(0, width, height);
    if (ret != 0) {
        printf("ERROR: vi_chn_init failed!\n");
        return -1;
    }

    // venc init
    RK_CODEC_ID_E enCodecType = RK_VIDEO_ID_AVC;
    ret = venc_init(0, width, height, enCodecType);
    if (ret != 0) {
        printf("ERROR: venc_init failed!\n");
        return -1;
    }
    printf("venc init success\n");	
	
    int frame_count = 0;
    while(1) {	
        // get vi frame
        h264_frame.stVFrame.u32TimeRef = H264_TimeRef++;
        h264_frame.stVFrame.u64PTS = TEST_COMM_GetNowUs(); 
        
        s32Ret = RK_MPI_VI_GetChnFrame(0, 0, &stViFrame, 2000);
        if (s32Ret == RK_SUCCESS) {
            void *vi_data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);	
            if (vi_data != NULL) {
                cv::Mat yuv420sp(height + height / 2, width, CV_8UC1, vi_data);
                cv::Mat bgr(height, width, CV_8UC3, data);			
                
                cv::cvtColor(yuv420sp, bgr, cv::COLOR_YUV420sp2BGR);
                
                // Выполняем инференс каждый 3-й кадр
                if (frame_count % 3 == 0) {
                    // letterbox
                    cv::Mat letterboxImage = letterbox(bgr);	
                    memcpy(rknn_app_ctx.input_mems[0]->virt_addr, letterboxImage.data, model_width * model_height * 3);		
                    
                    ret = inference_yolov5_model(&rknn_app_ctx, &od_results);
                    if (ret == 0) {
                        for(int i = 0; i < od_results.count; i++) {					
                            if (od_results.count >= 1) {
                                object_detect_result *det_result = &(od_results.results[i]);
                
                                sX = (int)(det_result->box.left);
                                sY = (int)(det_result->box.top);
                                eX = (int)(det_result->box.right);
                                eY = (int)(det_result->box.bottom);
                                
                                mapCoordinates(&sX, &sY);
                                mapCoordinates(&eX, &eY);
                                
                                printf("%s @ (%d %d %d %d) %.3f\n", 
                                       coco_cls_to_name(det_result->cls_id),
                                       sX, sY, eX, eY, det_result->prop);

                                cv::rectangle(bgr,
                                            cv::Point(sX, sY),
                                            cv::Point(eX, eY),
                                            cv::Scalar(0, 255, 0), 3);
                                
                                sprintf(text, "%s %.1f%%", 
                                        coco_cls_to_name(det_result->cls_id), 
                                        det_result->prop * 100);
                                
                                cv::putText(bgr, text,
                                          cv::Point(sX, sY - 8),
                                          cv::FONT_HERSHEY_SIMPLEX, 0.8,
                                          cv::Scalar(0, 255, 0), 2);
                            }
                        }
                    }
                }
                
                // Копируем обработанный кадр
                memcpy(data, bgr.data, width * height * 3);
            }
            
            // Освобождаем VI кадр
            s32Ret = RK_MPI_VI_ReleaseChnFrame(0, 0, &stViFrame);
            if (s32Ret != RK_SUCCESS) {
                printf("WARNING: RK_MPI_VI_ReleaseChnFrame fail %x\n", s32Ret);
            }
        } else {
            printf("WARNING: RK_MPI_VI_GetChnFrame timeout\n");
            continue;
        }
        
        // Отправляем кадр в энкодер
        s32Ret = RK_MPI_VENC_SendFrame(0, &h264_frame, 2000);
        if (s32Ret != RK_SUCCESS) {
            printf("WARNING: RK_MPI_VENC_SendFrame fail %x\n", s32Ret);
        }

        // Получаем закодированный поток
        VENC_PACK_S stPack;
        memset(&stPack, 0, sizeof(VENC_PACK_S));
        stFrame.pstPack = &stPack;
        
        s32Ret = RK_MPI_VENC_GetStream(0, &stFrame, 2000);
        if (s32Ret == RK_SUCCESS) {
            if (g_rtsplive && g_rtsp_session && stFrame.pstPack->u32Len > 0) {
                void *pData = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
                if (pData != NULL) {
                    rtsp_tx_video(g_rtsp_session, (uint8_t *)pData, 
                                stFrame.pstPack->u32Len,
                                stFrame.pstPack->u64PTS);
                    rtsp_do_event(g_rtsplive);
                }
            }
            
            // Освобождаем поток
            s32Ret = RK_MPI_VENC_ReleaseStream(0, &stFrame);
            if (s32Ret != RK_SUCCESS) {
                printf("WARNING: RK_MPI_VENC_ReleaseStream fail %x\n", s32Ret);
            }
        }
        
        memset(text, 0, sizeof(text));
        frame_count++;
        
        usleep(10000); // 10ms
    }

    // Cleanup
    printf("Cleaning up...\n");
    
    if (src_Blk != MB_INVALID_HANDLE) {
        RK_MPI_MB_ReleaseMB(src_Blk);
    }
    
    if (src_Pool != MB_INVALID_POOLID) { // Исправлено: MB_INVALID_POOLID
        RK_MPI_MB_DestroyPool(src_Pool);
    }
    
    RK_MPI_VI_DisableChn(0, 0);
    RK_MPI_VI_DisableDev(0);

    SAMPLE_COMM_ISP_Stop(0);
    
    RK_MPI_VENC_StopRecvFrame(0);
    RK_MPI_VENC_DestroyChn(0);

    if (g_rtsplive) {
        rtsp_del_demo(g_rtsplive);
    }
    
    RK_MPI_SYS_Exit();

    release_yolov5_model(&rknn_app_ctx);		
    deinit_post_process();
    
    if (stFrame.pstPack) {
        free(stFrame.pstPack);
    }
    
    printf("Program finished successfully\n");
    return 0;
}
