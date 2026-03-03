/* 
 * ESP32-based wireless pressure and temperature monitoring node
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Seeed_BMP280.h>
#include "time.h"

// set to false for less serial debug messages
#define DEBUG true

// the interval at which to display sensor data on the LCD (in ms)
#define DATA_DISP_INTERVAL  500
// the interval at which to update status mode display (in ms)
#define STAT_DISP_INTERVAL  500
// the interval at which to read data for local storage (in ms)
#define DATA_READ_INTERVAL  (15*1000)
// the interval at which to transmit the locally stored data to the server (in ms)
#define DATA_SEND_INTERVAL  (120*1000)
// amount of data points to store locally
#define STORED_DATA_LEN     ((DATA_SEND_INTERVAL/DATA_READ_INTERVAL)+1)

// UTC offset and UTC offset for daylight saving (used for time config via NTP)
#define UTC_OFFSET      0   // use UTC time
#define UTC_OFFSET_DST  0   // no daylight saving

#define LCD_ONOFF_BUTTON    26
#define DISP_STATUS_BUTTON  25

// BMP280 pressure and temperature sensor
BMP280 bmp280;

// LCD I2C address: 0x27 (make sure this is different from the pressure&temp sensor's address).
LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* wifi_ssid = "SSID";    // set to WiFi network name
const char* wifi_pass = "PASS";     // set to WiFi password

const char* servername = "http://your-website.org/weather_monitor/logger.cgi";  // set to URL of the data logger (server side)
const char* api_key = "123";    // set to same API key as checked on the server
const char* sensorname = "SENSOR1"; // name of this sensor

const char * ntp_server = "pool.ntp.org";   // NTP server for time config

unsigned long last_data_read_time = 0;
unsigned long last_data_disp_time = 0;
unsigned long last_stat_disp_time = 0;
unsigned long last_data_send_time = 0;

float pres_data[STORED_DATA_LEN] = {};
float temp_data[STORED_DATA_LEN] = {};
unsigned long time_data[STORED_DATA_LEN] = {};
int data_index = 0;

unsigned int successful_sent_count = 0;
unsigned int failed_sent_count = 0;

bool lcd_on = true;
bool disp_status_mode = false;

/* procedure for printing a short message to the LCD and a longer one
 * to the serial terminal */
void printMsg(char msg_short[], char msg_long[]){
    Serial.println(msg_long);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(msg_short);
}

/* procedure to display pressure and temperature on the LCD display */
void dispData(float pres, float temp){
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(pres);
    lcd.setCursor(14,0);
    lcd.print("Pa");
    lcd.setCursor(0,1);
    lcd.print(temp);
    lcd.setCursor(11,1);
    lcd.print("deg.C");
}

/* procedure for displaying system status (WiFi connectivity and uptime) */
void dispSystemStatus(){
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi:");
    if(WiFi.status() == WL_CONNECTED){
        lcd.print("CONNECTED");
    }else{
        lcd.print("DISCONNEC");
    }
    lcd.setCursor(0, 1);
    lcd.print("uptime:");
    lcd.print(millis()/1000/60);
    lcd.print(" mins");
}

/* function for sending the sensor data over to the server via HTTP */
int sendDataToServer(unsigned int entries){
    #if DEBUG == true
        // print data over serial (for debug)
        Serial.println("DATA");
        for(int i = 0; i < entries; i++){
            Serial.print("\t");
            Serial.print(time_data[i]);
            Serial.print(" ");
            Serial.print(pres_data[i]);
            Serial.print(" ");
            Serial.println(temp_data[i]);
        }
    #endif

    WiFiClient wificlient;
    HTTPClient httpclient;
    
    httpclient.begin(wificlient, servername);
    
    httpclient.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // add meta data to the request string
    String request_data = "api_key=" + String(api_key) +
                          "&sensor_name=" + String(sensorname) +
                          "&entries=" + String(entries) +
                          "&successful_sent_count=" + String(successful_sent_count) +
                          "&failed_sent_count=" + String(failed_sent_count) + "&";
                          
    // add sensor data to the request string
    for(int i = 0; i < entries; i++){
        request_data.concat("time" + String(i) + "=" + String(time_data[i]) + "&");
        request_data.concat("pres" + String(i) + "=" + String(pres_data[i]) + "&");
        request_data.concat("temp" + String(i) + "=" + String(temp_data[i]) + ((i == entries-1) ? "" : "&"));   // do not append '&' to the last data entry
    }

    Serial.print("sending data to server...");

    unsigned long send_begin_time = millis();
    int response = httpclient.POST(request_data);   // note: negative response value means connection error
    unsigned long send_end_time = millis();
    
    Serial.println("done");
    Serial.print("response: ");
    Serial.print(response);
    Serial.print(" (took ");
    Serial.print(send_end_time-send_begin_time);
    Serial.println(" ms)");
    Serial.println(httpclient.getString());

    httpclient.end();

    return response;
}

/* function for obtaining the current epoch time */
time_t getEpochTime(){
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        return 0;
    }else{
        time_t time_now;
        time(&time_now);
        return time_now;
    }
}

/* function for obtaining button input including debounce time */
int digitalReadDebounced(int but){
    unsigned long last_stable_time = millis();
    int last_state = digitalRead(but);
    while(1){
        int current_state = digitalRead(but);
        if(current_state != last_state){
            last_state = current_state;
            last_stable_time = millis();
        }else if(millis()-last_stable_time > 50){    // set debounce time here (in ms)
            return current_state;
        }
    }
}

void setup() {
    pinMode(LCD_ONOFF_BUTTON, INPUT);
    pinMode(DISP_STATUS_BUTTON, INPUT);

    // initialize serial communication for debug
    Serial.begin(115200);

    // initialize LCD display
    lcd.init();
    lcd.backlight();

    printMsg("starting...", "initializing the system...");

    // initialize sensor and check for errors
    if(!bmp280.init()){
        printMsg("sensor error", "Error: Sensor not connected or broken.");
    }

    // initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // TODO: check if our desired network exists

    Serial.print("connecting to WiFi...");

    // connect to the WiFi network
    WiFi.begin(wifi_ssid, wifi_pass);

    // keep checking if connection success and restart board if taking too long
    unsigned long connect_start = millis();
    while(true){
        if(WiFi.status() == WL_CONNECTED){
            Serial.println("done");
            break;
        }
        if(millis()-connect_start > 10000){ // wifi connection attemp timeout after 10s
            Serial.println("timeout, restarting");
            ESP.restart();
        }
        /*// if status display button is pressed, continue without waiting to connect to WiFi
        if(digitalReadDebounced(DISP_STATUS_BUTTON) == HIGH){
            Serial.println("continuing without waiting for a connection");
            break;
        }*/
    }

    Serial.print("syncing with NTP server...");

    // configure time by using NTP server
    configTime(UTC_OFFSET, UTC_OFFSET_DST, ntp_server);

    // keep checking if sync with NTP server was successful and restart board if taking too long
    unsigned long ntp_sync_start = millis();
    while(true){
        if(getEpochTime()){
            Serial.println("done");
            break;
        }
        if(millis()-ntp_sync_start > 10000){    // NTP sync attempt timeout after 10s
            Serial.println("timeout, restarting");
            ESP.restart();
        }
    }

    printMsg("system up", "system up");
    delay(700);

    // update read/datadisp/send times to sync with program startup time
    last_data_read_time = millis();
    last_data_disp_time = millis();
    last_data_send_time = millis();
}

void loop() {

    /* logic for processing data read/display/send as well as display update intervals */
    
    if(millis()-last_data_read_time > DATA_READ_INTERVAL){
        #if DEBUG == true
            Serial.print("reading data at ");
            Serial.println(data_index);
        #endif
        // read pressure, temperature, and time values and store them locally
        pres_data[data_index] = bmp280.getPressure();
        temp_data[data_index] = bmp280.getTemperature();
        time_data[data_index] = getEpochTime();
        data_index = (data_index < STORED_DATA_LEN-1) ? data_index+1 : 0; // reset data_index to 0 if overflow
        last_data_read_time = millis();
    }
    if(millis()-last_data_disp_time > DATA_DISP_INTERVAL && lcd_on && !disp_status_mode){
        // display the current pressure and temperature values on the LCD display
        dispData(bmp280.getPressure(), bmp280.getTemperature());
        last_data_disp_time = millis();
    }
    if(millis()-last_stat_disp_time > STAT_DISP_INTERVAL && lcd_on && disp_status_mode){
        // update system status display
        dispSystemStatus();
        last_stat_disp_time = millis();
    }
    if(millis()-last_data_send_time > DATA_SEND_INTERVAL){
        // send the locally stored pressure and temperature data over to the server
        if(WiFi.status() == WL_CONNECTED){
            int response = sendDataToServer(data_index);
            if(response == 200){
                successful_sent_count++;
            }else{
                failed_sent_count++;
            }
        }else{
            failed_sent_count++;
        }
        data_index = 0;
        last_data_send_time = millis();
    }

    /* logic for processing button inputs */
    
    if(digitalReadDebounced(LCD_ONOFF_BUTTON) == HIGH){
        // toggle LCD ON/OFF
        if(lcd_on){
            lcd.noDisplay();
            lcd.noBacklight();
            lcd_on = false;
        }else{
            lcd.display();
            lcd.backlight();
            lcd_on = true;
        }
    }
    if(digitalReadDebounced(DISP_STATUS_BUTTON) == HIGH){
        // toggle between status display and data display modes
        if(!disp_status_mode){
            dispSystemStatus();
            disp_status_mode = true;
        }else{
            dispData(bmp280.getPressure(), bmp280.getTemperature());    // display current pres and temp to wipe status screen
            disp_status_mode = false;
        }
    }
}
