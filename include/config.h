
#define FIRMWARE_STRING             "0.0.1"

#define SCREEN_WIDTH                128 // Ширина OLED-дисплея, в пикселях
#define SCREEN_HEIGHT               32 // Высота OLED-дисплея в пикселях
#define OLED_RESET                  -1 // т.к.у дисплея нет пина сброса прописываем -1, если используется общий сброс Arduino

// установлены шунты по 50 мОм для каналов CH2, CH3 (3,3 и 5 V) - MAX 3.2 А
// канал CH1 на 12 V остается на 100 мОм - MAX 1.6 А
#define INA3221_SHUNT_CH1           100
#define INA3221_SHUNT_CH2           50
#define INA3221_SHUNT_CH3           50

#define SEND_INTERVAL_MIN_MS        100
#define SEND_INTERVAL_MAX_MS        3600000 // 1 hour
#define SEND_INTERVAL_DEFAULT_MS    1000

#define DEBUG
