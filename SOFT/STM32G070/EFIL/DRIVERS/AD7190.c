#include "AD7190.h"     // AD7190 definitions.

#include "cmsis_os2.h"
#include "main.h"
#include "spi.h"
#include "gpio.h"
#include "math.h"



#define CS_L	HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);
#define CS_H	HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET);
extern uint8_t data_rdy;
uint8_t *spi1dr;

GPIO_TypeDef* AD7190_RDY_PORT = NULL;
uint16_t AD7190_RDY_PIN = 0;
/***************************************************************************//**
 * @brief Writes data into a register.
 *
 * @param registerAddress - Address of the register.
 * @param registerValue - Data value to write.
 * @param bytesNumber - Number of bytes to be written.
 *
 * @return none.
*******************************************************************************/
void AD7190_SetRegisterValue(uint8_t registerAddress, uint32_t registerValue, uint8_t bytesNumber) {
  uint8_t writeCommand[5] = {0, 0, 0, 0, 0};
  uint8_t* dataPointer = (uint8_t*)&registerValue;
  uint8_t bytesNr = bytesNumber;

  writeCommand[0] = AD7190_COMM_WRITE | AD7190_COMM_ADDR(registerAddress);
  while(bytesNr > 0) {
    writeCommand[bytesNr] = *dataPointer;
    dataPointer ++;
    bytesNr --;
  }
  AD7190_WriteBuff(writeCommand, bytesNumber + 1);
}

/***************************************************************************//**
 * @brief Reads the value of a register.
 *
 * @param registerAddress - Address of the register.
 * @param bytesNumber - Number of bytes that will be read.
 *
 * @return buffer - Value of the register.
*******************************************************************************/
uint32_t AD7190_GetRegisterValue(uint8_t registerAddress, uint8_t bytesNumber) {
  uint8_t registerWord[5] = {0, 0, 0, 0}; 
  uint32_t buffer = 0;
//	uint8_t ee = AD7190_COMM_READ | AD7190_COMM_ADDR(registerAddress);
  AD7190_WriteByte( AD7190_COMM_READ | AD7190_COMM_ADDR(registerAddress));			// Отправка команды
  AD7190_ReadBuff(registerWord, bytesNumber);										   // Получение данных
  for(uint8_t i = 0; i < bytesNumber; i++) {										  // Загрузка в регистр
      buffer = (buffer << 8) + registerWord[i];
  }
  return buffer;
}

/***************************************************************************//**
 * @brief Checks if the AD7190 part is present.
 *
 * @return status - Indicates if the part is present or not.
*******************************************************************************/
uint8_t AD7190_Init(void) {

  AD7190_Reset();
  /* Allow at least 500 us before accessing any of the on-chip registers. */
  delay_ms(500);
  if ((AD7190_GetRegisterValue(AD7190_REG_ID, 1) & AD7190_ID_MASK) 
	  != ID_AD7190)
  {
	  return 0;
  }
  return 1;
}

/***************************************************************************//**
 * @brief Resets the device.
 *
 * @return none.
*******************************************************************************/
void AD7190_Reset(void) {
  uint8_t registerWord[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  AD7190_WriteBuff(registerWord, 7);
}

/***************************************************************************//**
 * @brief Set device to idle or power-down.
 *
 * @param pwrMode - Selects idle mode or power-down mode.
 *                  Example: 0 - power-down
 *                           1 - idle
 *
 * @return none.
*******************************************************************************/
void AD7190_SetPower(uint8_t pwrMode) {
  uint32_t oldPwrMode = 0x0;
  uint32_t newPwrMode = 0x0;
  oldPwrMode = AD7190_GetRegisterValue(AD7190_REG_MODE, 3);
  oldPwrMode &= ~(AD7190_MODE_SEL(0x7));
  newPwrMode = oldPwrMode | AD7190_MODE_SEL((pwrMode * (AD7190_MODE_IDLE)) | (!pwrMode * (AD7190_MODE_PWRDN)));
  AD7190_SetRegisterValue(AD7190_REG_MODE, newPwrMode, 3);
}

/***************************************************************************//**
 * @brief Waits for RDY pin to go low.
 *
 * @return none.
*******************************************************************************/
void AD7190_WaitRdyGoLow(void) {
  uint32_t timeOutCnt = 6000000;
	uint32_t time;
	time = HAL_GetTick();
//	ff = HAL_GPIO_ReadPin(RDY_GPIO_Port, RDY_Pin);
//while((MISO_GPIO_Port->IDR & MISO_Pin) && timeOutCnt--) { }
	while ((HAL_GPIO_ReadPin(AD7190_RDY_PORT, AD7190_RDY_PIN)) && timeOutCnt--)
 {
	 __NOP();
 }
//while ((!HAL_GPIO_ReadPin(MISO_GPIO_Port, MISO_Pin)) && timeOutCnt--) {}
	time = HAL_GetTick() - time;
	__NOP();
}

/***************************************************************************//**
 * @brief Selects the channel to be enabled.
 *
 * @param channel - Selects a channel.
 *  
 * @return none.
*******************************************************************************/
void AD7190_ChannelSelect(uint16_t channel) {
  uint32_t oldRegValue = 0x0;
  uint32_t newRegValue = 0x0;   

  oldRegValue = AD7190_GetRegisterValue(AD7190_REG_CONF, 3);
  oldRegValue &= 0xFFFF00FF;
  newRegValue = oldRegValue | AD7190_CONF_CHAN(channel);   
  AD7190_SetRegisterValue(AD7190_REG_CONF, newRegValue, 3);
	osDelay(20);
}

/***************************************************************************//**
 * @brief Performs the given calibration to the specified channel.
 *
 * @param mode - Calibration type.
 * @param channel - Channel to be calibrated.
 *
 * @return none.
*******************************************************************************/
void AD7190_Calibrate(uint8_t mode) {
  uint32_t oldRegValue = 0x0;
  uint32_t newRegValue = 0x0;

  oldRegValue = AD7190_GetRegisterValue(AD7190_REG_MODE, 3);
  oldRegValue &= ~AD7190_MODE_SEL(0x7);
  newRegValue = oldRegValue | AD7190_MODE_SEL(mode);

  AD7190_SetRegisterValue(AD7190_REG_MODE, newRegValue, 3);
  AD7190_WaitRdyGoLow();
}

/***************************************************************************//**
 * @brief Selects the polarity of the conversion and the ADC input range.
 *
 * @param polarity - Polarity select bit. 
                     Example: 0 - bipolar operation is selected.
                              1 - unipolar operation is selected.
* @param range - Gain select bits. These bits are written by the user to select 
                 the ADC input range.     
 *
 * @return none.
*******************************************************************************/
void AD7190_RangeSetup(uint8_t polarity, uint8_t range) {
  uint32_t oldRegValue = 0x0;
  uint32_t newRegValue = 0x0;

  oldRegValue = AD7190_GetRegisterValue(AD7190_REG_CONF,3);  
  oldRegValue &= ~(AD7190_CONF_UNIPOLAR | AD7190_CONF_GAIN(0x7));
  newRegValue = oldRegValue | (polarity * AD7190_CONF_UNIPOLAR) | AD7190_CONF_GAIN(range);
  AD7190_SetRegisterValue(AD7190_REG_CONF, newRegValue, 3);
}

/***************************************************************************//**
 * @brief Returns the result of a single conversion.
 *
 * @return regData - Result of a single analog-to-digital conversion.
*******************************************************************************/
 uint32_t AD7190_SingleConversion(void) {
  uint32_t command = 0x0;
  uint32_t regData = 0x0;
	
 command = AD7190_MODE_SEL(AD7190_MODE_SINGLE)
		   | AD7190_MODE_CLKSRC(AD7190_CLK_INT)			// внутренний кварц
	//	   | AD7190_MODE_SINC3							// SINC3 Enable (если выключено то SINC4)
		   | AD7190_MODE_DAT_STA						// доп. инфа в результатах 
		   | AD7190_MODE_RATE(4800 / 50);				// rate 96 имхо оптимально 12.5 Гц
 AD7190_SetRegisterValue(AD7190_REG_MODE, command, 3);
 //osDelay(10);

// AD7190_WaitRdyGoLow();
// AD7190_WriteByte(AD7190_COMM_READ | AD7190_COMM_ADDR(AD7190_REG_DATA));
// regData = AD7190_GetRegisterValue(AD7190_REG_DATA, 4);

  return regData;
}

/***************************************************************************//**
 * @brief Returns the average of several conversion results.
 *
 * @return samplesAverage - The average of the conversion results.
*******************************************************************************/
uint32_t AD7190_ContinuousReadAvg(uint8_t sampleNumber) {
  uint32_t samplesAverage = 0x0;
  uint8_t count = 0x0;
  uint32_t command = 0x0;

  command = AD7190_MODE_SEL(AD7190_MODE_CONT) | AD7190_MODE_CLKSRC(AD7190_CLK_INT) | AD7190_MODE_RATE(4800/50);
  AD7190_SetRegisterValue(AD7190_REG_MODE, command, 3);
  for(count = 0;count < sampleNumber;count ++) {
    AD7190_WaitRdyGoLow();
    samplesAverage += AD7190_GetRegisterValue(AD7190_REG_DATA, 3);
  }
  samplesAverage = samplesAverage / sampleNumber;

  return samplesAverage;
}

/***************************************************************************//**
 * @brief Read data from temperature sensor and converts it to Celsius degrees.
 *
 * @return temperature - Celsius degrees.
*******************************************************************************/
uint32_t AD7190_TemperatureRead(void) {
  uint8_t temperature = 0x0;
  uint32_t dataReg = 0x0;

  AD7190_RangeSetup(0, AD7190_CONF_GAIN_1);
  AD7190_ChannelSelect(AD7190_CH_TEMP_SENSOR);
  dataReg = AD7190_SingleConversion();
  dataReg -= 0x800000;
  dataReg /= 2815;   // Kelvin Temperature
  dataReg -= 273;    //Celsius Temperature
  temperature = (uint32_t) dataReg;

  return temperature;
}
void AD7190_ReadBuff(uint8_t* pdata, uint16_t size_pdata) 
{
	HAL_SPI_Receive(&hspi1, pdata, size_pdata, HAL_MAX_DELAY);
}

// запись массива в микросхему W5500
void AD7190_WriteBuff(uint8_t* buff, uint16_t len) 
{
	for (uint16_t i = 0; i < len; i++) { AD7190_WriteByte(buff[i]); }
}

// принять байт через SPI
uint8_t AD7190_ReadByte(void)
{
	uint8_t data = 0;
	HAL_SPI_Receive(&hspi1, &data, 1, HAL_MAX_DELAY);
	return data;
}
 
// передать байт через SPI
void AD7190_WriteByte(uint8_t b) 
{
	HAL_SPI_Transmit(&hspi1, &b, 1, HAL_MAX_DELAY);
}

// Реализация ххх-мсек задержки
void delay_ms(uint16_t ms)
{
	osDelay(ms);
//	HAL_Delay(ms);
	// delayTimerValue = ms;
	//while (delayTimerValue) {__NOP(); }
}

 float getWeight (void)
{

}
float conv_bin_to_float(uint32_t value)
{
	uint8_t i = 29;
	float tmp_result = 0;
	
	do
	{
		if (value & (1 << i)) {tmp_result += powf(2, (i - 24)); }
		i--;
	} while (i);
	
	return tmp_result;
}
uint32_t conv_float_to_bin(float value)
{
	uint8_t i;
	uint32_t tmp_result;
	float tmp_value;
  
	tmp_value = value;
	i = 24;
	tmp_result = tmp_value;
	tmp_value = tmp_value - tmp_result;
	tmp_result = tmp_result << i;
	do
	{
		i--;
		tmp_value = tmp_value * 2;
		if (tmp_value >= 1) 
		{
			tmp_result |= (1 << i);
			tmp_value = tmp_value - 1;
		}
		if (tmp_value == 0) {break;}
	} while (i);
  
	return tmp_result;
}
uint32_t AD7190_calibrate_Zero_Offset(uint8_t count)
{
	uint32_t tmpdt,sum;
//	sum = AD7190_GetRegisterValue(AD7190_REG_FULLSCALE,  3);

	AD7190_WaitRdyGoLow();
	AD7190_WriteByte(AD7190_COMM_READ | AD7190_COMM_ADDR(AD7190_REG_DATA));  // Заканчиваем преобразование

	AD7190_SetPower(SET_IDLE); // Сброс установок
	AD7190_SetRegisterValue(AD7190_REG_OFFSET, DEFAULT_OFFSET_AD7190, 3);
	AD7190_SetRegisterValue(AD7190_REG_FULLSCALE, DEFAULT_FULLSCALE_AD7190, 3);

	for (uint8_t i = 0; i < count; i++) {		// Получаем 50 значений и считаем среднее далее
		osDelay(10);
		AD7190_Calibrate(AD7190_MODE_CAL_SYS_ZERO);								// Получаем новые значения регистра OFFSET
		tmpdt = AD7190_GetRegisterValue(AD7190_REG_OFFSET, 3);
		sum += tmpdt;
				
		} 											
	//======================== получаем нулевое смещение ==============================
	sum /= count; // Было 50 циклов

	AD7190_SetPower(SET_IDLE); 
	AD7190_SetRegisterValue(AD7190_REG_OFFSET, sum, 3); // Заносим в регистр новое значение смещения

	return sum;
}
uint32_t AD7190_calibrate_FullScale(uint8_t count,float cal_wgt)
{
	uint8_t tmpkt[4], adc_status;
	uint32_t tmpdt = 0;
	uint32_t sum = 0;
	uint32_t kADC;

	uint32_t command = AD7190_MODE_SEL(AD7190_MODE_CONT) | AD7190_MODE_CLKSRC(AD7190_CLK_INT) | AD7190_MODE_RATE(4800 / 100) | AD7190_MODE_DAT_STA;
	AD7190_SetRegisterValue(AD7190_REG_MODE, command, 3);


	for (uint8_t i = 0; i <= count; i++) // ПОЛУЧЕНИЕ ЗНАЧЕНИЯ С НАГРУЗКОЙ 100 раз
	{
		
		AD7190_WaitRdyGoLow();
		tmpdt = 0;
		AD7190_WriteByte(AD7190_COMM_READ | AD7190_COMM_ADDR(AD7190_REG_DATA));
		// Инициализация начальных значений
//		j = params.adr_device >> 4; // Определяем сколько бит шумы
		tmpdt = 0; // Установка в начальное значение
		tmpdt = tmpkt[0] << 16 | tmpkt[1] << 8 | tmpkt[2];
		tmpdt -= 0x00800000; // Преобразовываем в Int
		tmpdt = tmpdt >> 4; // Уменьшаем на количество незначимых битов
//		tmpdt -= params.adc_null; // Учитываем смещение -0-
		sum += tmpdt; // Результат в final_result
		
	}
	sum /= 50; // Так как начальный к-т = 0.5 (100 * 0.5 = 50)
	

	kADC = conv_float_to_bin((float) cal_wgt / (float) sum);
	//		kADC = conv_float_to_bin( kADCf);

	AD7190_WaitRdyGoLow();
	AD7190_SetRegisterValue(AD7190_REG_MODE, AD7190_MODE_SEL(AD7190_MODE_IDLE), 3);
	osDelay(50);
	AD7190_SetRegisterValue(AD7190_REG_FULLSCALE, kADC, 3);

	return kADC;
}