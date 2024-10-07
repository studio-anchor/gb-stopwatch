#include <gb/gb.h>
#include <gb/cgb.h>

#include <gbdk/font.h>
#include <gbdk/console.h> // gotoxy()

#include <stdbool.h> // bool, true, false
#include <stdlib.h> // uitoa
#include <stdio.h> // printf()

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  NOTES  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

/* ~---------------------------------------------------------------------------

	TIMA_REG (Timer Counter):
	This is the register that increments at a rate determined by the clock frequency selected in TAC_REG.
	When TIMA_REG overflows from 0xFF to 0x00, an interrupt is requested.

	TMA_REG (Timer Modulo):
	When TIMA_REG overflows, the value of TMA_REG is loaded into TIMA_REG.
	Essentially, TMA_REG holds the value that the TIMA_REG will be reset to, upon overflow.
	And TIMA_REG will continue to increment from there.

	TAC_REG (Timer Control):
	This register is used to control the behavior of the timer (TIMA_REG).
	It DIVIDES the CPU clock by a certain factor, determining the frequency at which TIMA_REG increments.
	When TIMA_REG overflows, an interrupt is requested.
	When the timer is stopped, TIMA_REG does not increment.
        
	possible values for TAC_REG are:
	_____________________________________________________________________________________
	TAC_REG Value	Bit 2	Bit 1	Bit 0	Clock Source		Frequency (approx.)
	0x00			0		0		0		Timer stopped		-
	0x01			0		0		1		Timer stopped		-
	0x02			0		1		0		Timer stopped		-
	0x03			0		1		1		Timer stopped		-
	0x04			1		0		0		CPU Clock / 1024	~4.096 kHz		=> 4.096ms
	0x05			1		0		1		CPU Clock / 16		~262.144 kHz 	=> 0.004ms
	0x06			1		1		0		CPU Clock / 64		~65.536 kHz 	=> 0.016ms
	0x07			1		1		1		CPU Clock / 256		~16.384 kHz 	=> 0.064ms
	_____________________________________________________________________________________
	_____________________________________________________________________________________
	TAC_REG Value	Clock Source		Frequency (approx.)		Increments per second		Increments per frame (60 FPS)
	0x04			CPU Clock / 1024	~4.096 kHz				4,096						~68.27
	0x05			CPU Clock / 16		~262.144 kHz			262,144						~4,369.07
	0x06			CPU Clock / 64		~65.536 kHz				65,536						~1,092.27
	0x07			CPU Clock / 256		~16.384 kHz				16,384						~273.07
	_____________________________________________________________________________________

---------------------------------------------------------------------------~ */

//* ------------------------------------------------------------------------------------------- *//
//* -------------------------------------  COMMON MACROS  ------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ ------------------------------  GENERAL  ------------------------------ +//

//+ --  BUFFERS  -- +//

#define UITOA_DECIMAL 10 // convert number to decimal

#define BUFFER_NUM_16_BIT (5 + 1) // 5 digits + null-terminator

//+ ------------------------------  SYSTEM  ------------------------------- +//

//+ --  SOUND  -- +//

#define SOUND_ON \
	NR52_REG = 0x80; /* turns on sound */ \
	NR51_REG = 0xFF; /* turns on L/R for all channels */ \
	NR50_REG = 0x77; /* sets volume to max for L/R */

#define SOUND_OFF \
	NR52_REG = 0x00; /* turns off sound */ \
	NR51_REG = 0x00; /* turns off L/R for all channels */ \
	NR50_REG = 0x00; /* sets volume to min for L/R */

#define VOLUME_MAX \
    NR50_REG = 0x77;

#define VOLUME_HIGH \
    NR50_REG = 0x55;

#define VOLUME_MED \
    NR50_REG = 0x33; 

#define VOLUME_LOW \
    NR50_REG = 0x11;

#define VOLUME_MIN \
    NR50_REG = 0x00;

//+ --  INTERRUPTS  -- +//

#define TAC_REG_STOP 0x00			// stop timer											- TACF_STOP   0b00000000
#define TAC_REG_START_1024 0x04 	// start timer, TIMA_REG increments every 1024 cycles	- TACF_4KHZ   0b00000000
#define TAC_REG_START_256 0x07 		// start timer, TIMA_REG increments every 256 cycles	- TACF_262KHZ 0b00000001
#define TAC_REG_START_64 0x06 		// start timer, TIMA_REG increments every 64 cycles 	- TACF_65KHZ  0b00000010

#define TMA_REG_REALTIME_DMG 0x5C 	// 0x5C=92
#define TMA_REG_REALTIME_GBC 0xAE 	// 0xAE=174

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  GAME MACROS  -------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ -----------------------------  TEXT-LUT  ------------------------------ +//

#define TEXT_LUT_ASCII_BASE_IDX 0 // 0x00

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  DEFINITIONS  -------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ ------------------------------  SYSTEM  ------------------------------- +//

bool is_gbc;
bool is_cpu_fast;

//+ -------------------------------  FONT  -------------------------------- +//

font_t font;

//+ ---------------------------  TEXT-BUFFERS  ---------------------------- +//

uint8_t text_LUT_ascii_base_idx; // base-tile for ascii characters

uint8_t num_zeros; // number of leading zeros

//+ -----------------------------  STOPWATCH  ----------------------------- +//

bool stopwatch;
bool play_tick_sfx;

volatile uint8_t minutes; // NOTE: volatile tells compiler this can change in isr, dont do optimizations on it
volatile uint8_t seconds;
volatile uint8_t hundredths;

unsigned char num_buffer_minutes[BUFFER_NUM_16_BIT];
unsigned char num_buffer_seconds[BUFFER_NUM_16_BIT];
unsigned char num_buffer_hundredths[BUFFER_NUM_16_BIT];

//* ------------------------------------------------------------------------------------------- *//
//* ----------------------------------------  ASSETS  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

const unsigned char white_tile[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

//* ------------------------------------------------------------------------------------------- *//
//* ------------------------------------------  SFX  ------------------------------------------ *//
//* ------------------------------------------------------------------------------------------- *//

void sfx_1(void) {
	// CHN-1:   1, 0, 7, 1, 2, 13, 0, 5, 1847, 0, 1, 1, 0
	NR10_REG = 0x17; // freq sweep
	NR11_REG = 0x42; // duty, length
	NR12_REG = 0xD5; // envelope 
	NR13_REG = 0x37; // freq lbs
	NR14_REG = 0x87; // init, cons, freq msbs 
}

void sfx_2(void) {
	// CHN-1:   6, 0, 4, 2, 2, 13, 0, 5, 1847, 0, 1, 1, 0
	NR10_REG = 0x64; // freq sweep
	NR11_REG = 0x82; // duty, length
	NR12_REG = 0xD5; // envelope 
	NR13_REG = 0x37; // freq lbs
	NR14_REG = 0x87; // init, cons, freq msbs 
}

void sfx_4(void) {
	// CHN-1:	6, 1, 5, 2, 5, 13, 0, 1, 1885, 0, 1, 1, 0
	NR10_REG = 0x6D; // freq sweep
	NR11_REG = 0x85; // duty, length
	NR12_REG = 0xD1; // envelope 
	NR13_REG = 0x5D; // freq lbs   
	NR14_REG = 0x87; // init, cons, freq msbs 
}

//* ------------------------------------------------------------------------------------------- *//
//* ----------------------------------------  SYSTEM  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void set_cpu(void) {

	CRITICAL {
		if (_cpu == CGB_TYPE) is_gbc = TRUE;
		if (is_gbc) {
			cpu_fast();
			is_cpu_fast = TRUE;

			set_default_palette(); // palette-0, grayscale
		}
	}

}

void clear_sprite_tiles(void) {

	for (uint8_t i = 0; i < 127; i++) {
		set_sprite_data(i, 1, white_tile);
	}

}

void init_system(void) {

	set_cpu();

	clear_sprite_tiles(); // clear VRAM
	init_bkg(0); // reset bkg_map with tile-0

	set_interrupts(VBL_IFLAG | LCD_IFLAG | SIO_IFLAG | TIM_IFLAG);
	enable_interrupts(); // IE_REG

	SHOW_BKG;
	SHOW_SPRITES;

	SOUND_ON;
	DISPLAY_ON;

}

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  INTERRUPTS  --------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void set_timer_realtime_reg(void) {

	CRITICAL {
		if (!is_cpu_fast) {
			TMA_REG = TMA_REG_REALTIME_DMG;
			TIMA_REG = TMA_REG_REALTIME_DMG;
		} else {
			TMA_REG = TMA_REG_REALTIME_GBC;
			TIMA_REG = TMA_REG_REALTIME_GBC;
		}
	}

}

void stopwatch_timer_isr(void) {

	if (stopwatch) {
		hundredths++;
		if (hundredths >= 100) {
			hundredths = 0;
			seconds++;
			play_tick_sfx = TRUE;
			if (seconds >= 60) {
				seconds = 0;
				minutes++;
			}
		}
	}

}

void set_timer_isr_stopwatch(void) {

	CRITICAL {
		add_TIM(stopwatch_timer_isr); // NOTE: will not be interrupted by other interrupts
	}

}

void clear_timer_isr_stopwatch(void) {

	CRITICAL {
		remove_TIM(stopwatch_timer_isr);
	}

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  INITS  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void init_scene(void) {

	gotoxy(1, 1);
	printf("GB STOPWATCH :");
	gotoxy(1, 2);
	printf("------------------");

	gotoxy(6, 6);
	printf("00:00:00");

	gotoxy(1, 14);
	printf("------------------");
	gotoxy(5, 15);
	printf("A:   Start");
	gotoxy(5, 16);
	printf("B:   Reset");

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  UTILS  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

inline void draw_numbers_ascii(uint8_t x, uint8_t y, const unsigned char *buffer) {

	uint8_t i = 0;
	uint8_t *vramAddr = get_bkg_xy_addr(x, y); // VRAM address of first character

	for (uint8_t j = 0; j < num_zeros; j++) {
		set_vram_byte(vramAddr++, text_LUT_ascii_base_idx + 16);
	}

	while (buffer[i] != '\0') {
		if (buffer[i] >= '0' && buffer[i] <= '9') {
			uint8_t tileidx = text_LUT_ascii_base_idx + buffer[i] - ' ';
			set_vram_byte(vramAddr++, tileidx);
		}
		i++;
	}

}

//* ------------------------------------------------------------------------------------------- *//
//* ---------------------------------------  ROUTINES  ---------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void reset_stopwatch(void) {

	sfx_4();

	stopwatch = FALSE;

	minutes = 0;
	seconds = 0;
	hundredths = 0;

	gotoxy(6, 6);
	printf("00:00:00");

}

void pause_stopwatch(void) {

	// NOTE: dont reset TIMA_REG, pick up where it left off

	CRITICAL {
		TAC_REG = TAC_REG_STOP;
		stopwatch = FALSE;
	}

	VOLUME_MAX;
	sfx_1();

	gotoxy(10, 15);
	printf("Start");
	gotoxy(5, 16);
	printf("B:   Reset");

}

void start_stopwatch(void) {

	CRITICAL {
		TAC_REG = is_cpu_fast ? TAC_REG_START_1024 : TAC_REG_START_256;
		stopwatch = TRUE;
	}

	VOLUME_MAX;
	sfx_1();
	
	gotoxy(10, 15);
	printf("Stop ");
	gotoxy(5, 16);
	printf("          ");

}

void handle_inputs(void) {

	static uint8_t prev_joypad = NULL;
	uint8_t current_joypad = joypad();

	if ((joypad() & J_A) && !(prev_joypad & J_A)) {
		if (stopwatch) pause_stopwatch();
		else start_stopwatch();
	}
	if ((joypad() & J_B) && !(prev_joypad & J_B) && !stopwatch) {
		reset_stopwatch();
	}

	prev_joypad = current_joypad;

}

inline void print_stopwatch(void) {

	// NOTE: draw every frame, to account for max cycles it will take anyway

	num_zeros = (minutes > 9) ? 0 : 1;
	uitoa(minutes, num_buffer_minutes, UITOA_DECIMAL); // minutes
	draw_numbers_ascii(6, 6, num_buffer_minutes);

	num_zeros = (seconds > 9) ? 0 : 1;
	uitoa(seconds, num_buffer_seconds, UITOA_DECIMAL); // seconds
	draw_numbers_ascii(9, 6, num_buffer_seconds);

	num_zeros = (hundredths > 9) ? 0 : 1;
	uitoa(hundredths, num_buffer_hundredths, UITOA_DECIMAL); // hundredths
	draw_numbers_ascii(12, 6, num_buffer_hundredths);

	set_bkg_tile_xy(14, 6, 0); // HACK: saftey, hundredths happens so fast, leading zeros can't keep up and it will sometimes draw 3 digits

}

void handle_stopwatch(void) {

	if (stopwatch) {
		print_stopwatch();

		if (play_tick_sfx) {
			VOLUME_LOW;
			sfx_2();
			play_tick_sfx = FALSE;
		}
	}

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  GAME  ------------------------------------------ *//
//* ------------------------------------------------------------------------------------------- *//

void init_game(void) {

	font_init();
	font = font_load(font_spect);

	set_timer_realtime_reg(); // set counter and modulo registers
	set_timer_isr_stopwatch(); // set isr

	init_scene(); // header and controls text

}

//* ------------------------------------------------------------------------------------------- *//
//* -----------------------------------------  MAIN  ------------------------------------------ *//
//* ------------------------------------------------------------------------------------------- *//

void main(void) {

	init_system();
	init_game();

	while (TRUE) {
		handle_inputs();
		vsync();
		handle_stopwatch();
	}

}
