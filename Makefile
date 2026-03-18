# Makefile для LuckFox Pico Mini + YOLOv5 + RTSP
# Использовать с кросс-компилятором из SDK LuckFox

# Путь к SDK (изменить под вашу систему)
SDK_PATH ?= /opt/luckfox_sdk
TOOLCHAIN_PATH ?= $(SDK_PATH)/toolchain/gcc/linux-x86_64/arm-rockchip830-linux-uclibcgnueabihf/bin

# Компилятор и инструменты
CC = arm-rockchip830-linux-uclibcgnueabihf-gcc
CXX = arm-rockchip830-linux-uclibcgnueabihf-g++
AR = arm-rockchip830-linux-uclibcgnueabihf-ar
LD = arm-rockchip830-linux-uclibcgnueabihf-ld

# Если TOOLCHAIN_PATH указан, добавляем его в PATH
ifneq ($(TOOLCHAIN_PATH),)
export PATH := $(TOOLCHAIN_PATH):$(PATH)
endif

# Флаги компиляции
CFLAGS = -Wall -Wextra -O2 -std=c++11
CFLAGS += -I$(SDK_PATH)/rv1106_rv1103/MPP/include
CFLAGS += -I$(SDK_PATH)/rv1106_rv1103/RKNN/include
CFLAGS += -I./

# Флаги линковки
LDFLAGS = -L$(SDK_PATH)/rv1106_rv1103/MPP/lib \
          -L$(SDK_PATH)/rv1106_rv1103/RKNN/lib \
          -lrknn_api \
          -lrk_mpi \
          -limp \
          -laiq \
          -lopencv_core \
          -lopencv_imgproc \
          -lopencv_highgui \
          -lpthread \
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
