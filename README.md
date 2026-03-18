# LuckFox Pico Mini + SC3336 Camera + YOLOv5 RKNN + RTSP Stream

Полное решение для детекции объектов в реальном времени с трансляцией по RTSP.

## Описание

Программа выполняет:
1. Захват видео с камеры SC3336 через ISP/VI
2. Обработку кадров нейросетью YOLOv5 (RKNN формат)
3. Отрисовку bounding boxes на кадре
4. Кодирование в H.264 через VENC
5. RTSP трансляцию результата

## Структура проекта

```
/workspace/
├── main.cc              # Основной файл программы
├── luckfox_mpi.h        # MPI функции (VI, VENC)
├── luckfox_mpi.cc       
├── yolov5.h             # RKNN модель YOLOv5
├── yolov5.cc            
├── postprocess_impl.cc  # Постобработка (NMS, декодирование)
├── rtsp_demo.h          # RTSP сервер интерфейс
├── rtsp_demo.c          # RTSP сервер реализация
├── Makefile             # Файл сборки
└── model/
    ├── yolov5.rknn      # Модель YOLOv5 (нужно добавить)
    └── coco_80_labels_list.txt  # Названия классов COCO
```

## Требования

### На хост-компьютере (для сборки):
- Кросс-компилятор из SDK LuckFox
- Путь к SDK: `/opt/luckfox_sdk` (или укажите свой в Makefile)

### На плате LuckFox Pico Mini:
- Прошивка с поддержкой MPI и RKNN
- Камера SC3336 подключена к MIPI CSI
- Библиотеки: `librknn_api.so`, `librk_mpi.so`, `libimp.so`, `libaiq.so`
- OpenCV для embedded ARM

## Сборка

### Вариант 0: Автоматическая настройка окружения (рекомендуется)

```bash
# Активация окружения SDK (скрипт сам найдет SDK)
source env.sh

# Или укажите путь к SDK явно
source env.sh /home/user/luckfox-pico

# Сборка
make clean && make
```

### Вариант 1: Ручная настройка через Makefile

```bash
# Установите правильный путь к SDK в Makefile
export SDK_PATH=/path/to/luckfox_sdk

# Сборка
make clean
make

# Копирование на плату (замените IP на ваш)
make install
```

### Вариант 2: Ручная компиляция

```bash
# Экспорт путей к инструментальной цепи
export PATH=$SDK_PATH/toolchain/gcc/linux-x86_64/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH

# Компиляция
arm-rockchip830-linux-uclibcgnueabihf-g++ -Wall -O2 -std=c++11 \
    -I$SDK_PATH/rv1106_rv1103/MPP/include \
    -I$SDK_PATH/rv1106_rv1103/RKNN/include \
    -c main.cc -o main.o

arm-rockchip830-linux-uclibcgnueabihf-g++ -Wall -O2 -std=c++11 \
    -I$SDK_PATH/rv1106_rv1103/MPP/include \
    -I$SDK_PATH/rv1106_rv1103/RKNN/include \
    -c luckfox_mpi.cc -o luckfox_mpi.o

arm-rockchip830-linux-uclibcgnueabihf-g++ -Wall -O2 -std=c++11 \
    -I$SDK_PATH/rv1106_rv1103/MPP/include \
    -I$SDK_PATH/rv1106_rv1103/RKNN/include \
    -c yolov5.cc -o yolov5.o

arm-rockchip830-linux-uclibcgnueabihf-g++ -Wall -O2 -std=c++11 \
    -I$SDK_PATH/rv1106_rv1103/MPP/include \
    -I$SDK_PATH/rv1106_rv1103/RKNN/include \
    -c postprocess_impl.cc -o postprocess_impl.o

arm-rockchip830-linux-uclibcgnueabihf-gcc -Wall -O2 \
    -I. -c rtsp_demo.c -o rtsp_demo.o

# Линковка
arm-rockchip830-linux-uclibcgnueabihf-g++ -o yolov5_rtsp \
    main.o luckfox_mpi.o yolov5.o postprocess_impl.o rtsp_demo.o \
    -L$SDK_PATH/rv1106_rv1103/MPP/lib \
    -L$SDK_PATH/rv1106_rv1103/RKNN/lib \
    -lrknn_api -lrk_mpi -limp -laiq \
    -lopencv_core -lopencv_imgproc -lopencv_highgui \
    -lpthread -lm -lrt
```

## Подготовка модели YOLOv5

### Конвертация модели в RKNN формат

На хост-компьютере выполните конвертацию PyTorch модели:

```python
import rknn_toolkit2 as rknn

# Создание RKNN объекта
rknn = rknn.RKNN2()

# Конфигурация
rknn.config(
    target_platform='rv1106',
    optimization_level=3
)

# Загрузка ONNX модели
rknn.load_onnx(model='./yolov5s.onnx')

# Build модели
rknn.build(do_quantization=True, dataset='./dataset.txt')

# Экспорт
rknn.export_rknn('./yolov5.rknn')
```

Скопируйте полученный файл `yolov5.rknn` в папку `model/` на плате.

## Запуск на плате

```bash
# Копирование файлов на плату
scp yolov5_rtsp root@192.168.1.10:/root/
scp -r model root@192.168.1.10:/root/

# Подключение к плате
ssh root@192.168.1.10

# Запуск
cd /root
./yolov5_rtsp
```

## Просмотр потока

RTSP поток доступен по адресу:
```
rtsp://<IP-платы>/live/0
```

### VLC Player
```
vlc rtsp://192.168.1.10/live/0
```

### FFplay
```bash
ffplay -rtsp_transport tcp rtsp://192.168.1.10/live/0
```

### GStreamer
```bash
gst-launch-1.0 rtspsrc location=rtsp://192.168.1.10/live/0 ! rtph264depay ! h264parse ! avdec_h264 ! autovideosink
```

## Параметры

В коде можно изменить следующие параметры:

```cpp
#define STREAM_WIDTH  640    // Разрешение выходного потока
#define STREAM_HEIGHT 480

#define MODEL_WIDTH  640     // Размер входа модели YOLOv5
#define MODEL_HEIGHT 640

#define NMS_THRESH  0.45f    // Порог NMS
#define BOX_THRESH  0.25f    // Порог доверия

int detect_interval = 3;     // Детекция каждые N кадров
```

## Структура работы

1. **Инициализация**:
   - Загрузка RKNN модели
   - Инициализация MPI системы
   - Настройка ISP для камеры
   - Создание VI канала
   - Создание VENC канала (H.264)
   - Запуск RTSP сервера

2. **Основной цикл**:
   - Получение кадра от камеры (VI)
   - Преобразование YUV → BGR
   - Letterbox преобразование для модели
   - RKNN инференс
   - Постобработка (NMS)
   - Отрисовка bounding boxes
   - Преобразование BGR → YUV
   - Отправка в энкодер (VENC)
   - RTSP трансляция закодированного потока

3. **Завершение**:
   - Корректное освобождение всех ресурсов
   - Остановка ISP, VI, VENC
   - Выгрузка RKNN модели

## Troubleshooting

### Ошибка "RK_MPI_SYS_Init failed"
- Проверьте загрузку модулей ядра: `lsmod | grep rga`
- Перезапустите сервисы: `/etc/init.d/S99mpp stop && /etc/init.d/S99mpp start`

### Ошибка "Failed to load YOLOv5 model"
- Проверьте путь к модели: `ls -la ./model/yolov5.rknn`
- Убедитесь что модель сконвертирована для RV1106

### Нет изображения с камеры
- Проверьте подключение камеры: `dmesg | grep sensor`
- Убедитесь что IQ файлы присутствуют: `ls /etc/iqfiles/`

### RTSP не подключается
- Проверьте firewall: `iptables -L`
- Убедитесь что порт 554 открыт
- Проверьте сеть: `ping <IP-платы>`

## Лицензия

Проект использует код из SDK Rockchip под лицензией Apache 2.0.
