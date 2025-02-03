/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Modified Main program body for "Musical Box" logic
  ******************************************************************************
  * @attention
  *
  * Code adapted from the provided sample. Logic changed to implement:
  * - 4 standard melodies and 1 user melody
  * - No blocking delays (no HAL_Delay)
  * - Timer-based note duration and sound generation
  * - UART input for user melody configuration
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
#include "stdio.h"
#include "stdbool.h"
#include "string.h"

/* Private typedef -----------------------------------------------------------*/
typedef struct {
	uint32_t frequency;
	uint32_t duration; // duration in ms
} Note_t;

typedef enum {
	MELODY_IDLE,
	MELODY_PLAYING
} MelodyState_t;

/* Private define ------------------------------------------------------------*/
#define CLOCK_SCALED_FREQUENCY	1000000U
#define ENTER_ASCII				'\r'

#define MAX_USER_MELODY_NOTES   20
#define UART_INPUT_BUFFER_SIZE  64



#define A3  220
#define B3  246
#define C4  261
#define D4  293
#define E4  329

#define FS4  369
#define G4  392
#define A4  440 //432
#define B4  493

#define CS5  554
#define D5  587

#define FS5  739
#define G5  783
/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

char enter_user_mode_msg[] = "\n\rEntering user melody configuration mode.\n\rPlease enter notes in format: freq dur\n\rEnd with a line containing ';'\n\r";
char user_melody_done_msg[] = "\n\rUser melody configuration finished.\n\rPress '5' to play user melody.\n\r";

char unknown_command_msg[] = "\n\rUnknown command.\n\r";

char playing_standard_msg[] = "\n\rPlaying standard melody...\n\r";
char playing_user_msg[] = "\n\rPlaying user melody...\n\r";

/* Стандартные мелодии (простой пример): */
static const Note_t melody1[] = {
	{E4, 200}, {E4, 100}, {B3, 100}, {C4, 100},
	{D4, 100}, {C4, 100}, {B3, 200}, {A3, 200},
	{A3, 200}, {A3, 200}, {C4, 200}, {E4, 750},
	{D4, 200}, {C4, 200}, {B3, 100}, {B3, 100},
	{B3, 100}, {C4, 100}, {D4, 100}, {E4, 200},
	{C4, 200}, {A3, 750}
};


static const Note_t melody2[] = {
	{D4, 200}, {FS4, 200}, {CS5, 300},
	{E4, 200}, {G4, 200}, {D5, 300},
	{FS4, 200}, {A4, 200}, {FS5, 300},
	{G4, 500}, {B4, 500}, {G5, 750},
	{D5, 100}, {CS5, 100}, {A4, 100}, {FS4, 100},
	{D5, 100}, {CS5, 100}, {A4, 100}, {FS4, 100},
	{D5, 100}, {CS5, 100}, {A4, 100}, {FS4, 100},
	{D5, 100}, {CS5, 100}, {A4, 100}, {FS4, 100},
	{G4, 100}, {FS4, 100}, {D4, 100}, {B3, 100},
	{G4, 100}, {FS4, 100}, {D4, 100}, {B3, 100},
	{G4, 100}, {FS4, 100}, {D4, 100}, {B3, 100},
	{G4, 100}, {FS4, 100}, {D4, 100}, {B3, 100}
};
static const Note_t melody3[] = {
	{659, 500}, {698, 500}, {784, 500}, {0, 500}, {659, 500}
};
static const Note_t melody4[] = {
	{330, 500}, {349, 500}, {392, 500}, {0, 500}, {330, 500}
};

/* Массив пользовательской мелодии */
static Note_t userMelody[MAX_USER_MELODY_NOTES];
static uint8_t userMelodyCount = 0;
static bool inUserMelodyConfig = false;

/* UART input parsing */
static char uartInputBuffer[UART_INPUT_BUFFER_SIZE];
static uint8_t uartInputIndex = 0;

/* Melody playback state */
static MelodyState_t melodyState = MELODY_IDLE;
static const Note_t *currentMelody = NULL;
static uint16_t currentMelodyLength = 0;
static uint16_t currentNoteIndex = 0;
static bool timerStarted = false;

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void play_melody(const Note_t *melody, uint16_t length);
static void play_sound(Note_t note);
static void stop_sound(void);
static void stop_timer(void);
static void set_timer(uint32_t ms);

extern void initialise_monitor_handles(void);

/* Private user code ---------------------------------------------------------*/

bool is_digit_char(char c) {
	return ('0' <= c && c <= '9');
}

static void uart_write(char *data) {
	uint16_t size = strlen(data);
	HAL_UART_Transmit(&huart6, (uint8_t *) data, size, 100);
}




/* Генерация тона */
void play_sound(Note_t note) {
	if (note.frequency == 0) {
		htim1.Instance->CCR1 = 0; // пауза
		return;
	}
	htim1.Instance->ARR = (1000000U / note.frequency) - 1; //период одного колебания в мкс (с данной частотой)
	htim1.Instance->CCR1 = (htim1.Instance->ARR >> 1); // 50% duty
}




/* Остановить ноту */
void stop_sound(void) {
	htim1.Instance->CCR1 = 0;
}

/* Остановка и сброс таймера длительности нот */
void stop_timer(void) {
	HAL_TIM_Base_Stop_IT(&htim6);
	htim6.Instance->ARR = 0;
	timerStarted = false;
}

/* Запуск таймера на ms миллисекунд (по истечении вызовется Callback) */
void set_timer(uint32_t ms) {
	htim6.Instance->ARR = ms - 1;
	HAL_TIM_Base_Start_IT(&htim6);
	timerStarted = true;
}

/* Запуск мелодии */
static void play_melody(const Note_t *melody, uint16_t length) {
	currentMelody = melody;
	currentMelodyLength = length;
	currentNoteIndex = 0;
	melodyState = MELODY_PLAYING;
	// стартуем первую ноту
	play_sound(currentMelody[currentNoteIndex]);
	set_timer(currentMelody[currentNoteIndex].duration);
}

/* Завершение мелодии */
static void finish_melody(void) {
	stop_timer();
	stop_sound();
	melodyState = MELODY_IDLE;
	uart_write("\n\rMelody finished.\n\r");
}

/* Обработка входных данных UART */
bool get_input(char c) {
    char echo[2] = {c, '\0'};
    uart_write(echo);

    if (c == 'q' || c == 'Q') {
        if (melodyState == MELODY_PLAYING) {
        	finish_melody();
            return true;
        }
    }

    if (inUserMelodyConfig) {
        // В режиме ввода пользовательской мелодии
        if (c == '\r' || c == '\n') {
            // Конец строки ввода
            if (uartInputIndex == 0) {
                // Пустая строка, ничего не делаем
                return true;
            }

            uartInputBuffer[uartInputIndex] = '\0';
            uartInputIndex = 0;

            // Проверяем, не конец ли это ввода мелодии
            if (strchr(uartInputBuffer, ';') != NULL) {
                // Завершаем ввод пользовательской мелодии
                inUserMelodyConfig = false;
                uart_write(user_melody_done_msg);
            } else {
                // Парсим строку как "частота длительность"
                uint32_t freq, dur;
                if (sscanf(uartInputBuffer, "%lu %lu", &freq, &dur) == 2) {
                    if (userMelodyCount < MAX_USER_MELODY_NOTES) {
                        userMelody[userMelodyCount].frequency = freq;
                        userMelody[userMelodyCount].duration = dur;
                        userMelodyCount++;
                        uart_write("Note added.\n\r");
                    } else {
                        uart_write("User melody is full.\n\r");
                    }
                } else {
                    uart_write("Invalid input. Format: freq dur\n\rEnd input with ';'\n\r");
                }
            }
            return true;
        } else {
            // Накапливаем символ в буфер
            if (uartInputIndex < UART_INPUT_BUFFER_SIZE - 1) {
                uartInputBuffer[uartInputIndex++] = c;
            }
            return true;
        }
    } else {
        // Не в режиме ввода мелодии
        if (c == ENTER_ASCII) {
            // Вход в режим настройки пользовательской мелодии

            if (melodyState == MELODY_PLAYING) {
                uart_write("\n\rCannot enter configuration mode while melody is playing.\n\r");
                return true;
            }

        	inUserMelodyConfig = true;
            userMelodyCount = 0;
            uart_write(enter_user_mode_msg);
            uartInputIndex = 0;
            return true;
        }

        switch (c) {
            case '1':
                uart_write(playing_standard_msg);
                play_melody(melody1, sizeof(melody1)/sizeof(Note_t));
                return true;
            case '2':
                uart_write(playing_standard_msg);
                play_melody(melody2, sizeof(melody2)/sizeof(Note_t));
                return true;
            case '3':
                uart_write(playing_standard_msg);
                play_melody(melody3, sizeof(melody3)/sizeof(Note_t));
                return true;
            case '4':
                uart_write(playing_standard_msg);
                play_melody(melody4, sizeof(melody4)/sizeof(Note_t));
                return true;
            case '5':
                if (userMelodyCount > 0) {
                    uart_write(playing_user_msg);
                    play_melody(userMelody, userMelodyCount);
                } else {
                    uart_write("\nUser melody is empty.\n\r");
                }
                return true;
        }
    }
    return false;
}


void UART_get_input(void) {
	char c;
	HAL_StatusTypeDef status = HAL_UART_Receive(&huart6, (uint8_t *)&c, 1, 100);
	if (status == HAL_OK) {
		if (!get_input(c)) {
			uart_write(unknown_command_msg);
		}
	}
}

/* Callback от таймера длительности ноты */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim->Instance == TIM6) {
		// Окончание длительности текущей ноты
		if (melodyState == MELODY_PLAYING && currentMelody != NULL) {
			currentNoteIndex++;
			if (currentNoteIndex < currentMelodyLength) {
				play_sound(currentMelody[currentNoteIndex]);
				set_timer(currentMelody[currentNoteIndex].duration);
			} else {
				finish_melody();
			}
		} else {
			stop_timer();
		}
	}
}

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
	initialise_monitor_handles();

	HAL_Init();
	SystemClock_Config();

	MX_GPIO_Init();
	MX_TIM4_Init();
	MX_TIM6_Init();
	MX_USART6_UART_Init();
	MX_TIM1_Init();

	HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);

	HAL_TIM_Base_Start_IT(&htim1);
	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

	stop_timer();

	uart_write("Musical box ready.\n\rPress 1-4 for standard melodies, 5 for user melody.\n\rPress Enter to configure user melody.\n\r");

	while (1)
	{
		UART_get_input();
	}

}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  // Настройки тактирования согласно вашему проекту
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 15;
  RCC_OscInitStruct.PLL.PLLN       = 72;
  RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ       = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType     = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource  = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider= RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider= RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}


void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
