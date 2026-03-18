/*
 * Простая реализация RTSP сервера для LuckFox Pico
 * Основано на примере из SDK LuckFox
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#include "rtsp_demo.h"

#define MAX_CLIENTS 4
#define RTP_PORT_BASE 50000

typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
    int session_id;
    uint32_t ssrc;
    uint16_t seq;
    int rtp_port;
    int rtcp_port;
} rtsp_client_t;

typedef struct {
    int server_fd;
    int port;
    rtsp_client_t clients[MAX_CLIENTS];
    int client_count;
    uint32_t rtptime_offset;
    uint64_t ntp_offset;
} rtsp_demo_t;

static uint32_t get_current_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static uint64_t get_ntp_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t ntp = (uint64_t)(tv.tv_sec + 2208988800ULL) << 32;
    ntp |= (uint32_t)((double)tv.tv_usec / 1000000.0 * (double)(1LL << 32));
    return ntp;
}

static int create_udp_socket(int port) {
    int sock;
    struct sockaddr_in addr;
    
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

rtsp_demo_handle create_rtsp_demo(int port) {
    rtsp_demo_t* demo = (rtsp_demo_t*)malloc(sizeof(rtsp_demo_t));
    if (!demo) {
        return NULL;
    }
    
    memset(demo, 0, sizeof(rtsp_demo_t));
    demo->port = port;
    demo->rtptime_offset = get_current_ms();
    demo->ntp_offset = get_ntp_time();
    
    // Создаем TCP сокет для RTSP
    demo->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (demo->server_fd < 0) {
        free(demo);
        return NULL;
    }
    
    int reuse = 1;
    setsockopt(demo->server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    
    if (bind(demo->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(demo->server_fd);
        free(demo);
        return NULL;
    }
    
    if (listen(demo->server_fd, 5) < 0) {
        close(demo->server_fd);
        free(demo);
        return NULL;
    }
    
    printf("RTSP server listening on port %d\\n", port);
    
    return (rtsp_demo_handle)demo;
}

rtsp_session_handle rtsp_new_session(rtsp_demo_handle demo_handle, const char* path) {
    rtsp_demo_t* demo = (rtsp_demo_t*)demo_handle;
    if (!demo || !path) {
        return NULL;
    }
    
    // В упрощенной версии просто возвращаем указатель на демо
    // Путь сохраняется для последующего использования
    printf("New RTSP session created: %s\\n", path);
    return demo_handle;
}

void rtsp_set_video(rtsp_session_handle session, int codec_id, const uint8_t* extradata, int extradata_size) {
    printf("Video stream configured: codec=%d, extradata_size=%d\\n", codec_id, extradata_size);
    
    // В реальной реализации здесь сохраняются параметры кодека и SPS/PPS
}

void rtsp_sync_video_ts(rtsp_session_handle session, uint32_t reltime, uint64_t ntptime) {
    // Синхронизация временных меток
}

uint32_t rtsp_get_reltime() {
    return get_current_ms();
}

uint64_t rtsp_get_ntptime() {
    return get_ntp_time();
}

// Формирование RTP пакета H264
static int send_rtp_packet(int sock, struct sockaddr_in* client_addr, 
                          const uint8_t* data, int len, 
                          uint16_t seq, uint32_t ts, uint32_t ssrc,
                          int marker) {
    uint8_t rtp_header[12] = {0};
    
    // RTP header
    rtp_header[0] = 0x80;  // Version 2
    if (marker) {
        rtp_header[1] = 0x60 | 96;  // Payload type 96 (H264), Marker bit
    } else {
        rtp_header[1] = 0x60 | 96;
    }
    rtp_header[2] = (seq >> 8) & 0xFF;
    rtp_header[3] = seq & 0xFF;
    rtp_header[4] = (ts >> 24) & 0xFF;
    rtp_header[5] = (ts >> 16) & 0xFF;
    rtp_header[6] = (ts >> 8) & 0xFF;
    rtp_header[7] = ts & 0xFF;
    rtp_header[8] = (ssrc >> 24) & 0xFF;
    rtp_header[9] = (ssrc >> 16) & 0xFF;
    rtp_header[10] = (ssrc >> 8) & 0xFF;
    rtp_header[11] = ssrc & 0xFF;
    
    struct iovec iov[2];
    iov[0].iov_base = rtp_header;
    iov[0].iov_len = 12;
    iov[1].iov_base = (void*)data;
    iov[1].iov_len = len;
    
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = client_addr;
    msg.msg_namelen = sizeof(*client_addr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    
    return sendmsg(sock, &msg, 0);
}

void rtsp_tx_video(rtsp_session_handle session, const uint8_t* frame, int len, uint64_t pts) {
    rtsp_demo_t* demo = (rtsp_demo_t*)session;
    if (!demo || !frame || len <= 0) {
        return;
    }
    
    // Для простоты отправляем первому подключенному клиенту
    if (demo->client_count == 0) {
        return;
    }
    
    rtsp_client_t* client = &demo->clients[0];
    
    // Создаем UDP сокет если еще не создан
    static int rtp_sock = -1;
    static struct sockaddr_in rtp_addr;
    
    if (rtp_sock < 0) {
        rtp_sock = create_udp_socket(0);
        if (rtp_sock < 0) {
            return;
        }
        memcpy(&rtp_addr, &client->client_addr, sizeof(rtp_addr));
        rtp_addr.sin_port = htons(client->rtp_port);
    }
    
    // Разбиваем кадр на RTP пакеты (MTU ~1400 байт)
    const int MTU_SIZE = 1400;
    const uint8_t* ptr = frame;
    int remaining = len;
    uint16_t seq = client->seq;
    uint32_t ts = (uint32_t)(pts / 1000 * 90);  // Конвертация в 90kHz
    
    while (remaining > 0) {
        int chunk_size = (remaining > MTU_SIZE) ? MTU_SIZE : remaining;
        int marker = (remaining <= MTU_SIZE) ? 1 : 0;
        
        send_rtp_packet(rtp_sock, &rtp_addr, ptr, chunk_size, 
                       seq++, ts, client->ssrc, marker);
        
        ptr += chunk_size;
        remaining -= chunk_size;
    }
    
    client->seq = seq;
}

void rtsp_do_event(rtsp_demo_handle demo_handle) {
    rtsp_demo_t* demo = (rtsp_demo_t*)demo_handle;
    if (!demo) {
        return;
    }
    
    // Проверяем новые подключения (неблокирующе)
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Устанавливаем non-blocking для accept
    int flags = fcntl(demo->server_fd, F_GETFL, 0);
    fcntl(demo->server_fd, F_SETFL, flags | O_NONBLOCK);
    
    if (demo->client_count < MAX_CLIENTS) {
        int client_fd = accept(demo->server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            rtsp_client_t* client = &demo->clients[demo->client_count];
            client->client_fd = client_fd;
            client->client_addr = client_addr;
            client->session_id = demo->client_count;
            client->ssrc = 0x12345678 + demo->client_count;
            client->seq = 0;
            client->rtp_port = RTP_PORT_BASE + demo->client_count * 2;
            client->rtcp_port = client->rtp_port + 1;
            
            demo->client_count++;
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            printf("New RTSP client connected: %s:%d\\n", ip_str, ntohs(client_addr.sin_port));
        }
    }
    
    // Восстанавливаем blocking режим
    fcntl(demo->server_fd, F_SETFL, flags);
}

void rtsp_del_demo(rtsp_demo_handle demo_handle) {
    rtsp_demo_t* demo = (rtsp_demo_t*)demo_handle;
    if (!demo) {
        return;
    }
    
    // Закрываем все клиентские сокеты
    for (int i = 0; i < demo->client_count; i++) {
        if (demo->clients[i].client_fd >= 0) {
            close(demo->clients[i].client_fd);
        }
    }
    
    // Закрываем серверный сокет
    if (demo->server_fd >= 0) {
        close(demo->server_fd);
    }
    
    free(demo);
}
