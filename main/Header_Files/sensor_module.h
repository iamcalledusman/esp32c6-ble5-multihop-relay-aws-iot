#ifndef SENSOR_MODULE_H
#define SENSOR_MODULE_H

#include "driver/i2c.h"
#include "esp_log.h"

#define TLV493_Samples 20
#define Thirty_Sec 2  // 1*2  = 2 secs
#define Six_Hour 3    // 1*3 =  3 secs

#define I2C_MASTER_SCL_IO 13        /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO 12        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM I2C_NUM_0    /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ 100000   /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0 /*!< I2C master doesn't need buffer */
// const  uint8_t Dev_Addr =  0x5E;  // REG_MAG =0x00
static uint8_t Dev_Addr = 0x5E;  // REG_MAG =0x00

/* extern unsigned char wbuffer[4],rbuffer[10];

//     Example settings for Ultra-Low Power, Low Power, Fast Mode, and Power Down.
//                        Reg 1      Reg 2      Reg 3      Reg 4
extern const unsigned char ulpm[]; // ultra low power mode
extern const unsigned char lpm[]; // low power mode, 100ms , interrupt disable, temp disable
extern const unsigned char fm[]; // fast mode (unsupported)
extern const unsigned char pd[]; // power down mode.

extern int XSamples[TLV493_Samples],YSamples[TLV493_Samples],ZSamples[TLV493_Samples];
extern unsigned char XSampleIndex,YSampleIndex;
extern float AverageX,AverageY;
extern float x_float,y_float,z_float;
extern float Look_Up_table[20][3];

extern float Percentage;
extern unsigned char Round_off;
extern double Angle_Input;
 */
extern esp_err_t ret;

// #define SENSOR_ADDR                 0x40       /*!< I2C address of the sensor */
// #define SENSOR_REG_DATA             0x00       /*!< Register address for sensor data */

void Take_Samples();
void Take_Average(float *avg_x, float *avg_y);
esp_err_t TLV493D_Init_Auto(void);
esp_err_t tlv493d_try_read_id(uint8_t addr);
esp_err_t tlv493d_general_reset_select_addr(uint8_t select_5e);
esp_err_t i2c_master_init(void);
esp_err_t Configure_TLV493D(void);
esp_err_t Read_Tlv493D(void);
int decodeX(int a, int b);
int decodeY(int a, int b);
void Read_Sensor_Calc_Average(float *avg_x, float *avg_y);
// esp_err_t tlv493d_write_register(uint8_t reg_addr, uint8_t data);

void Raw_Avg_Data_To_Angle(float x, float y, double *Angle_Input);
unsigned char Angle_to_Percentage(double Angle_Input);
void Read_Sensor_TLV493D(int *Arr);
void app_TakeSampleInit(void);

#endif
