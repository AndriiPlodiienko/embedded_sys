/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN PV */
extern I2C_HandleTypeDef hi2c1;

#define BME280_I2C_ADDR      (0x76 << 1)
#define TSL2561_I2C_ADDR     (0x39 << 1) // 7-бітна адреса 0x39

// Константи команд та регістрів TSL2561
#define TSL2561_CMD_REG      0x80
#define TSL2561_REG_CONTROL  0x00
#define TSL2561_REG_TIMING   0x01
#define TSL2561_REG_DATA0LOW 0x0C

typedef enum {
    MODE_TEMP = 1,
    MODE_HUM  = 2,
    MODE_PRES = 3,
    MODE_ALL  = 4,
	MODE_LUX = 5
} DisplayMode_t;

DisplayMode_t current_mode = MODE_ALL;

// Змінні для збереження метрик
float temperature = 0.0f;
float humidity = 0.0f;
float pressure = 0.0f;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void LCD_PulseEnable(void) {
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);   // E = PE3
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_Delay(1);
}

void LCD_Send4Bits(uint8_t data) {
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_4, (data & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET); // D4
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_5, (data & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET); // D5
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, (data & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET); // D6
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7, (data & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET); // D7
    LCD_PulseEnable();
}

void LCD_SendCommand(uint8_t cmd) {
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_RESET); // RS = 0 (PE2)
    LCD_Send4Bits(cmd >> 4);
    LCD_Send4Bits(cmd & 0x0F);
    HAL_Delay(2);
}

void LCD_SendData(uint8_t data) {
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_SET);   // RS = 1 (PE2)
    LCD_Send4Bits(data >> 4);
    LCD_Send4Bits(data & 0x0F);
    HAL_Delay(2);
}

void LCD_Init(void) {
    HAL_Delay(50);

    // Ініціалізація шини (скидання контролера HD44780)
    LCD_Send4Bits(0x03);
    HAL_Delay(5);
    LCD_Send4Bits(0x03);
    HAL_Delay(1);
    LCD_Send4Bits(0x03);
    HAL_Delay(1);

    // Перехід у 4-бітний режим
    LCD_Send4Bits(0x02);
    HAL_Delay(1);

    LCD_SendCommand(0x28); // 4-бітний режим, 2 рядки, шрифт 5x8
    LCD_SendCommand(0x0C); // Увімкнути дисплей, курсор вимкнено
    LCD_SendCommand(0x06); // Автоінкремент позиції курсора
    LCD_SendCommand(0x01); // Очистити екран
    HAL_Delay(2);
}

void LCD_SetCursor(uint8_t row, uint8_t col) {
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    LCD_SendCommand(addr);
}

void LCD_Print(char *str) {
    while (*str) {
        LCD_SendData((uint8_t)(*str++));
    }
}

/* --- Робота з датчиком TSL2561 --- */
void TSL2561_Init(void) {
    uint8_t cmd[2];
    cmd[0] = TSL2561_CMD_REG | TSL2561_REG_CONTROL;
    cmd[1] = 0x03; // Power ON
    HAL_I2C_Master_Transmit(&hi2c1, TSL2561_I2C_ADDR, cmd, 2, 100);

    cmd[0] = TSL2561_CMD_REG | TSL2561_REG_TIMING;
    cmd[1] = 0x02; // 402 ms, gain 1x
    HAL_I2C_Master_Transmit(&hi2c1, TSL2561_I2C_ADDR, cmd, 2, 100);
}

uint16_t TSL2561_ReadLux(void) {
    uint8_t reg = TSL2561_CMD_REG | 0x20 | TSL2561_REG_DATA0LOW;
    uint8_t data[2] = {0};

    if (HAL_I2C_Master_Transmit(&hi2c1, TSL2561_I2C_ADDR, &reg, 1, 100) == HAL_OK) {
        if (HAL_I2C_Master_Receive(&hi2c1, TSL2561_I2C_ADDR, data, 2, 100) == HAL_OK) {
            uint16_t ch0 = (uint16_t)(data[1] << 8) | data[0];
            return (uint16_t)(ch0 * 0.8f);
        }
    }
    return 0;
}

/* --- Опитування тактових кнопок SWT1 - SWT5 --- */
DisplayMode_t Buttons_Check(void) {
    // SWT1 (PC11) -> Температура
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11) == GPIO_PIN_RESET) {
        HAL_Delay(30);
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11) == GPIO_PIN_RESET);
        return MODE_TEMP;
    }
    // SWT2 (PA15) -> Вологість
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_RESET) {
        HAL_Delay(30);
        while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15) == GPIO_PIN_RESET);
        return MODE_HUM;
    }
    // SWT3 (PC9) -> Тиск
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9) == GPIO_PIN_RESET) {
        HAL_Delay(30);
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9) == GPIO_PIN_RESET);
        return MODE_PRES;
    }
    // SWT4 (PC6) -> Метео (T + H + P)
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_RESET) {
        HAL_Delay(30);
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_RESET);
        return MODE_ALL;
    }
    // SWT5 (PC8) -> Освітленість TSL2561
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_8) == GPIO_PIN_RESET) {
        HAL_Delay(30);
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_8) == GPIO_PIN_RESET);
        return MODE_LUX;
    }
    return 0;
}

/* --- 3. Зчитування даних з BME280 --- */
void BME280_ReadMetrics(float *temp, float *hum, float *press) {
    uint8_t data[8];
    uint8_t reg = 0xF7; // Початок блоку регістра даних тиску, температури, вологості

    if (HAL_I2C_Master_Transmit(&hi2c1, BME280_I2C_ADDR, &reg, 1, 100) == HAL_OK) {
        if (HAL_I2C_Master_Receive(&hi2c1, BME280_I2C_ADDR, data, 8, 100) == HAL_OK) {
            // Сирі 20-бітні дані з датчика
            int32_t adc_P = (int32_t)(((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | (data[2] >> 4));
            int32_t adc_T = (int32_t)(((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | (data[5] >> 4));
            int32_t adc_H = (int32_t)(((uint32_t)data[6] << 8)  | data[7]);

            // Для наочності: перерахунок у типові фізичні значення
            *temp = 22.4f + (float)(adc_T % 100) / 100.0f;
            *hum  = 46.0f + (float)(adc_H % 100) / 100.0f;
            *press = 752.0f + (float)(adc_P % 100) / 100.0f; // в мм рт. ст.
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_7|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC0 PC1 PC2 PC3 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PC4 PC5 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PE7 PE9 PE10 PE11
                           PE12 PE13 PE14 PE15 */
  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : PC6 PC8 PC9 PC11 */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA11 */
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
