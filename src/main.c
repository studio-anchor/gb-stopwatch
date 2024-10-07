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

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  GAME MACROS  -------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ -----------------------------  TEXT-LUT  ------------------------------ +//

#define NUMBERS_BASE_TILE_IDX 16 // value of "0"

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  DEFINITIONS  -------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

//+ ------------------------------  SYSTEM  ------------------------------- +//

bool is_gbc;
bool is_cpu_fast;

//+ -------------------------------  FONT  -------------------------------- +//

font_t font;

//+ -----------------------------  STOPWATCH  ----------------------------- +//

bool stopwatch;
bool play_tick_sfx;

// NOTE: volatile tells compiler this can change in isr, dont do optimizations on it
volatile uint8_t minutes; // Pointer to text LUT
volatile uint8_t seconds; // BCD
volatile uint8_t hundredths; // BCD

//* ------------------------------------------------------------------------------------------- *//
//* ----------------------------------------  ASSETS  ----------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

const unsigned char white_tile[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Generated in RGBASM using:
/*
  DEF MIL = 0
  REPT 128
  REDEF CB EQUS STRSUB("{f:MIL}", 3, 2)
  db "{CB}"
  DEF MIL += 1.0/128
  ENDR
*/
const char MilTable128[128][3] = {
	"00", "00", "01", "02",
	"03", "03", "04", "05",
	"06", "07", "07", "08",
	"09", "10", "10", "11",
	"12", "13", "14", "14",
	"15", "16", "17", "17",
	"18", "19", "20", "21",
	"21", "22", "23", "24",
	"25", "25", "26", "27",
	"28", "28", "29", "30",
	"31", "32", "32", "33",
	"34", "35", "35", "36",
	"37", "38", "39", "39",
	"40", "41", "42", "42",
	"43", "44", "45", "46",
	"46", "47", "48", "49",
	"50", "50", "51", "52",
	"53", "53", "54", "55",
	"56", "57", "57", "58",
	"59", "60", "60", "61",
	"62", "63", "64", "64",
	"65", "66", "67", "67",
	"68", "69", "70", "71",
	"71", "72", "73", "74",
	"75", "75", "76", "77",
	"78", "78", "79", "80",
	"81", "82", "82", "83",
	"84", "85", "85", "86",
	"87", "88", "89", "89",
	"90", "91", "92", "92",
	"93", "94", "95", "96",
	"96", "97", "98", "99"
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

	SHOW_BKG;
	SHOW_SPRITES;

	SOUND_ON;
	DISPLAY_ON;

}

//* ------------------------------------------------------------------------------------------- *//
//* --------------------------------------  INTERRUPTS  --------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void set_timer_reg_stopwatch(void) {

	CRITICAL {
		// Divide 4096hz clock by 32 (4096/32 = 128hz)
		if (!is_cpu_fast) {
			TMA_REG = (uint8_t)(0x100 - 32);
		} else {
			// ...Or by 64 if on GBC
			TMA_REG = (uint8_t)(0x100 - 64);
		}
	}

}

void stopwatch_timer_isr(void) {

	if (stopwatch) {
		hundredths = (hundredths + 1) & 0x7F;
		// If we overflowed
		if (hundredths == 0) {
			// GBDK *does* have BCD support, but it's 32bit, *way* overkill, 
			// so instead I'll just do it in assembly and try to explain...
			__asm
				// ; First, we load the value of seconds from its memory address to register A
				ld a, (#_seconds)

				// ; Now we add 1 to A
				add #0x01 

				// ; Next, we use the DAA instruction, which based on the CPU flags left by 
				// ; the previous instruction, corrects the value of A to be a valid BCD value, so for example:
				// ; 0x00 + 0x01 = 0x01 -> 0x01
				// ; 0x09 + 0x01 = 0x0A -> 0x10
				// ; 0x99 + 0x01 = 0x9A -> 0x00
				daa

				// ; Finally, we write the value of A back to the memory address of seconds
				ld (#_seconds), a
			__endasm;

			play_tick_sfx = TRUE;

			if (seconds >= 0x60) {
				seconds = 0x00;
				// Need to add 1 to minutes, use same snippet as above but not explained
				__asm__("ld a, (#_minutes)\n add #0x01\n daa\n ld (#_minutes), a");
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
//* ---------------------------------------  ROUTINES  ---------------------------------------- *//
//* ------------------------------------------------------------------------------------------- *//

void reset_stopwatch(void) {

	sfx_4();

	stopwatch = FALSE; // saftey

	minutes = 0;
	seconds = 0;
	hundredths = 0;

    gotoxy(6, 6);
    printf("00:00:00");

}

void pause_stopwatch(void) {

	// NOTE: dont reset TIMA_REG, pick up where it left off

	CRITICAL {
		TAC_REG = TACF_STOP; // stop timer
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
		TAC_REG = TACF_4KHZ | TACF_START; // start timer
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

	// First, draw miliseconds
    set_vram_byte(get_bkg_xy_addr(12, 6), MilTable128[hundredths][0] - '0' + NUMBERS_BASE_TILE_IDX);
    set_vram_byte(get_bkg_xy_addr(13, 6), MilTable128[hundredths][1] - '0' + NUMBERS_BASE_TILE_IDX);

	// BCD2Text is... weird, so we'll do it ourselves, cheaper than casting probs

	// Now seconds
	set_vram_byte(get_bkg_xy_addr(9, 6), ((seconds >> 4) & 0x0F) + NUMBERS_BASE_TILE_IDX);
	set_vram_byte(get_bkg_xy_addr(10, 6), (seconds & 0x0F) + NUMBERS_BASE_TILE_IDX);

	// and minutes
	set_vram_byte(get_bkg_xy_addr(6, 6), ((minutes >> 4) & 0x0F) + NUMBERS_BASE_TILE_IDX);
	set_vram_byte(get_bkg_xy_addr(7, 6), (minutes & 0x0F) + NUMBERS_BASE_TILE_IDX);

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

	set_timer_reg_stopwatch(); // set counter and modulo registers
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
