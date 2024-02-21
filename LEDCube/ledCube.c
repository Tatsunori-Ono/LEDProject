/* INCLUDING NECESSARY LIBRARIES */
#include "libopencm3/stm32/rcc.h" // Needed to enable clocks for particular GPIO ports
#include "libopencm3/stm32/gpio.h" // Needed to define things on the GPIO
#include "libopencm3/stm32/usart.h" // Needed to use USART
#include "libopencm3/stm32/adc.h" // Needed to convert analogue signals to digital

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* DEFINING MACROS */
#define LEDCUBE_PORT GPIOB
#define TX_PIN GPIO6
#define RX_PIN GPIO7
#define USART_PORT USART1
#define NUM_LEDS 512
#define ADC_REG ADC1

#define WIN_LENGTH 100

/* STRUCTS AND ENUMS */
enum CellState { EMPTY, APPLE, SNAKE, WALL };

/* Enum to interface between logic of program and analogue values of joystick */
enum DirectionChange {
	RIGHT,
	LEFT,
	UP,
	DOWN,
	CENTRE,
};

/* Struct to representing a single segment of the snake */
struct Snake_Segment {
	/* Position of snake */
	int x;
	int y;
	int z;

	/* Next and previous segments of snake */
	struct Snake_Segment* Next;
	struct Snake_Segment* Prev;
};

/* FUNCTION DECLARATIONS */
enum DirectionChange Controller_GetDirection(void);

void Game_Over(void);
void Game_Start(void);

void Cube_SetBitAt(int x, int y, int z);
void Cube_ClearBitAt(int x, int y, int z);
bool Cube_IsBitOnAt(int x, int y, int z);
void Cube_GenerateApple(void);
enum CellState Cube_GetCellStateAt(int x, int y, int z);
bool Cube_DimensionOutOfBounds(int d);
bool Cube_IsSnakeSegment(int x, int y, int z);
void Cube_SetAll(void);

void Snake_Init(int x, int y, int z);
void Snake_Turn(enum DirectionChange directionChange);
bool Snake_Step(void);
void Snake_Free(void);
void Snake_AddHead(int x, int y, int z);
void Snake_PopTail(void);
void Snake_NormalStep(int x, int y, int z);
void Snake_AppleStep(int x, int y, int z);

void Hardware_Setup(void);
int Hardware_ReadChannel(int channel);
void Hardware_RenderCube(void);

/* GLOBAL VARIABLES */
/* Variables for linked list representing snake */
int Snake_size = 0;
struct Snake_Segment* Snake_head;
struct Snake_Segment* Snake_tail;

/* Current (x, y, z) direction of snake */
int Snake_currentDirection[3] = {1, 0, 0};

/* This array is a representation of the cube and is rendered */
char Cube_map[64];

/* CONTROLLER FUNCTIONS */
/* Function to interface between program and joystick */
/* By getting appropriate DirectionChange depending on value of joystick */
enum DirectionChange Controller_GetDirection() {
	// Read appropriate channels
	int channel1 = Hardware_ReadChannel(1);
	int channel2 = Hardware_ReadChannel(2);

	if (channel2 > 2500) { // If controller to the left
		return LEFT;
	} else if (channel2 < 1500) { // Joystick moved to right
		return RIGHT;
	} else if (channel1 > 2500) { // Joystick moved up
		return UP;
	} else if (channel1 < 1500) { // Joystick moved down
		return DOWN;
	} else { // Joystick not moved enough in any particular direction
		return CENTRE;
	}
}

/* GAME FUNCTIONS */
/* Runs functions needed to be called at end of game */
void Game_Over() {
	// Cleanup memory
	Snake_Free();
}

/* Called when game is started, all the logic of the game stems from here */
void Game_Start() {
	Snake_Init(0, 5, 5); // Initialize snake such that its tail is at the position (0, 5, 5)
			     // And its head is one step in the current direction

	// Game loop
	while (true) {
		Snake_Turn(Controller_GetDirection()); // Turn (or continue forwards) snake depending on joystick

		// Try and move the snake in its current direction, otherwise end the game
		if (!Snake_Step()) {
			break;
		}

		Hardware_RenderCube(); // Render snake onto map

		// If win condition is met (snake length is at WIN_LENGTH)
		if (Snake_size == WIN_LENGTH) {
			// Set all LEDs on to indicate the player has won
			Cube_SetAll();
			Hardware_RenderCube();

			// And end the game loop
			break;
		}

		for (volatile unsigned int tmr=1e6; tmr > 0; tmr--); // Sleep for 1 second
	};

	Game_Over();
}

/* CUBE FUNCTIONS */
/* Sets bit corresponding to x, y, z position */
void Cube_SetBitAt(int x, int y, int z) {
	int i = 8 * y + x;
	Cube_map[i] = Cube_map[i] | 1 << z;
}

/* Clears bit corresponding to x, y, z position */
void Cube_ClearBitAt(int x, int y, int z) {
	int i = 8 * y + x;
	Cube_map[i] = Cube_map[i] & ~(1 << z);
}

/* Gets bit corresponding to x, y, z position */
bool Cube_IsBitOnAt(int x, int y, int z) {
	int i = 8 * y + x;
	return Cube_map[i] & 1 << z;
}

/* Generates an apple on a random non-snake position on the map */
void Cube_GenerateApple() {
	int x, y, z;

	// Pick a random position until that random positions corresponding bit is not 1
	do {
		x = rand() % 8;
		y = rand() % 8;
		z = rand() % 8;
	} while (Cube_IsBitOnAt(x, y, z) != 0);

	// Set the bit at the chosen position
	Cube_SetBitAt(x, y, z);
}

/* Checks if a variable of a dimension (x, y or z) is within the valid range */
bool Cube_DimensionOutOfBounds(int d) {
	if (d < 0 || d >= 8) {
		return true;
	}

	return false;
}

/* Checks if a position on the map is a segment of the snake */
bool Cube_IsSnakeSegment(int x, int y, int z) {
	// Make a copy of the Snake_head pointer we can manipulate
	struct Snake_Segment* current = Snake_head;

	// Iterate through all segments of the linked list representing the snake
	while (current != NULL) {
		// If any of their positions match with given position return true
		if (x == current->x && y == current->y && z == current->z) {
			return true;
		}

		current = current->Next;
	}

	return false;
}

/* Gets cell state (WALL, SNAKE, APPLE, EMPTY) of position */
enum CellState Cube_GetCellStateAt(int x, int y, int z) {
	// Check if given position is within valid bounds of the cube
	if (Cube_DimensionOutOfBounds(x) || Cube_DimensionOutOfBounds(y) || Cube_DimensionOutOfBounds(z)) {
		return WALL;
	}

	// Check if given position corresponds to a snake segment or an apple and handle appropriately if so
	if (Cube_IsBitOnAt(x, y, z)) {
		if (Cube_IsSnakeSegment(x, y, z)) {
			return SNAKE;
		}

		return APPLE;
	}

	// None of previous conditions were met so given position corresponds to an empty cell
	return EMPTY;
}

void Cube_SetAll() {
	// Set all cells to ON in Cube_map
	for (int i = 0; i < 64; i++) {
		Cube_map[i] = 1;
	}
}

/* SNAKE FUNCTIONS*/
/* Initializes linked list representing snake with Snake_tail at given position */
/* And Snake_head the position it is facing with currentDirection */
void Snake_Init(int x, int y, int z) {
	// Initialize Snake_head and Snake_tail pointers
	Snake_head = (struct Snake_Segment*)malloc(sizeof(struct Snake_Segment));
	Snake_tail = Snake_head;

	// Assign given position to Snake_head
	Snake_head->x = x;
	Snake_head->y = y;
	Snake_head->z = z;

	// Set bit on map corresponding to position
	Cube_SetBitAt(x, y, z);

	// As there is currently only one segment, set next and previous segments to NULL
	Snake_head->Next = NULL;
	Snake_head->Prev = NULL;

	// Initialize Snake_size as 1
	Snake_size = 1;

	// Move the snake once in its current direction using the same logic as if it ate an apple
	// So that it starts at a length of 2 and an apple is randomly generated on the map
	int newX = Snake_head->x + Snake_currentDirection[0];
	int newY = Snake_head->y + Snake_currentDirection[1];
	int newZ = Snake_head->z + Snake_currentDirection[2];
	Snake_AppleStep(newX, newY, newZ);
}

/* Add head of linked list representing snake at the given position */
void Snake_AddHead(int x, int y, int z) {
	// Change the map accordingly
	Cube_SetBitAt(x, y, z);

	// Allocate some new memory for the new segment to be pushed in
	struct Snake_Segment* newHead = (struct Snake_Segment*)malloc(sizeof(struct Snake_Segment));

	// Initialize the position of the new head at given position
	newHead->x = x;
	newHead->y = y;
	newHead->z = z;

	// Set connections of the new head segment appropriately
	newHead->Next = Snake_head;
	newHead->Prev = NULL;

	// Edit connections of the previous head appropriately
	Snake_head->Prev = newHead;

	// Reassign variable containing the pointer to the head of the snake to the new head
	Snake_head = newHead;
}

/* Pop tail of linked list representing snake */
void Snake_PopTail() {
	// Clear bit accordingly
	Cube_ClearBitAt(Snake_tail->x, Snake_tail->y, Snake_tail->z);

	// Set tail to be second last element in linked list
	Snake_tail = Snake_tail->Prev;

	free(Snake_tail->Next); // Free memory used by previous tail

	// New tail is no longer connected to the previous tail
	Snake_tail->Next = NULL;
}

/* Called when snake takes a normal step with the new head assumed to be at the given position*/
void Snake_NormalStep(int x, int y, int z) {
	// When a normal step is taken, we insert the new head and pop the current tail
	Snake_AddHead(x, y, z);
	Snake_PopTail();
}

/* Called when snake eats an apple with the new head assumed to be at the given position */
void Snake_AppleStep(int x, int y, int z) {
	// When snake eats an apple, its size increases by one and we insert a new head
	Snake_AddHead(x, y, z);
	Snake_size++;

	// Generate an apple
	Cube_GenerateApple();
}

/* Change currentDirection depending on directionChange */
void Snake_Turn(enum DirectionChange directionChange) {
	switch (directionChange) {
		case LEFT:
			// If snake was going inwards or outwards, set direction to absolute left
			if (Snake_currentDirection[2] == 1 || Snake_currentDirection[2] == -1) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = -1;
				Snake_currentDirection[2] = 0;
			// Otherwise turn left relative to current direction
			} else {
				Snake_currentDirection[2] = Snake_currentDirection[0];
				Snake_currentDirection[0] = Snake_currentDirection[1];
				Snake_currentDirection[1] = -Snake_currentDirection[2];

				Snake_currentDirection[2] = 0;
			}

			break;
		case RIGHT:
			// If snake was going inwards or outwards, set direction to absolute right
			if (Snake_currentDirection[2] == 1 || Snake_currentDirection[2] == -1) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = 1;
				Snake_currentDirection[2] = 0;
			// Otherwise turn right relative to current direction
			} else {
				Snake_currentDirection[2] = Snake_currentDirection[0];
				Snake_currentDirection[0] = -Snake_currentDirection[1];
				Snake_currentDirection[1] = Snake_currentDirection[2];

				Snake_currentDirection[2] = 0;
			}

			break;
		case UP:
			// If snake is not currently going inwards, set the direction to inwards
			if (!(Snake_currentDirection[2] == -1)) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = 0;
				Snake_currentDirection[2] = 1;
			}

			break;
		case DOWN:
			// If snake is not currently going outwards, set the direction to outwards
			if (!(Snake_currentDirection[2] == 1)) {
				Snake_currentDirection[0] = 0;
				Snake_currentDirection[1] = 0;
				Snake_currentDirection[2] = -1;
			}

			break;
		case CENTRE:
			// Don't change Snake_currentDirection
			break;
	}
}

/* Try and move one step in currentDirection */
/* Return whether it succeeded */
bool Snake_Step() {
	// New position head of snake will be trying to go to
	int newX = Snake_head->x + Snake_currentDirection[0];
	int newY = Snake_head->y + Snake_currentDirection[1];
	int newZ = Snake_head->z + Snake_currentDirection[2];

	// Get the state of the cell and handle appropriately
	enum CellState cellState = Cube_GetCellStateAt(newX, newY, newZ);
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

// Free memory of the linked list representing the snake at the end
void Snake_Free() {
	// Iterate through linked list representing snake
	while (Snake_head != NULL) {
		struct Snake_Segment* segment = Snake_head;
		Snake_head = Snake_head->Next;
		free(segment); // Free memory of segment
	}
}

/* HARDWARE FUNCTIONS */
/* Setup everything to be able to interact with the hardware (the LED cube and a joystick) */
void Hardware_Setup() {
	//// Setup GPIO B
	rcc_periph_clock_enable(RCC_GPIOB); // Enable clock for GPIO Port B
					
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5); // GPIO Port Name, GPIO Mode, GPIO Push Up Pull Down Mode, GPIO Pin Number
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO5); // GPIO Port Name, GPIO Pin Driver Type, GPIO Pin Speed, GPIO Pin Number

	//// Setup LED cube pins
	// Set B5
	gpio_set(GPIOB, GPIO5);

	// Setup TX pin
	gpio_mode_setup(LEDCUBE_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, TX_PIN);
	gpio_set_af(LEDCUBE_PORT, GPIO_AF7, TX_PIN);

	// Setup RX pin
	gpio_mode_setup(LEDCUBE_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, RX_PIN);
	gpio_set_af(LEDCUBE_PORT, GPIO_AF7, RX_PIN);

	//// Setup USART
	rcc_periph_clock_enable(RCC_USART1); // Enable clock for USART

	usart_set_baudrate(USART_PORT, 9600);
	usart_set_databits(USART_PORT, 8);
	usart_set_stopbits(USART_PORT, USART_STOPBITS_1);
	usart_set_mode(USART_PORT, USART_MODE_TX_RX);
	usart_set_parity(USART_PORT, USART_PARITY_NONE);
	usart_set_flow_control(USART_PORT, USART_FLOWCONTROL_NONE);

	usart_enable_rx_interrupt(USART_PORT);
	usart_enable_tx_interrupt(USART_PORT);

	usart_enable(USART_PORT);

	//// Setup ADC
	rcc_periph_clock_enable(RCC_ADC12); // Enable clock for ADC registers 1 and 2

	adc_power_off(ADC_REG); // Turn off ADC register 1 whist we set it up

	adc_set_clk_prescale(ADC_REG, ADC_CCR_CKMODE_DIV1); // Setup a scaling
	adc_disable_external_trigger_regular(ADC_REG); // We don't need to externally trigger the register...
	adc_set_right_aligned(ADC_REG); // Make sure it is right aligned to get more usable values
	adc_set_sample_time_on_all_channels(ADC_REG, ADC_SMPR_SMP_61DOT5CYC); // Set up sample time
	adc_set_resolution(ADC_REG, ADC_CFGR1_RES_12_BIT); // Get a good resolution

	adc_power_on(ADC_REG); // Finished setup, turn on ADC register 1
}

/* Read given channel on ADC_REG */
int Hardware_ReadChannel(int channel) {
	uint8_t channelArray[] = {channel}; // Define the channel that we want to look at
	adc_set_regular_sequence(ADC_REG, 1, channelArray); // Set up the channel
	adc_start_conversion_regular(ADC_REG); // Start converting the analogue signal

	while(!(adc_eoc(ADC_REG))); // Wait until the register is ready to read data

	return adc_read_regular(ADC_REG); // Read the value from the register and channel
}

/* Renders Cube_map on the LED cube */
void Hardware_RenderCube() {
	usart_send_blocking(USART_PORT, 0xF2); // To asynchonously start data transmission

	// Render Cube_map on cube
	for (int i = 0; i < 64; i++) {
		usart_send_blocking(USART_PORT, Cube_map[i]);
	}
}


int main(void) {
	Hardware_Setup();
	Game_Start();
	return 0;
}
