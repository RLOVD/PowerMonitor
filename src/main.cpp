#include <Arduino.h>
#include <Wire.h>

#include "config.h"

#include "INA3221.h"

#include <Adafruit_GFX.h>        //OLED библиотека
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Объявляем имя и задаем параметры

INA3221 ina3221(INA3221_ADDR40_GND);

TaskHandle_t receive_data_send_interval_task_handle;
void receive_data_send_interval_task(void* pvParameters);
xQueueHandle interval_queue_handle;
typedef int32_t interval_time_t;

TaskHandle_t send_measure_data_task_handle;
void send_measure_data_task(void* pvParameters);

TaskHandle_t get_measure_data_ina3221_task_handle;
void get_measure_data_ina3221_task(void* pvParameters);
xQueueHandle meas_data_ina3221_queue_handle;
typedef struct {
    float current[3];
    float current_compensated[3];
    float voltage[3];
} ina3221_meas;

TaskHandle_t oled_print_task_handle;
void oled_print_task(void* pvParameters);


void task_init(void) {
    interval_queue_handle = xQueueCreate(1, sizeof(interval_time_t));
    meas_data_ina3221_queue_handle = xQueueCreate(1, sizeof(ina3221_meas));

    xTaskCreatePinnedToCore(receive_data_send_interval_task, "receive_data_send_interval_task", configMINIMAL_STACK_SIZE + 1000, NULL, 1,
                            &receive_data_send_interval_task_handle, ARDUINO_RUNNING_CORE);

    xTaskCreatePinnedToCore(send_measure_data_task, "send_measure_data_task", configMINIMAL_STACK_SIZE + 1500, NULL, 1,
                            &send_measure_data_task_handle, ARDUINO_RUNNING_CORE);

    xTaskCreatePinnedToCore(get_measure_data_ina3221_task, "get_measure_data_ina3221_task", configMINIMAL_STACK_SIZE + 1000, NULL, 1,
                            &get_measure_data_ina3221_task_handle, ARDUINO_RUNNING_CORE);

    xTaskCreatePinnedToCore(oled_print_task, "oled_print_task", configMINIMAL_STACK_SIZE + 2000, NULL, 1,
                            &oled_print_task_handle, ARDUINO_RUNNING_CORE);
}

void setup() {
    setCpuFrequencyMhz(80);
    delay(10);
    Serial.begin(115200);
    Serial.println("Power Monitor");
    Serial.print("FV: ");
    Serial.println(FIRMWARE_STRING);

    task_init();
}

void loop() {

}


/*
* @brief Функция сравнивает принятую команду с заданной в cmd_str
* @return индекс с которого начинается payload если команда в буфере найдена, 0 - в противном случае
*/
unsigned check_serial_cmd(String cmd_str, char *buffer)
{
    unsigned i = 0;
    for ( ; i < cmd_str.length(); i++) {
        if (buffer[i] != cmd_str.charAt(i)) {
            return 0;
        }
    }

    return i;
}

/*
* @brief Задача чтения интервала отправки данных из serial port
*/
void receive_data_send_interval_task(void* pvParameters)
{
    while (1) {
        unsigned num = 0;
        num = Serial.available();

        if (num > 0) {  // если есть доступные данные считываем

            char buf[50] = {0};
            Serial.readBytes(buf, ((num < 50) ? num : 50));  // если num < 50 читаем num, если больше читаем 50 символов
#if defined(DEBUG)
            Serial.print("RX buf: ");
            Serial.println(buf);
#endif
            unsigned val_start = check_serial_cmd("Int=", buf);
            if (val_start) {
                Serial.print("Interval, ms = ");
                Serial.println(&buf[val_start]);

                String interval_str(&buf[val_start]);
                interval_time_t interval_ms = interval_str.toInt();

                if (interval_ms < SEND_INTERVAL_MIN_MS) {  // установка граничного значения если интервал выходит за их пределы
                    interval_ms = SEND_INTERVAL_MIN_MS;
                } else if (interval_ms > SEND_INTERVAL_MAX_MS) {
                    interval_ms = SEND_INTERVAL_MAX_MS;
                }

                xQueueSend(interval_queue_handle, &interval_ms, 0);
            }
            Serial.flush();
        }

        delay(200);
    }

    vTaskDelete(NULL);
}


int64_t GetTimestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}

/*
* @brief Задача отправки измеренных данных в serial port
*/
void send_measure_data_task(void* pvParameters)
{
    Serial.print("uptime(ms);");
    for (size_t i = 0; i < 3; i++) {
        Serial.printf("ch%d_curr(A);", i + 1);
        Serial.printf("ch%d_ccurr(A);", i + 1);
        Serial.printf("ch%d_v(V);", i + 1);
    }
    Serial.println(" ");

    while (1) {
        static interval_time_t interval_ms = SEND_INTERVAL_DEFAULT_MS;
        xQueueReceive(interval_queue_handle, &interval_ms, 0);

        ina3221_meas ina3221_meas_data;
        xQueuePeek(meas_data_ina3221_queue_handle, &ina3221_meas_data, portMAX_DELAY);  // ждем пока не появятся данные в очереди

        Serial.printf("%09llu;", GetTimestamp());

        for (size_t i = 0; i < 3; i++) {
            Serial.printf("%.3f;", ina3221_meas_data.current[i]);
            Serial.printf("%.3f;", ina3221_meas_data.current_compensated[i]);
            Serial.printf("%.3f;", ina3221_meas_data.voltage[i]);
        }
        Serial.println(" ");

        delay(interval_ms);
    }

    vTaskDelete(NULL);
}


/*
* @brief Задача получения измеренных данных из INA3221
*/
void get_measure_data_ina3221_task(void* pvParameters)
{
    ina3221.begin();
    ina3221.reset();
    ina3221.setShuntRes(INA3221_SHUNT_CH1, INA3221_SHUNT_CH2, INA3221_SHUNT_CH3);

    // Set series filter resistors to 10 Ohm for all channels.
    // Series filter resistors introduce error to the current measurement.
    // The error can be estimated and depends on the resitor values and the bus
    // voltage.
    ina3221.setFilterRes(10, 10, 10);

    while (1) {
        ina3221_meas ina3221_meas_data;

        for (size_t i = 0; i < 3; i++) {
            ina3221_meas_data.current[i] = ina3221.getCurrent((ina3221_ch_t)i);
            ina3221_meas_data.current_compensated[i] = ina3221.getCurrentCompensated((ina3221_ch_t)i);
            ina3221_meas_data.voltage[i] = ina3221.getVoltage((ina3221_ch_t)i);
        }

        xQueueReset(meas_data_ina3221_queue_handle);
        xQueueSend(meas_data_ina3221_queue_handle, &ina3221_meas_data, 0);

        delay(50);
    }

    vTaskDelete(NULL);
}

/*
* @brief Задача отображения измеренных данных на дисплее
*/
void oled_print_task(void* pvParameters)
{
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.display();
    display.dim(true); // перевод на низкую яркость
    delay(100);  // Пауза для инизиализации дисплея
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(20, 16);
    display.println("Power Monitor");

    String fw("FV: ");
    fw += FIRMWARE_STRING;

    display.setCursor(20, 25);
    display.println(fw);
    display.display();
    delay(1000);

    while (1) {
        ina3221_meas ina3221_meas_data;
        xQueuePeek(meas_data_ina3221_queue_handle, &ina3221_meas_data, portMAX_DELAY);  // ждем пока не появятся данные в очереди

        display.clearDisplay();

        for (size_t i = 0; i < 3; i++) {
            display.setCursor(i * 40, 5);
            display.printf("%.3f ", ina3221_meas_data.voltage[i]);
            display.setCursor(i * 40, 15);
            display.printf("%.3f ", ina3221_meas_data.current_compensated[i]);
            display.setCursor(i * 40, 25);
            display.printf("%.3f ", ina3221_meas_data.current_compensated[i] * ina3221_meas_data.voltage[i]);
        }

        display.setCursor(120, 5);
        display.println("V");
        display.setCursor(120, 15);
        display.println("A");
        display.setCursor(120, 25);
        display.println("W");

        display.display();

        delay(50);
    }

    vTaskDelete(NULL);
}
