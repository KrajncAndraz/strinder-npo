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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
#include "stdio.h"
#include "ctype.h"
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

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define AP_SSID     "ESP_SETUP"
#define AP_PASS     "12345678"

static char saved_ssid[64] = "LAPTOP-RI8T18TU_9680";
static char saved_pass[64] = "aaaaaaaa";

static void uart_send_str(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t*)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

// wait until response
static int esp_wait_for(const char *token, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    size_t i = 0;
    size_t n = strlen(token);

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t c;
        if (HAL_UART_Receive(&huart2, &c, 1, 10) == HAL_OK)
        {
            if (c == (uint8_t)token[i]) {
                i++;
                if (i == n) return 1;
            } else {
                i = (c == (uint8_t)token[0]) ? 1 : 0;
            }
        }
    }
    return 0;
}


static int esp_cmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uart_send_str(cmd);
    return esp_wait_for(expect, timeout_ms);
}

// send data to client
static int esp_send(uint8_t link_id, const char *data, int len)
{
    char cmd[64];

    // AT+CIPSEND=<id>,<len>
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d,%d\r\n", link_id, len);
    if (!esp_cmd(cmd, ">", 2000)) return 0;

    HAL_UART_Transmit(&huart2, (uint8_t*)data, (uint16_t)len, HAL_MAX_DELAY);

    if (!esp_wait_for("SEND OK", 3000)) return 0;
    return 1;
}

// cloes conection
static void esp_close(uint8_t link_id)
{
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d\r\n", link_id);
    esp_cmd(cmd, "OK", 2000);
}

// start tcp on port 80
static int esp_start_ap_server(void)
{
    char cmd[128];

    if (!esp_cmd("AT\r\n", "OK", 1000)) return 0;
    esp_cmd("ATE0\r\n", "OK", 1000);            // echo off

    // AP+STA mode
    if (!esp_cmd("AT+CWMODE=3\r\n", "OK", 1000)) return 0;

    // Create setup AP
    snprintf(cmd, sizeof(cmd), "AT+CWSAP=\"%s\",\"%s\",5,3\r\n", AP_SSID, AP_PASS);
    if (!esp_cmd(cmd, "OK", 3000)) return 0;

    esp_cmd("AT+CIFSR\r\n", "OK", 2000);        // show IP 192.168.4.1

    if (!esp_cmd("AT+CIPMUX=1\r\n", "OK", 1000)) return 0;      // multiple connections
    if (!esp_cmd("AT+CIPSERVER=1,80\r\n", "OK", 2000)) return 0;// server port 80

    return 1;
}

// join real wifi
static int esp_join_wifi(const char *ssid, const char *pass)
{
    char cmd[200];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pass);
    return esp_cmd(cmd, "OK", 20000);
}

static int esp_read_ipd(uint8_t *link_id, char *out, int out_max, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    // find "+IPD,"
    const char *pat = "+IPD,";
    size_t pi = 0;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        uint8_t c;
        if (HAL_UART_Receive(&huart2, &c, 1, 10) != HAL_OK) continue;

        if (c == (uint8_t)pat[pi]) {
            pi++;
            if (pi == strlen(pat)) break;
        } else {
            pi = (c == (uint8_t)pat[0]) ? 1 : 0;
        }
    }
    if (pi != strlen(pat)) return 0;

    // read link_id digit
    uint8_t c;
    if (HAL_UART_Receive(&huart2, &c, 1, 100) != HAL_OK) return 0;
    if (c < '0' || c > '9') return 0;
    *link_id = (uint8_t)(c - '0');

    // read comma
    if (HAL_UART_Receive(&huart2, &c, 1, 100) != HAL_OK) return 0;
    if (c != ',') return 0;

    // read length until ':'
    int len = 0;
    while (1)
    {
        if (HAL_UART_Receive(&huart2, &c, 1, 200) != HAL_OK) return 0;
        if (c == ':') break;
        if (c < '0' || c > '9') return 0;
        len = len * 10 + (c - '0');
        if (len > 2000) return 0; // safety
    }

    // read payload bytes
    int take = (len < (out_max - 1)) ? len : (out_max - 1);

    for (int i = 0; i < take; i++) {
        if (HAL_UART_Receive(&huart2, (uint8_t*)&out[i], 1, 500) != HAL_OK) return 0;
    }

    // discard extra bytes if too large
    for (int i = take; i < len; i++) {
        if (HAL_UART_Receive(&huart2, &c, 1, 500) != HAL_OK) return 0;
    }

    out[take] = 0;
    return 1;
}

static void url_decode(char *s)
{
    char *p = s;
    char *q = s;

    while (*p)
    {
        if (*p == '+') {
            *q++ = ' ';
            p++;
        }
        else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = {p[1], p[2], 0};
            int val = 0;
            sscanf(hex, "%x", &val);
            *q++ = (char)val;
            p += 3;
        }
        else {
            *q++ = *p++;
        }
    }
    *q = 0;
}

static int get_param(const char *query, const char *key, char *out, int outsz)
{
    // find "key="
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "%s=", key);

    const char *p = strstr(query, pattern);
    if (!p) return 0;

    p += strlen(pattern);

    int i = 0;
    while (*p && *p != '&' && i < outsz - 1) {
        out[i++] = *p++;
    }
    out[i] = 0;
    url_decode(out);
    return (i > 0);
}

static void http_reply(uint8_t link_id, const char *html_body)
{
    char resp[1024];

    snprintf(resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s", html_body);

    esp_send(link_id, resp, (int)strlen(resp));
    esp_close(link_id);
}

static void handle_http(uint8_t link_id, const char *req)
{
    // req looks like: "GET /something HTTP/1.1..."
    if (strncmp(req, "GET ", 4) != 0) {
        http_reply(link_id, "<html><body><h3>Only GET supported</h3></body></html>");
        return;
    }

    // Extract path between "GET " and next space
    const char *p = req + 4;
    const char *sp = strchr(p, ' ');
    if (!sp) {
        http_reply(link_id, "<html><body><h3>Bad request</h3></body></html>");
        return;
    }

    char path[256];
    int path_len = (int)(sp - p);
    if (path_len > (int)sizeof(path) - 1) path_len = sizeof(path) - 1;
    memcpy(path, p, path_len);
    path[path_len] = 0;

    // "/" page
    if (strcmp(path, "/") == 0)
    {
        http_reply(link_id,
            "<html><body style='font-family:Arial'>"
            "<h2>WiFi Setup</h2>"
            "<p>Enter SSID and Password of your real WiFi.</p>"
            "<form action='/save' method='get'>"
            "SSID:<br><input name='ssid'><br><br>"
            "PASS:<br><input name='pass' type='password'><br><br>"
            "<input type='submit' value='SAVE'>"
            "</form>"
            "</body></html>");
        return;
    }

    // "/save?ssid=...&pass=..."
    if (strncmp(path, "/save", 5) == 0)
    {
        const char *q = strchr(path, '?');
        if (!q) {
            http_reply(link_id, "<html><body><h3>Missing params</h3><a href='/'>Back</a></body></html>");
            return;
        }
        q++; // after '?'

        char ssid[64] = {0};
        char pass[64] = {0};

        if (!get_param(q, "ssid", ssid, sizeof(ssid)) ||
            !get_param(q, "pass", pass, sizeof(pass))) {
            http_reply(link_id, "<html><body><h3>Error: need ssid and pass</h3><a href='/'>Back</a></body></html>");
            return;
        }

        // force user to change default
        if (strcmp(ssid, "CHANGE_ME") == 0 || strcmp(pass, "CHANGE_ME") == 0) {
            http_reply(link_id,
                "<html><body><h3>SSID/PASS must be real values (not CHANGE_ME)</h3>"
                "<a href='/'>Back</a></body></html>");
            return;
        }

        // Save in RAM
        strncpy(saved_ssid, ssid, sizeof(saved_ssid)-1);
        strncpy(saved_pass, pass, sizeof(saved_pass)-1);

        http_reply(link_id,
            "<html><body><h3>Saved ✅</h3><p>Connecting to WiFi...</p>"
            "<p>You can close this page.</p></body></html>");

        esp_join_wifi(saved_ssid, saved_pass);
        return;
    }

    // 404
    http_reply(link_id, "<html><body><h3>404 Not Found</h3><a href='/'>Home</a></body></html>");
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
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  HAL_Delay(1500); // wait ESP boot

  // Start AP + HTTP server on ESP
  esp_start_ap_server();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  uint8_t link_id;
	  char req[512];

	  if (esp_read_ipd(&link_id, req, sizeof(req), 10))
	  {
		  handle_http(link_id, req);
	  }
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_USART2
                              |RCC_PERIPHCLK_I2C1;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  PeriphClkInit.USBClockSelection = RCC_USBCLKSOURCE_PLL;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
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
  hi2c1.Init.Timing = 0x2000090E;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_4BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

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
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DRDY_Pin MEMS_INT3_Pin MEMS_INT4_Pin MEMS_INT1_Pin
                           MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = DRDY_Pin|MEMS_INT3_Pin|MEMS_INT4_Pin|MEMS_INT1_Pin
                          |MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : CS_I2C_SPI_Pin LD4_Pin LD3_Pin LD5_Pin
                           LD7_Pin LD9_Pin LD10_Pin LD8_Pin
                           LD6_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD7_Pin|LD9_Pin|LD10_Pin|LD8_Pin
                          |LD6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

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
