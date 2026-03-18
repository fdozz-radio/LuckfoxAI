#ifndef __LUCKFOX_MPI_H__
#define __LUCKFOX_MPI_H__

#include "rk_mpi.h"
#include "rk_comm_mb.h"
#include "rk_comm_vi.h"
#include "rk_comm_venc.h"
#include "rk_comm_video.h"
#include "rk_type.h"
#include "rk_aiq_api.h"
#include "sample_comm_isp.h"

#ifdef __cplusplus
extern "C" {
#endif

RK_U64 TEST_COMM_GetNowUs();
int vi_dev_init();
int vi_chn_init(int channelId, int width, int height);
int venc_init(int chnId, int width, int height, RK_CODEC_ID_E enType);

#ifdef __cplusplus
}
#endif

#endif // __LUCKFOX_MPI_H__
