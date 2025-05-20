//********************************************************************
//*                    MEC4126F C template                           *
//*==================================================================*
//* WRITTEN BY: Tinomuda Madzore  	                 	             *
//* DATE CREATED: 13/05/2025                                         *
//*==================================================================*
//* PROGRAMMED IN: Visual Studio Code                                *
//* TARGET:        STM32F0                                           *
//*==================================================================*
//* DESCRIPTION:     Template for MEC4126F C Practicals              *
//*                                                                  *
//********************************************************************
// INCLUDE FILES
//====================================================================

#define STM32F051

#include "stm32f0xx.h"											   
#include "stdio.h"
#include "stdint.h"
#include "lcd_stm32f0.h"

//====================================================================
// GLOBAL CONSTANTS
//====================================================================

const float K_p = 50.0f;
const float I = 1.0f;
const float T_s = 0.001f; // for 1khz

//====================================================================
// GLOBAL VARIABLES
//====================================================================

volatile float gain = 0;
volatile float out = 0;
volatile uint16_t comm_pos = 0;
volatile uint16_t real_pos = 0;
volatile uint8_t adc_channel = 0;

//====================================================================
// FUNCTION DECLARATIONS
//====================================================================
void set_to_48MHz(void);
void init_student(void);
void init_ADC(void);
void init_TIM3(void);
void ADC1_COMP_IRQHandler(void);
float PI_control(uint16_t comm_pos, uint16_t real_pos);
void init_TIM14(void);
void TIM14_IRQHandler(void);

//====================================================================
// MAIN FUNCTION
//====================================================================

void main (void)
{
    set_to_48MHz();
    init_student();
    init_ADC();
    init_TIM3();
    init_TIM14();

    while (1)
    {
       
    }
}							
// End of main

//====================================================================
// ISR DEFINITIONS
//====================================================================


//====================================================================
// FUNCTION DEFINITIONS
//====================================================================
/*
*   Function to configure system clock to 48 MHz
*/
void set_to_48MHz(void)
{
    if ((RCC->CFGR & RCC_CFGR_SWS) == RCC_CFGR_SWS_PLL)
    {
        RCC->CFGR &= (uint32_t) (~RCC_CFGR_SW);
        while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI);
    }
    RCC->CR &= (uint32_t)(~RCC_CR_PLLON);
    while ((RCC->CR & RCC_CR_PLLRDY) != 0);
    RCC->CFGR = ((RCC->CFGR & (~0x003C0000)) | 0x00280000);
    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0);
    RCC->CFGR |= (uint32_t) (RCC_CFGR_SW_PLL);
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

void init_student(void){
    init_LCD();
    lcd_command(CLEAR);
    lcd_putstring("MDZTIN014_test");
}

void init_ADC(void){
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;  // Enable GPIOA clock
    GPIOA->MODER |= GPIO_MODER_MODER6 | GPIO_MODER_MODER5;    // Set PA6 and PA5 to Analog
    
    RCC -> APB2ENR |= RCC_APB2ENR_ADCEN;    // Enable ADC clock
    ADC1 -> CR &= ~ADC_CR_ADEN;             // Ensure ADC is disabled before configuration

    ADC1 -> CFGR1 &= ~(ADC_CFGR1_RES | ADC_CFGR1_ALIGN); // Clear resolution and alignment
    ADC1 -> CFGR1 |= (ADC_CFGR1_RES_0); // 10-bit resolution

    // Select Channel 6 (PA6) and Channel 5 (PA5)
    ADC1 -> CHSELR = ADC_CHSELR_CHSEL6 | ADC_CHSELR_CHSEL5;

    ADC1->CR |= ADC_CR_ADCAL;               // Perform ADC calibration
    while ((ADC1->CR & ADC_CR_ADCAL) != 0); // Wait until calibration finishes

    ADC1->CR |= ADC_CR_ADEN; // Set ADEN=1 in ADC_CR register, actually starts ADC
    while(!(ADC1 -> ISR & ADC_ISR_ADRDY)); // Wait for ADC to be ready

    NVIC_EnableIRQ(ADC1_COMP_IRQn);         // Enable Interrupt on ADC

    // Start ADC conversion
    ADC1->CR |= ADC_CR_ADSTART;
}

void init_TIM3(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;  // Enable the TIM3 clock

    // Set PB4 to alternate function mode, AF1 for TIM3 ch3 and ch4, (set to AF1 to connect to the PWM channels of TIM3)
    RCC -> AHBENR |= RCC_AHBENR_GPIOBEN; // Ensure GPIOB clock is enabled
    GPIOB -> MODER &= ~GPIO_MODER_MODER4; //clear bits first
    GPIOB -> MODER |= GPIO_MODER_MODER4_1; // Set to AF mode
    GPIOB -> AFR[0] &= ~(0xF << GPIO_AFRL_AFRL4_Pos); //clears the existing AF setting.
    GPIOB -> AFR[0] |= (1 << GPIO_AFRL_AFRL4_Pos); // set to AF1

    // PWM mode on CH3
    TIM3 -> CCMR2 &= ~(TIM_CCMR2_OC3M); //clear bits
    TIM3 -> CCMR2 |= (6 << TIM_CCMR2_OC3M_Pos); // PWM mode 1 for CH3 (110)
    TIM3 -> CCMR2 |= TIM_CCMR2_OC3PE; // Preload enable

    TIM3->CCER |= (TIM_CCER_CC3E); // Enable output on CH3

    TIM3 -> PSC = 3;  // 48Mhz / (3+1) = 12Mhz
    TIM3 -> ARR = 599;    // 12Mhz/(599+1) = 20khz
    TIM3 -> CCR3 = 0; // PB4 Initial Duty (CH3) (adc value)

    TIM3 -> CR1 |= TIM_CR1_ARPE; // Enable auto-reload preload
    TIM3 -> CR1 |= TIM_CR1_CEN; // Start the timer
}

void ADC1_COMP_IRQHandler(void){
    if (ADC1->ISR & ADC_ISR_EOC) { // Check if it's the end of a conversion
        // Read 10-bit ADC result (0–1023)
        uint16_t adc_val = (uint16_t)(ADC1->DR & 0x3FF);;
        
        // Alternate between channel 6 (PA6) and channel 5 (PA5)
        if (adc_channel == 0) {
            comm_pos = adc_val;  // PA6, Channel 6
            adc_channel = 1;
        } else {
            real_pos = adc_val;  // PA5, Channel 5
            adc_channel = 0;
        }
        ADC1->CR |= ADC_CR_ADSTART; // Start next conversion
        ADC1->ISR |= ADC_ISR_EOC;   // Clear EOC flag
    }
}

float PI_control(uint16_t comm_pos, uint16_t real_pos){
    int32_t err = (int32_t)comm_pos - (int32_t)real_pos;

    float g = K_p * err;
    float o = (g * (2 + I * T_s) + gain * (I * T_s - 2) + 2 * out) / 2;

    // limit to the ADC's range of 0-1024
    if (o > 1023.0f){
        o = 1023.0f;
    } 
    if (o < 0.0f){
        o = 0.0f;
    }

    gain = g;
    out = o;
    return o;

}

void init_TIM14(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM14EN; // Enable TIM14 clock

    TIM14->PSC = 47999; // 48MHz / (47999 + 1) = 1kHz
    TIM14->ARR = 0;     // every 1ms
    TIM14->DIER |= TIM_DIER_UIE; // Enable update interrupt
    TIM14->CR1 |= TIM_CR1_CEN;   // Enable the timer

    NVIC_EnableIRQ(TIM14_IRQn);  // Enable TIM14 interrupt
}

void TIM14_IRQHandler(void){
    if (TIM14->SR & TIM_SR_UIF) {
        // Clear the interrupt flag
        TIM14->SR &= ~TIM_SR_UIF;

        // Run the PI controller
        uint16_t pwm_val = (uint16_t) PI_control(comm_pos, real_pos);

        // Update the PWM duty cycle (scaled)
        TIM3->CCR3 = (pwm_val * 600) / 1024;
    }
}

//********************************************************************
// END OF PROGRAM
//********************************************************************