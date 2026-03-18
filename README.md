# LuckFox Pico Mini YOLOv5 RTSP Server

Программа для платы LuckFox Pico Mini с камерой SC3336, выполняющая детекцию объектов с помощью YOLOv5 (RKNN) и транслирующая результат по RTSP.

## Структура проекта

```
/workspace/
├── main.cc              # Основной файл программы
├── luckfox_mpi.cc       # Функции инициализации MPI (VI, VENC)
├── luckfox_mpi.h        # Заголовочный файл для MPI
├── yolov5.cc            # Инициализация и запуск RKNN модели
├── yolov5.h             # Заголовочный файл для YOLOv5
├── postprocess_impl.cc  # Постобработка результатов детекции
├── rtsp_demo.h          # Заголовочный файл для RTSP сервера
└── README.md            # Этот файл
```

## Требования

1. Плата **LuckFox Pico Mini** (RV1106)
2. Камера **SC3336**
3. Модель YOLOv5 в формате **.rknn** (должна быть в папке `./model/yolov5.rknn`)
4. SDK для LuckFox Pico с библиотеками:
   - librknn_api
   - librga
   - OpenCV
   - Библиотеки MPI (VI, VENC, ISP)

## Компиляция на устройстве

Пример команды компиляции (адаптируйте пути под вашу среду):

```bash
arm-linux-gnueabihf-g++ -o yolov5_rtsp \
    main.cc luckfox_mpi.cc yolov5.cc postprocess_impl.cc \
    -I./include \
    -L./lib \
    -lrknn_api -lrga -lopencv_core -lopencv_imgproc -lopencv_imgcodecs \
    -lpthread -lm -ldl \
    -Wl,-rpath,/usr/lib
```

Или используйте CMake с предоставленным toolchain файлом от LuckFox.

## Подготовка модели

1. Сконвертируйте модель YOLOv5 в формат RKNN:
```bash
python3 -m rknn_toolkit2 \
    --config yolov5_config.py \
    --model yolov5s.pt \
    --output yolov5s
```

2. Скопируйте модель на устройство:
```bash
mkdir -p /path/to/app/model
cp yolov5s.rknn /path/to/app/model/yolov5.rknn
```

## Запуск

```bash
cd /path/to/app
./yolov5_rtsp
```

## Просмотр потока

После запуска программа выведет сообщение:
```
RTSP URL: rtsp://<device-ip>/live/0
```

Для просмотра используйте:
- **VLC**: `vlc rtsp://<IP-адрес-платы>/live/0`
- **FFplay**: `ffplay rtsp://<IP-адрес-платы>/live/0`
- **ONVIF Device Manager** или другой RTSP клиент

## Параметры

В коде можно изменить:
- `STREAM_WIDTH`, `STREAM_HEIGHT` - разрешение выходного потока (по умолчанию 640x480)
- `MODEL_WIDTH`, `MODEL_HEIGHT` - размер входа модели (по умолчанию 640x640)
- `detect_interval` - частота детекции (каждые N кадров, по умолчанию 3)

## Особенности реализации

1. **Letterbox преобразование** - изображение масштабируется с сохранением пропорций
2. **NMS (Non-Maximum Suppression)** - фильтрация перекрывающихся детекций
3. **Оптимизация** - детекция выполняется не каждый кадр для повышения FPS
4. **Корректная обработка буферов** - правильное освобождение ресурсов MPI
5. **Обработка сигналов** - корректное завершение по Ctrl+C

## Troubleshooting

### Ошибка инициализации камеры
- Проверьте подключение камеры SC3336
- Убедитесь, что IQ файлы присутствуют в `/etc/iqfiles`

### Ошибка загрузки модели
- Проверьте путь к модели (`./model/yolov5.rknn`)
- Убедитесь, что модель сконвертирована для RV1106

### Нет изображения в RTSP
- Проверьте настройки сети
- Убедитесь, что порт 554 не занят
- Проверьте логи на ошибки VENC

## Лицензия

Код постобработки основан на примерах Rockchip Electronics Co., Ltd. (Apache License 2.0)
