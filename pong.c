#define F_CPU 1000000UL

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <stdint.h>
#include "font5x7.h"

typedef struct {
	int x;
	int y;

	int w;
	int h;
} rect;

typedef enum { 
	LCD_CMD  = 0, 
	LCD_DATA = 1 
} lcd_cmd_data_t;


/* Lcd screen size */
#define LCD_X_RES 132
#define LCD_Y_RES 64
#define LCD_CACHE_SIZE ((LCD_X_RES * LCD_Y_RES) / 8)

int lcd_cache_data[LCD_X_RES][8];

/* Pinout for LCD */
#define LCD_CLK_PIN 	(1<<PC4)
#define LCD_DATA_PIN 	(1<<PC3)
#define LCD_DC_PIN 		(1<<PC2)
#define LCD_CE_PIN 		(1<<PC1)
#define LCD_RST_PIN 	(1<<PC0)
#define LCD_PORT		PORTC
#define LCD_DDR			DDRC

void lcd_send(uint8_t data, lcd_cmd_data_t cd);

void lcd_clear() 
{
	int i, j;

	for(i=0;i<8;i++)
	{
		lcd_send(0xB0 | i, LCD_CMD);
		lcd_send(0x10, LCD_CMD);
		lcd_send(0x00, LCD_CMD);	// column 0

		for(j=0;j<LCD_X_RES;j++)
		{
			lcd_send(0x00, LCD_DATA);

			lcd_cache_data[j][i] = 0;
		}
	}   

	lcd_send(0xB0, LCD_CMD);	// page 0
	lcd_send(0x10, LCD_CMD);
	lcd_send(0x00, LCD_CMD);	// column 0
}

void lcd_send(uint8_t data, lcd_cmd_data_t cd)
{
	// Data/DC are outputs for the lcd (all low)
	LCD_DDR |= LCD_DATA_PIN | LCD_DC_PIN;
	
    // Enable display controller (active low)
    LCD_PORT &= ~LCD_CE_PIN;

    // Either command or data
    if(cd == LCD_DATA) 
	{
        LCD_PORT |= LCD_DC_PIN;
    } 
	else 
	{
        LCD_PORT &= ~LCD_DC_PIN;
    }
	
	for(unsigned char i=0;i<8;i++) 
	{
		// Set the DATA pin value
		if((data>>(7-i)) & 0x01) 
		{
			LCD_PORT |= LCD_DATA_PIN;
		} 
		else 
		{
			LCD_PORT &= ~LCD_DATA_PIN;
		}
		
		// Toggle the clock
		LCD_PORT |= LCD_CLK_PIN;
		for(int j=0;j<4;j++); // delay
		LCD_PORT &= ~LCD_CLK_PIN;
	}

	// Disable display controller
	//LCD_PORT &= ~LCD_DC_PIN;
    LCD_PORT |= LCD_CE_PIN;
	
	// Data/DC can be used as button inputs when not sending to LCD (/w pullups)
	LCD_DDR &= ~(LCD_DATA_PIN | LCD_DC_PIN);
	LCD_PORT |= LCD_DATA_PIN | LCD_DC_PIN;
}

void lcd_init(void)
{
	//Pull-up on reset pin
    LCD_PORT |= LCD_RST_PIN;	//Reset = 1
	
	//Set output bits on lcd port
	LCD_DDR |= LCD_RST_PIN | LCD_CE_PIN | LCD_DC_PIN | LCD_DATA_PIN | LCD_CLK_PIN;

	//Wait after VCC high for reset (max 30ms)
    _delay_ms(15);
    
    //Toggle display reset pin
    LCD_PORT &= ~LCD_RST_PIN; 	//Reset = 0
	_delay_ms(15);
    LCD_PORT |= LCD_RST_PIN;	//Reset = 1

	_delay_ms(15);

    //Disable LCD controller
    LCD_PORT |= LCD_CE_PIN;

    lcd_send(0xEB, LCD_CMD);  	//LCD bias 
    lcd_send(0x23, LCD_CMD);  	//Set Lines >> 23 = 64
    lcd_send(0x81, LCD_CMD);	//Set Potentiometer
	lcd_send(0x64, LCD_CMD);	//16 >> 64 (Tummuus)
	lcd_send(0xAF, LCD_CMD);  	//Set Display ON
    lcd_send(0xCC, LCD_CMD);  	//Set LCD to RAM mapping
    
    // Clear lcd
    lcd_clear();
	
	//For using printf
	//fdevopen(lcd_chr, 0);
}

void lcd_char(int8_t c)
{
	for (uint8_t i = 0; i < 5; ++i)
	{
		lcd_send(pgm_read_byte(&font5x7[c - 32][i]) << 1, LCD_DATA);
	}
}

void lcd_pixel(int x, int y)
{
	int page = y/8;
	int y_line = y % 8;

	int i = y_line;
	
	int chr = 1;

	while (i > 0) {
		chr = chr << 1;
		--i;
	}

	chr = chr | lcd_cache_data[x][page];

	lcd_send(0xB0 | page, LCD_CMD);	// page
		
	lcd_send(0x00 | (x & 0x0F), LCD_CMD); // what is this?
	lcd_send(0x10 | ((x & 0xF0)>>4), LCD_CMD);	// column
	lcd_send(chr, LCD_DATA);

	lcd_cache_data[x][page]=chr;
}

void rect_draw(rect* rect) {
	int x = rect->x;
	int y = rect->y;

	int w = rect->w;
	int h = rect->h;

	for (int i = y; i < y + h; i++) {
		for (int j = x; j < x + w; j++) {
			lcd_pixel(j, i);
		}
	}
}

void adc_init()
{
	ADCSRA |= 1<<ADPS2; 			//Esijakaja 64 -> Taajuus sopivaksi AD-muuntimelle
	ADCSRA |= 1<<ADPS1;
	
	ADMUX |= 1<<ADLAR;				//AD-muunnoksen tulos vasemmalle vieritetty
	
	ADMUX |= 1<<REFS0;
	ADMUX &= ~(1<<REFS1);			//Avcc(+5v) muuntimen referenssijännitteeksi
	
	ADCSRA |= 1<<ADEN;				//Otetaan AD-muunnin käyttöön
}


uint16_t adc_read(uint8_t ch)
{
	uint16_t ADCresult = 0;

	ADMUX &= (~0x1F);							//Nollataan rekisterin kanavanvalintabitit
	ADMUX |= ch;								//otetaan haluttu kanava k?ytt??n
	
	ADCSRA |= 1<<ADSC;							//Aloitetaan uusi muunnos
    while(!(ADCSRA & (1<<ADIF)));				//Odotetaan ett? muunnos on valmis
	
	uint8_t theLowADC = (ADCL>>6);				// Luetaan AD-muuntimelta tuleva LSB ja bittien siirto
	uint8_t theHighADC = ADCH;					// Luetaan AD-muuntimelta tuleva MSB

	ADCresult = theLowADC | (theHighADC<<2);	//Yhdistet??n AD-muuntimen LSB ja MSB ja bittien siirto
	ADCresult = ADCresult & 0x03FF;				//Tuloksen maskaus	
	
	return ADCresult;
}

void joystick_read(int* vx, int* vy)
{
	*vx = 0;
	*vy = 0;

	float temp = adc_read(0); //AD-muuntimen lukeminen, kanava 0 (0b00000)
	float xVoltage = ((temp * 5.f) / 1024);

	temp = adc_read(1);//AD-muuntimen lukeminen, kanava 1 (0b00001)
	float yVoltage = ((temp * 5.f) / 1024);

	if (xVoltage < 2.4f) 
		*vx = -1;
	else if (xVoltage > 2.9f)
		*vx = 1;

	if (yVoltage < 2.4f)
		*vy = -1;
	else if (yVoltage > 2.9f)
		*vy = 1;
}	

int main()
{
	adc_init();
	lcd_init();

	rect player;
	player.x = 0;
	player.y = 5;
	player.w = 2;
	player.h = 8;

	rect player2;
	player2.w = 2;
	player2.h = 8;
	player2.y = 0;
	player2.x = LCD_X_RES - player2.w;

	for (;;) 
	{
		lcd_clear();

		int vx, vy;

		joystick_read(&vx, &vy);

		if (vx != 0) player.x += vx * 1;
		if (vy != 0) player.y += vy * 1;
		
		if (player.x + player.w >= LCD_X_RES) player.x = 0;
		if (player.y + player.h >= LCD_Y_RES) player.y = 0;
		/*
		lcd_send(0xB0 | y, LCD_CMD);	// page
		
		lcd_send(0x00 | (x & 0x0F), LCD_CMD); // what is this?
		lcd_send(0x10 | ((x & 0xF0)>>4), LCD_CMD);	// column

		lcd_char('b');

		lcd_send(0, LCD_DATA); // what is this
		*/

		rect_draw(&player);
		rect_draw(&player2);
		
		_delay_ms(500);
	}
	return 0;
}
