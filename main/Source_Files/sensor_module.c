#include "sensor_module.h"
#include "common_mode.h"
#include "log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

float Look_Up_table[20][3] = {{5, 207.92, 0},      {10, 253.14, 45.22}, {15, 273.20, 20.06}, {20, 286.57, 13.37}, {25, 296.44, 9.87}, {30, 305.45, 9.01}, {35, 313.98, 8.53},
                              {40, 324.44, 10.46}, {45, 333.24, 8.80},  {50, 341.92, 8.68},  {55, 352.50, 10.59}, {60, 9.76, 17.25},  {65, 24.37, 14.61}, {70, 37.38, 13.02},
                              {75, 55.78, 18.40},  {80, 70.75, 14.97},  {85, 91.44, 20.69},  {90, 116.91, 25.47}, {95, 143.92, 27.01}};

unsigned char wbuffer[4] = {0x00, 0x00, 0x00, 0x00}, rbuffer[10] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

//     Example settings for Ultra-Low Power, Low Power, Fast Mode, and Power Down.
//                        Reg 1      Reg 2      Reg 3      Reg 4
// static const unsigned char ulpm[] = { 0x00,0x05,0x00,0x00}; // ultra low power mode
static const unsigned char lpm[] = {0x00, 0x01, 0x00, 0x00};  // low power mode, 100ms , interrupt disable, temp disable
// static const unsigned char fm[]   = { 0x00,0x06,0x00,0x00}; // fast mode (unsupported)
// static const unsigned char pd[]   = { 0x00,0x01,0x00,0x00}; // power down mode.

static int XSamples[TLV493_Samples], YSamples[TLV493_Samples], ZSamples[TLV493_Samples];
static unsigned char XSampleIndex, YSampleIndex;

esp_err_t ret;

esp_err_t tlv493d_try_read_id(uint8_t addr) {
    uint8_t temp[10] = {0};

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);

    i2c_master_read(cmd, temp, 9, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &temp[9], I2C_MASTER_NACK);

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));

    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
        // MY_LOGD("TLV493D", "Sensor responded at address 0x%02X", addr);
        MY_LOGI("TLV493D", COLOR_ORANGE "Sensor responded at address 0x%02X" COLOR_RESET, addr);

    } else {
        ESP_LOGE("TLV493D", "No response at address 0x%02X: %s", addr, esp_err_to_name(ret));
    }

    return ret;
}
esp_err_t tlv493d_general_reset_select_addr(uint8_t select_5e) {
    /*
       select_5e = 1 -> force/select address 0x5E
       select_5e = 0 -> force/select address 0x1F

       Infineon library logic:
       write 0x00 after general call -> address 0x5E
       write 0xFF after general call -> address 0x1F
    */

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);

    // General call address = 0x00, write mode
    i2c_master_write_byte(cmd, (0x00 << 1) | I2C_MASTER_WRITE, true);

    if (select_5e) {
        i2c_master_write_byte(cmd, 0x00, true);  // select 0x5E
    } else {
        i2c_master_write_byte(cmd, 0xFF, true);  // select 0x1F
    }

    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));

    i2c_cmd_link_delete(cmd);

    vTaskDelay(pdMS_TO_TICKS(20));  // allow sensor to restart

    return ret;
}

esp_err_t TLV493D_Init_Auto(void) {
    esp_err_t ret;

    ret = i2c_master_init();
    if (ret != ESP_OK) {

        ESP_LOGE("TLV493D", "I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    // First try normal Adafruit / already-good case
    Dev_Addr = 0x5E;
    ret = tlv493d_try_read_id(Dev_Addr);

    if (ret == ESP_OK) {

        MY_LOGI("TLV493D", COLOR_BRIGHT_WHITE "Using TLV493D address 0x5E" COLOR_RESET);

        return Configure_TLV493D();
    }

    // Reset and force 0x5E
    ESP_LOGW("TLV493D", "Trying general reset and forcing address 0x5E...");
    tlv493d_general_reset_select_addr(1);

    Dev_Addr = 0x5E;
    ret = tlv493d_try_read_id(Dev_Addr);

    if (ret == ESP_OK) {

        MY_LOGI("TLV493D", COLOR_BRIGHT_WHITE "Using TLV493D address 0x5E" COLOR_RESET);

        return Configure_TLV493D();
    }

    // Reset and force 0x1F
    ESP_LOGW("TLV493D", "Trying general reset and forcing address 0x1F...");
    tlv493d_general_reset_select_addr(0);

    Dev_Addr = 0x1F;
    ret = tlv493d_try_read_id(Dev_Addr);

    if (ret == ESP_OK) {

        MY_LOGI("TLV493D", COLOR_BRIGHT_WHITE "Using TLV493D address 0x1F" COLOR_RESET);

        return Configure_TLV493D();
    }

    ESP_LOGE("TLV493D", "TLV493D not found at 0x5E or 0x1F");
    return ESP_FAIL;
}

esp_err_t i2c_master_init(void) {
    int i2c_master_port = 0;  // I2C_MASTER_NUM;

    gpio_set_direction(I2C_MASTER_SDA_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(I2C_MASTER_SCL_IO, GPIO_MODE_OUTPUT_OD);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        //.clk_flags = 0,          //!< Optional, you can use I2C_SCLK_SRC_FLAG_* flags to choose i2c source clock here. //
    };

    esp_err_t err = i2c_param_config(i2c_master_port, &conf);

    if (err != ESP_OK) {

        MY_LOGE("TLV493D", "i2c_param_config ERROR!");

        return err;
    }
    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

esp_err_t Configure_TLV493D() {  // Configure the TLV493D before communication

    esp_err_t ret;
    unsigned char k;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (Dev_Addr << 1) | I2C_MASTER_READ, true);

    i2c_master_read(cmd, rbuffer, 9, I2C_MASTER_ACK);  // Read first 10 registers
    i2c_master_read_byte(cmd, &rbuffer[9], I2C_MASTER_NACK);

    i2c_master_stop(cmd);

    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));

    i2c_cmd_link_delete(cmd);

    for (k = 0; k < 10; k++) {

        MY_LOGI("TLV493D", COLOR_BRIGHT_WHITE "config data is .... %x\n" COLOR_RESET, rbuffer[k]);
    }

    // Write to registers in order to configure the tlv493D like low power mode etc ...
    wbuffer[0] = 0x00;  // B00000000;

    // Read Register 7H 6:3 -> Write Register 1H 6:3
    wbuffer[1] = rbuffer[7] & 0x78;  // B01111000;

    // Read Register 8H 7:0 -> Write Register 2H 7:0
    wbuffer[2] = rbuffer[8];

    // Read Register 9H 4:0 -> Write Register 3H 4:0 (Mod2)
    wbuffer[3] = rbuffer[9] & 0x1F;  // B00011111;

    // Set Power Mode (ulpm, lpm, fm, pd)
    for (int i = 0; i < sizeof(wbuffer); i++) {

        wbuffer[i] |= lpm[i];  // Set Low power Mode
    }

    cmd = i2c_cmd_link_create();  // Again Create LInk
    i2c_master_start(cmd);        // Start Command

    i2c_master_write_byte(cmd, (Dev_Addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, wbuffer, 4, I2C_MASTER_ACK);  // 4 registers
    i2c_master_stop(cmd);

    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));

    i2c_cmd_link_delete(cmd);  // Delete link

    if (ret != ESP_OK) {

        printf("i2c_master_cmd_begin WRITE BUFFER ERROR ! ");
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // //100 ms Delay

    return ESP_OK;
}

esp_err_t Read_Tlv493D() {  // Read TLV493D Data

    esp_err_t ret;  // ret of type int

    i2c_cmd_handle_t cmd;
    cmd = i2c_cmd_link_create();  // create link
    i2c_master_start(cmd);        // Start Condition

    i2c_master_write_byte(cmd, (Dev_Addr << 1) | I2C_MASTER_READ, true);

    i2c_master_read(cmd, rbuffer, 9, I2C_MASTER_ACK);  // Read first 10 registers
    i2c_master_read_byte(cmd, &rbuffer[9], I2C_MASTER_NACK);

    i2c_master_stop(cmd);  // stop condition

    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));

    i2c_cmd_link_delete(cmd);  // Delete the i2C link

    if (ret != ESP_OK) {  // Means Error in master begin command

        MY_LOGE("TLV493D", "i2c_master_cmd_begin READ ERROR ! ");

        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // 30 Ms Delay

    return ESP_OK;
}

void Read_Sensor_Calc_Average(float *avg_x, float *avg_y) {

    int xy_values[3];

    char samples = 0;

    app_TakeSampleInit();

    do {

        Read_Sensor_TLV493D(&xy_values[0]);  // Read X & Y Values from TLV493D

        if (xy_values[0] != 0 && xy_values[1] != 0) {

            XSamples[XSampleIndex] = xy_values[0];
            YSamples[YSampleIndex] = xy_values[1];

            Take_Samples();  // Take Samples and Store it in an Array
            samples++;
        }

    } while (TLV493_Samples > samples);

    samples = 0;

    Take_Average(avg_x, avg_y);  // Read Averaged Values of X & Y from TLV493D
}

void Take_Samples() {

    int Compare = 0;
    /* Increase X index */

    if (XSampleIndex > 0) {

        /*Compare the two indexes */

        Compare = XSamples[XSampleIndex - 1] - XSamples[XSampleIndex];

        if (Compare < 0) {  // if value is in negative , means previous value is less then the next , increase in the value

            Compare = Compare * (-1);  // Multiply it with (-) to make it (+)
        }
    }

    if (Compare < 6) {  // As compare value always be positive , we multiplied the (-) value with (+)

        // do nothing
    }

    else {  // if greater or equal to 6

        /*Replace the previous index value to the new index value */
        XSamples[XSampleIndex] = XSamples[XSampleIndex - 1];
    }

    if (XSampleIndex < (TLV493_Samples - 1u))  // Increment the index till it reach to maximum limit
    {
        XSampleIndex++;
    } else {
        XSampleIndex = 0u;  // Set the index to 0 when reach to maximum
    }

    /* Increase Y index */

    if (YSampleIndex > 0) {

        /*Compare the two indexes */

        Compare = YSamples[YSampleIndex - 1] - YSamples[YSampleIndex];

        if (Compare < 0) {  // if value is in negative , means previous value is less then the next , increase in the value

            Compare = Compare * (-1);  // Multiply it with (-) to make it (+)
        }
    }
    if (Compare < 6) {  // As compare value always be positive , we multiplied the (-) value with (+)

        // do nothing
    }

    else {  // if greater or equal to 6

        /*Replace the previous index value to the new index value */
        YSamples[YSampleIndex] = YSamples[YSampleIndex - 1];
    }

    if (YSampleIndex < (TLV493_Samples - 1u))  // Increment the index till it reach to maximum limit
    {
        YSampleIndex++;
    } else {
        YSampleIndex = 0u;  // Set the index to 0 when reach to maximum
    }
}

void Take_Average(float *avg_x, float *avg_y) {

    unsigned char i;
    float AverageX, AverageY;
    /* Average Calculation X*/
    AverageX = 0u;
    for (i = 0u; i < TLV493_Samples; i++) {
        AverageX += XSamples[i];
    }
    *avg_x = (float)AverageX / TLV493_Samples;

    /* Average Calculation Y*/
    AverageY = 0u;
    for (i = 0u; i < TLV493_Samples; i++) {
        AverageY += YSamples[i];
    }
    *avg_y = (float)AverageY / TLV493_Samples;
}

int decodeX(int a, int b) {
    /* Shift all bits of register 0 to the left 4 positions.  Bit 8 becomes bit 12.  Bits 0:3 shift in as zero.
     * Determine which of bits 4:7 of register 4 are high, shift them to the right four places -- remask in case
     * they shift in as something other than 0.  bitRead and bitWrite would be a bit more elegant in next version
     * of code.
     */
    int ans = (a << 4) | (((b & 0xF0) >> 4) & 0x0F);
    if (ans > 2047) {

        ans = ans - 2048 * 2;

    }  // Interpret bit 12 as +/-

    return ans;
}

int decodeY(int a, int b) {
    /* Shift all bits of register 1 to the left 4 positions.  Bit 8 becomes bit 12.  Bits 0-3 shift in as zero.
     * Determine which of the first four bits of register 4 are true.  Add to previous answer.
     */

    int ans = (a << 4) | (b & 0x0F);
    if (ans > 2047) {

        ans = ans - 2048 * 2;

    }  // Interpret bit 12 as +/-

    return ans;
}

void app_TakeSampleInit(void) {

    // Clear Sample Index //
    XSampleIndex = 0u;
    YSampleIndex = 0u;

    // Clear Sample Buffer //
    for (unsigned char i = 0; i < TLV493_Samples; i++) {
        XSamples[i] = 0u;
        YSamples[i] = 0u;
    }
}

void Read_Sensor_TLV493D(int *Arr) {

    // static int Arr[3];
    unsigned char data_validity_R5, data_validity_R3;

    ret = Read_Tlv493D();

    if (ret != ESP_OK) {

        Arr[0] = 0;
        Arr[1] = 0;
        Arr[2] = 0;
        return;
    }

    data_validity_R5 = (rbuffer[5] & 0xF0) >> 4;

    data_validity_R3 = (rbuffer[3] & 0x03);

    // Go to decode functions below

    int x = decodeX(rbuffer[0], rbuffer[4]);  // Arrange the data and convert into the 12 bits , For X Values , Because registers dont have the aligned values
    int y = decodeY(rbuffer[1], rbuffer[4]);  // Arrange the data and convert into the 12 bits , For Y Values , Because registers dont have the aligned values

    Arr[0] = x;  // Store data in an array to return the two values x & y
    Arr[1] = y;
    Arr[2] = 0;
    // x_float=(float)x*0.098;                  //formula for conversion , please consult datasheet for more detail
    // y_float=(float)y*0.098;
    // z_float=(float)z*0.098;

    // if(data_validity_R5==0x03 && data_validity_R3==0) {

    //   printf("  Z : %d",z);
    //   printf("\n");
    //   printf("Arr[0] %c",Arr[0]);
    //   printf("Arr[0] %c",Arr[1]);

    /*    printf("data_validity  : %d ",data_validity_R5);
        printf("B0  : %d ",rbuffer[0]);
        printf("  B4 : %d ",rbuffer[4]);
        printf("  B1 :  %d ",rbuffer[1]);
        printf("  x  : %d",x);
        printf("  y : %d",y);
      //  printf("  X : %f",x_float);
      //  printf("  Y : %f",y_float);
       printf("  Radian : %f  ",atan2(x_float,y_float));
       printf("  Degree : %f  ",atan2(x_float,y_float)*57.2958);


        printf("\n");
*/

    // return Arr;

    //   }

    //  printf("Data is Un............... stable \n");
    /* Arr[0]=0;
    Arr[1]=0;
    Arr[2]=0; */

    // return Arr;
}
void Raw_Avg_Data_To_Angle(float x, float y, double *Angle_In) {

    float x_float, y_float, z_float;

    x_float = (float)x * 0.098;  // formula for conversion , please consult datasheet for more detail
    y_float = (float)y * 0.098;

    if ((atan2(x_float, y_float) * 57.2958) < 0) {

        *Angle_In = (atan2(x_float, y_float) * 57.2958) * (-1);

    } else if ((atan2(x_float, y_float) * 57.2958) >= 0) {

        *Angle_In = (360) - (atan2(x_float, y_float) * 57.2958);
    }

    //   MY_LOGD(LOG_TAG_Sensor," Degree    :  %f\n ",*Angle_In);

    MY_LOGI(LOG_TAG_Central, COLOR_ORANGE " Degree    :  %f\n " COLOR_RESET, *Angle_In);
}

unsigned char Angle_to_Percentage(double Angle_Input) {

    // Compare the Angle_Input with All Rows and Column # 1

    float Percentage;
    char Round_off = 0;

    for (unsigned char j = 0; j < 19; j++) {  // rOWS

        if (Look_Up_table[j][1] <= Angle_Input && Look_Up_table[j + 1][1] >= Angle_Input) {

            Percentage = ((Angle_Input - Look_Up_table[j][1]) * 5) / Look_Up_table[j + 1][2];

            Percentage = Look_Up_table[j][0] + Percentage;

            Percentage = (Percentage / 100) * 255;

            //  MY_LOGD(LOG_TAG_Sensor, "Normalization is  %f",Percentage);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "Normalization is  %f" COLOR_RESET, Percentage);

            Round_off = Percentage + 0.5;

            // MY_LOGD(LOG_TAG_Sensor, "Round off   %d \n",Round_off);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "Round off   %d \n" COLOR_RESET, Round_off);

            return Round_off;
        }

        if (Look_Up_table[10][1] <= Angle_Input && Look_Up_table[11][1] <= Angle_Input) {

            Percentage = ((Angle_Input - Look_Up_table[10][1]) * 5) / Look_Up_table[11][2];

            Percentage = Look_Up_table[10][0] + Percentage;

            //  MY_LOGD(LOG_TAG_Sensor, "  Percentage is   :%f",Percentage);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "  Percentage is   :%f" COLOR_RESET, Percentage);

            Percentage = (Percentage / 100) * 255;

            //  MY_LOGD(LOG_TAG_Sensor, "Normalization is  %f",Percentage);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "Normalization is  %f" COLOR_RESET, Percentage);

            Round_off = Percentage + 0.5;

            // MY_LOGD(LOG_TAG_Sensor, "Round off   %d \n",Round_off);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "Round off   %d \n" COLOR_RESET, Round_off);

            return Round_off;
        }

        if (Look_Up_table[10][1] >= Angle_Input && Look_Up_table[11][1] >= Angle_Input) {

            Percentage = ((Angle_Input + 360 - Look_Up_table[10][1])) * 5 / Look_Up_table[11][2];

            Percentage = Look_Up_table[10][0] + Percentage;

            // MY_LOGD(LOG_TAG_Sensor, "  Percentage is   :%f",Percentage);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE " Percentage is   :%f" COLOR_RESET, Percentage);

            Percentage = (Percentage / 100) * 255;

            // MY_LOGD(LOG_TAG_Sensor, "Normalization is  %f",Percentage);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE " Normalization is  :%f" COLOR_RESET, Percentage);

            Round_off = Percentage + 0.5;

            // MY_LOGD(LOG_TAG_Sensor, "Round off   %d \n",Round_off);

            MY_LOGI(LOG_TAG_Central, COLOR_ORANGE "Round off   %d \n" COLOR_RESET, Round_off);

            return Round_off;
        }
    }

    return 0;
}
