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
#include<string.h>
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
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define BUFFER_SIZE 64
uint8_t rx_data;

uint8_t rx_buffer[BUFFER_SIZE];

volatile uint8_t head = 0;
volatile uint8_t tail = 0;

volatile uint32_t overflow_count = 0;

volatile uint32_t uart_error_count = 0;

volatile uint32_t uart_ore_count = 0;
volatile uint32_t uart_fe_count = 0;
volatile uint32_t uart_ne_count = 0;
volatile uint32_t uart_pe_count = 0;

volatile uint8_t uart_recovery_pending = 0;

volatile uint32_t uart_restart_fail_count = 0;
volatile uint32_t uart_recovery_count = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
int RingBuffer_Push(uint8_t data);
int RingBuffer_Pop(uint8_t *data);
void UART_PrintStats(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_StatusTypeDef uart_status;

  uart_status = HAL_UART_Receive_IT(
      &huart2,
      &rx_data,
      1
  );

  if (uart_status != HAL_OK)
  {
      uart_restart_fail_count++;
      uart_recovery_pending = 1;
  }
  /* USER CODE END 2 */

  /* Infinite loop */

  /* USER CODE BEGIN WHILE */
  /* USER CODE BEGIN WHILE */
  uint32_t last_tick = HAL_GetTick();

  while (1)
  {
      /* USER CODE END WHILE */

      /* USER CODE BEGIN 3 */

      if (HAL_GetTick() - last_tick >= 500)
      {
          last_tick = HAL_GetTick();
          HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
      }

      // UART Recovery
      if (uart_recovery_pending)
      {
          if (huart2.RxState == HAL_UART_STATE_READY)
          {
              HAL_StatusTypeDef status =
                  HAL_UART_Receive_IT(&huart2, &rx_data, 1);

              if (status == HAL_OK)
              {
                  uart_recovery_pending = 0;
                  uart_recovery_count++;
              }
              else
              {
                  uart_restart_fail_count++;
              }
          }
          else if (huart2.RxState == HAL_UART_STATE_BUSY_RX)
          {
              uart_recovery_pending = 0;
          }
      }

      // UART Ring Buffer Consumer
      uint8_t data;

      if (RingBuffer_Pop(&data))
      {
          if (data == '?')
          {
              UART_PrintStats();
          }
          else
          {
              HAL_UART_Transmit(&huart2, &data, 1, 100);
          }
      }
  }
  /* USER CODE END 3 */
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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
	int RingBuffer_Push(uint8_t data){
		uint16_t next_head = (head + 1U) % BUFFER_SIZE;
		if (next_head == tail){
			overflow_count++;
			return 0;
		}
		rx_buffer[head] = data;
		head = next_head;

		return 1;
	}

	int RingBuffer_Pop(uint8_t *data){
		if(data == NULL)
			return 0;
		if (head == tail)
			return 0;
		*data = rx_buffer[tail];
		tail = (tail + 1U) %BUFFER_SIZE;
		return 1;
	}

	void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
	{
	    if (huart->Instance == USART2)
	    {
	        RingBuffer_Push(rx_data);

	        HAL_StatusTypeDef status =
	            HAL_UART_Receive_IT(&huart2, &rx_data, 1);

	        if (status != HAL_OK)
	        {
	            uart_restart_fail_count++;
	            uart_recovery_pending = 1;
	        }
	    }
	}
	void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart){
		if (huart -> Instance == USART2){
			uint32_t error = HAL_UART_GetError(huart);
					uart_error_count++;
			if (error & HAL_UART_ERROR_ORE)
			{
			            uart_ore_count++;
			            uart_recovery_pending = 1;
			}
			if (error & HAL_UART_ERROR_FE)
			{
			            uart_fe_count++;
			}

			if (error & HAL_UART_ERROR_NE)
			{
			            uart_ne_count++;
			}

			if (error & HAL_UART_ERROR_PE)
			{
			            uart_pe_count++;
			}
		}
	}

	void UART_PrintStats(void)
	{
	    char msg[192];

	    uint16_t current_head = head;
	    uint16_t current_tail = tail;

	    uint16_t used =
	        (current_head + BUFFER_SIZE - current_tail)
	        % BUFFER_SIZE;

	    int len = snprintf(
	        msg,
	        sizeof(msg),
	        "\r\nHead=%u Tail=%u Used=%u Overflow=%lu\r\n"
	        "UART Errors=%lu ORE=%lu FE=%lu NE=%lu PE=%lu\r\n"
	        "Recovery=%lu RestartFail=%lu\r\n",
	        (unsigned)current_head,
	        (unsigned)current_tail,
	        (unsigned)used,
	        (unsigned long)overflow_count,
	        (unsigned long)uart_error_count,
	        (unsigned long)uart_ore_count,
	        (unsigned long)uart_fe_count,
	        (unsigned long)uart_ne_count,
	        (unsigned long)uart_pe_count,
	        (unsigned long)uart_recovery_count,
	        (unsigned long)uart_restart_fail_count
	    );

	    if (len > 0 && (size_t)len < sizeof(msg))
	    {
	        HAL_UART_Transmit(
	            &huart2,
	            (uint8_t *)msg,
	            (uint16_t)len,
	            100
	        );
	    }
	}
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
