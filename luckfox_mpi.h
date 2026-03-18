#ifndef __LUCKFOX_MPI_H__
#define __LUCKFOX_MPI_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

// Заголовки MPP для RV1106 (актуальная структура SDK)
#include "rk_type.h"
#include "rk_mpi.h"
#include "rk_mpi_cmd.h"
#include "rk_mpi_mb.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_vpss.h"
#include "rk_mpi_sys.h"
#include "rk_venc_cmd.h"
#include "rk_venc_cfg.h"
#include "rk_venc_rc.h"
#include "mpp_frame.h"
#include "mpp_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Типы из mpp_frame.h для работы с кадрами
typedef MppFrame RkMppFrame;

// Прототипы функций инициализации и работы с камерой/кодером
RK_U64 TEST_COMM_GetNowUs();
int vi_dev_init();
int vi_chn_init(int channelId, int width, int height);
int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType);
int mpi_system_init();
void mpi_system_deinit();
int mpi_camera_init(int width, int height);
void mpi_camera_deinit();
int mpi_venc_init(int width, int height, int bitrate);
void mpi_venc_deinit();

#ifdef __cplusplus
}
#endif

#endif // __LUCKFOX_MPI_H__
