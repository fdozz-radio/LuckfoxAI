# Makefile для LuckFox Pico Mini + YOLOv5 + RTSP
# Использовать с кросс-компилятором из SDK LuckFox

# Путь к SDK (должен быть установлен через env.sh или вручную)
SDK_PATH ?= /home/fdoz/luckfox-pico

# Пути к заголовочным файлам и библиотекам для RV1106
MPP_INCLUDE := $(SDK_PATH)/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf/include/rockchip
MPP_LIB := $(SDK_PATH)/media/mpp/release_mpp_rv1106_arm-rockchip830-linux-uclibcgnueabihf/lib

RKNN_INCLUDE := $(SDK_PATH)/rv1106_rv1103/RKNN/include
RKNN_LIB := $(SDK_PATH)/rv1106_rv1103/RKNN/lib

# Компилятор и инструменты (должны быть в PATH после source env.sh)
CC = arm-rockchip830-linux-uclibcgnueabihf-gcc
CXX = arm-rockchip830-linux-uclibcgnueabihf-g++

# Флаги компиляции
CFLAGS = -Wall -Wextra -O2 -std=c++11
CFLAGS += -I$(MPP_INCLUDE)
CFLAGS += -I$(RKNN_INCLUDE)
CFLAGS += -I./

# Флаги линковки
LDFLAGS = -L$(MPP_LIB) \
          -L$(RKNN_LIB) \
          -lrockchip_mpp \
          -lrknn_api \
          -lpthread \
          -ldl \
          -lm \
          -lrt

# Исходные файлы C++
CXX_SRCS = main.cc \
           luckfox_mpi.cc \
           yolov5.cc \
           postprocess_impl.cc

# Исходные файлы C
C_SRCS = rtsp_demo.c

# Объектные файлы
OBJS = $(CXX_SRCS:.cc=.o) $(C_SRCS:.c=.o)

# Исполняемый файл
TARGET = yolov5_rtsp

# Правила сборки
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)
	@echo "=========================================="
	@echo "Сборка завершена: $(TARGET)"
	@echo "Для запуска скопируйте на плату:"
	@echo "  scp $(TARGET) root@<ip-платы>:/root/"
	@echo "  scp -r model root@<ip-платы>:/root/"
	@echo "Запуск на плате:"
	@echo "  cd /root && ./$(TARGET)"
	@echo "=========================================="

%.o: %.cc
	$(CXX) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Очистка
clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf *.o

# Установка на плату (требуется настроить IP адрес)
install: $(TARGET)
	@echo "Копирование на плату..."
	@scp $(TARGET) root@192.168.1.10:/root/ 2>/dev/null || echo "Ошибка: измените IP адрес в Makefile"
	@scp -r model root@192.168.1.10:/root/ 2>/dev/null || echo "Ошибка: измените IP адрес в Makefile"

.PHONY: all clean install
