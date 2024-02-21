/* INCLUDING NECESSARY LIBRARIES */
#include "libopencm3/stm32/rcc.h"   //Needed to enable clocks for particular GPIO ports
#include "libopencm3/stm32/gpio.h"  //Needed to define things on the GPIO
#include "libopencm3/stm32/usart.h"
#include "libopencm3/stm32/adc.h" //Needed to convert analogue signals to digital

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* DEFINING MACROS */
#define LEDCUBE_PORT GPIOB
#define TX_PIN GPIO6
#define RX_PIN GPIO7
#define USART_PORT USART1
#define NUM_LEDS 512

/* STRUCTS AND ENUMS */
enum CellState { EMPTY, APPLE, SNAKE, WALL };

enum DirectionChange {
	RIGHT,
	LEFT,
	UP,
	DOWN,
	FORWARD,
};

struct Snake_Segment {
	int x;
	int y;
	int z;

	struct Snake_Segment* Next;
	struct Snake_Segment* Prev;
};

/* CONTROLLER */
enum DirectionChange Controller_GetDirection(void);

/* GAME */
void Game_Over(void);
void Game_Start(void);

/* MAP */
void Map_SetBitAt(int x, int y, int z);
void Map_ClearBitAt(int x, int y, int z);
bool Map_IsBitOnAt(int x, int y, int z);
void Map_GenerateApple(void);
enum CellState Map_GetCellStateAt(int x, int y, int z);
void Map_Render(void);

bool Map_DimensionOutOfBounds(int d);
bool Map_IsSnakeSegment(int x, int y, int z);
void Map_SetAll(void);

/* SNAKE */
void Snake_Init(int x, int y, int z);
bool Snake_Step(enum DirectionChange directionChange);
void Snake_Free(void);

void Snake_AddHead(int x, int y, int z);
void Snake_PopTail(void);
void Snake_NormalStep(int x, int y, int z);
void Snake_AppleStep(int x, int y, int z);

/* HARDWARE */
void Hardware_Setup(void);
int Hardware_ReadChannel(int channel);

int Snake_size = 0;

int Snake_currentDirection[3] = {1, 0, 0};

struct Snake_Segment* Snake_head;
struct Snake_Segment* Snake_tail;

bool Game_gameOver = false;
const int Game_INTERVAL_TIME = 100;

char Map_map[64];

/* CONTROLLER */
enum DirectionChange Controller_GetDirection() {
	int channel1 = Hardware_ReadChannel(1);
	int channel2 = Hardware_ReadChannel(2);
	if (channel2 > 2500) {
		return LEFT;
	} else if (channel2 < 1500) {
		return RIGHT;
	} else if (channel1 > 2500) {
		return UP;
	} else if (channel1 < 1500) {
		return DOWN;
	} else {
		return FORWARD;
	}
}

/* GAME */
void Game_Over() {
	Snake_Free();
	exit(0);
}

void Game_Start() {
	Snake_Init(0, 5, 5);

	int counter = 0;
	while (true) {
		if (!Snake_Step(Controller_GetDirection())) {
			break;
		}

		Map_Render();

		if (Snake_size == WIN_LENGTH) {
			Map_SetAll();

			Hardware_RenderCube();
			break;
		}

		for (volatile unsigned int tmr=1e6; tmr > 0; tmr--); //Sleep for 1 second
	};


	Game_Over();
}

/* CUBE */
void Map_SetBitAt(int x, int y, int z) {
	int i = 8 * y + x;
	Map_map[i] = Map_map[i] | 1 << z;
}

void Map_ClearBitAt(int x, int y, int z) {
	int i = 8 * y + x;
	Map_map[i] = Map_map[i] & ~(1 << z);
}

bool Map_IsBitOnAt(int x, int y, int z) {
	int i = 8 * y + x;
	return Map_map[i] & 1 << z;
}

void Map_GenerateApple() {
	int x;
	int y;
	int z;

	do {
		x = rand() % 8;
		y = rand() % 8;
		z = rand() % 8;
	} while (Map_IsBitOnAt(x, y, z) != 0);

	Map_SetBitAt(x, y, z);
}

bool Map_DimensionOutOfBounds(int d) {
	if (d < 0 || d >= 8) {
		return true;
	}

	return false;
}

bool Map_IsSnakeSegment(int x, int y, int z) {
	struct Snake_Segment* current = Snake_head;

	while (current != NULL) {
		if (x == current->x && y == current->y && z == current->z) {
			return true;
		}

		current = current->Next;
	}

	return false;
}

enum CellState Map_GetCellStateAt(int x, int y, int z) {
	if (Map_DimensionOutOfBounds(x) || Map_DimensionOutOfBounds(y) || Map_DimensionOutOfBounds(z)) {
		return WALL;
	}

	if (Map_IsBitOnAt(x, y, z)) {
		if (Map_IsSnakeSegment(x, y, z)) {
			return SNAKE;
		}

		return APPLE;
	}

	return EMPTY;
}

void Map_Render() {
	usart_send_blocking(USART_PORT, 0xF2); // As asynchonous start

	for (int i = 0; i < 64; i++) {
		usart_send_blocking(USART_PORT, Map_map[i]);
	}
}

void Map_SetAll() {
	// Set all cells to ON in Cube_map
	for (int i = 0; i < 64; i++) {
		Map_map[i] = 1;
	}
}

/* REPRESENTATION OF SNAKE AND FUNCTIONS TO MODIFY IT */
void Snake_Init(int x, int y, int z) {
	Snake_head = (struct Snake_Segment*)malloc(sizeof(struct Snake_Segment));
	Snake_tail = Snake_head;

	Snake_head->x = x;
	Snake_head->y = y;
	Snake_head->z = z;

	Map_SetBitAt(x, y, z);

	Snake_head->Next = NULL;
	Snake_head->Prev = NULL;

	Snake_size = 1;

	int newX = Snake_head->x + Snake_currentDirection[0];
	int newY = Snake_head->y + Snake_currentDirection[1];
	int newZ = Snake_head->z + Snake_currentDirection[2];
	Snake_AppleStep(newX, newY, newZ);
}

void Snake_AddHead(int x, int y, int z) {
	Map_SetBitAt(x, y, z);
	struct Snake_Segment* newHead = (struct Snake_Segment*)malloc(sizeof(struct Snake_Segment));

	newHead->x = x;
	newHead->y = y;
	newHead->z = z;

	newHead->Next = Snake_head;
	newHead->Prev = NULL;

	Snake_head->Prev = newHead;

	Snake_head = newHead;
}

void Snake_PopTail() {
	Map_ClearBitAt(Snake_tail->x, Snake_tail->y, Snake_tail->z);

	// Shift tail to be the segment left of it
	Snake_tail = Snake_tail->Prev;
	free(Snake_tail->Next);

	Snake_tail->Next = NULL;
}

void Snake_NormalStep(int x, int y, int z) {
	Snake_AddHead(x, y, z);
	Snake_PopTail();
}

void Snake_AppleStep(int x, int y, int z) {
	Snake_AddHead(x, y, z);
	Map_GenerateApple();

	Snake_size++;
}

bool Snake_Step(enum DirectionChange directionChange) {
	switch (directionChange) {
		case LEFT:
			if (Snake_currentDirection[2] == 1 || Snake_currentDirection[2] == -1) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = -1;
				Snake_currentDirection[2] = 0;
			} else {
				Snake_currentDirection[2] = Snake_currentDirection[0];
				Snake_currentDirection[0] = Snake_currentDirection[1];
				Snake_currentDirection[1] = -Snake_currentDirection[2];

				Snake_currentDirection[2] = 0;
			}
			break;
		case RIGHT:
			if (Snake_currentDirection[2] == 1 || Snake_currentDirection[2] == -1) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = 1;
				Snake_currentDirection[2] = 0;
			} else {
				Snake_currentDirection[2] = Snake_currentDirection[0];
				Snake_currentDirection[0] = -Snake_currentDirection[1];
				Snake_currentDirection[1] = Snake_currentDirection[2];

				Snake_currentDirection[2] = 0;
			}
			break;
		case UP:
			if (!(Snake_currentDirection[2] == -1)) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = 0;
				Snake_currentDirection[2] = 1;
			}

			break;
		case DOWN:
			if (!(Snake_currentDirection[2] == 1)) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = 0;
				Snake_currentDirection[2] = -1;
			}
			break;
		case FORWARD:
			break;
	}

	int newX = Snake_head->x + Snake_currentDirection[0];
	int newY = Snake_head->y + Snake_currentDirection[1];
	int newZ = Snake_head->z + Snake_currentDirection[2];

	enum CellState cellState = Map_GetCellStateAt(newX, newY, newZ);
	switch (cellState) {
		case (WALL):
		case (SNAKE):
			return false;
			break;
		case (APPLE):
			Snake_AppleStep(newX, newY, newZ);
			break;
		case (EMPTY):
			Snake_NormalStep(newX, newY, newZ);
			break;
	}

	return true;
}

void Snake_Free() {
	// Free memory for the Snake at the end
	while (Snake_head != NULL) {
		struct Snake_Segment* segment = Snake_head;
		Snake_head = Snake_head->Next;
		free(segment);
	}
}

void Hardware_Setup() {
	rcc_periph_clock_enable(RCC_GPIOB); //Enable clock for GPIO Port A
	rcc_periph_clock_enable(RCC_USART1); //Enable clock for USART

	// Setup GPIO B
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);            //GPIO Port Name, GPIO Mode, GPIO Push Up Pull Down Mode, GPIO Pin Number
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO5);   //GPIO Port Name, GPIO Pin Driver Type, GPIO Pin Speed, GPIO Pin Number

	gpio_set(GPIOB, GPIO5);  //Sets GPIO B5 pin

	// Setup TX pin
	gpio_mode_setup(LEDCUBE_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TX_PIN);
	gpio_set_af(LEDCUBE_PORT, GPIO_AF7, TX_PIN);

	// Setup RX pin
	gpio_mode_setup(LEDCUBE_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, RX_PIN);
	gpio_set_af(LEDCUBE_PORT, GPIO_AF7, RX_PIN);

	// Setup UART parameters
	usart_set_baudrate(USART_PORT, 9600);
	usart_set_databits(USART_PORT, 8);
	usart_set_stopbits(USART_PORT, USART_STOPBITS_1);
	usart_set_mode(USART_PORT, USART_MODE_TX_RX);
	usart_set_parity(USART_PORT, USART_PARITY_NONE);
	usart_set_flow_control(USART_PORT, USART_FLOWCONTROL_NONE);

	usart_enable_rx_interrupt(USART_PORT);
	usart_enable_tx_interrupt(USART_PORT);

	usart_enable(USART_PORT);

	rcc_periph_clock_enable(RCC_ADC12); //Enable clock for ADC registers 1 and 2

	adc_power_off(ADC1);  //Turn off ADC register 1 whist we set it up

	adc_set_clk_prescale(ADC1, ADC_CCR_CKMODE_DIV1);  //Setup a scaling, none is fine for this
	adc_disable_external_trigger_regular(ADC1);   //We don't need to externally trigger the register...
	adc_set_right_aligned(ADC1);  //Make sure it is right aligned to get more usable values
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_61DOT5CYC);  //Set up sample time
	adc_set_resolution(ADC1, ADC_CFGR1_RES_12_BIT);  //Get a good resolution

	adc_power_on(ADC1);  //Finished setup, turn on ADC register 1
}

int Hardware_ReadChannel(int channel) {
	uint8_t channelArray[] = {channel};  //Define a channel that we want to look at
	adc_set_regular_sequence(ADC1, 1, channelArray);  //Set up the channel
	adc_start_conversion_regular(ADC1);  //Start converting the analogue signal

	while(!(adc_eoc(ADC1)));  //Wait until the register is ready to read data

	return adc_read_regular(ADC1);  //Read the value from the register and channel
}

int main(void) {
	Hardware_Setup();
	Game_Start();
	return 0;
}
