#include <application.h>
#include <stm32l0xx.h>

// LED instance
bc_led_t led;

// Button instance
bc_button_t button;

void uart_init(void)
{
    // Enable GPIOA clock
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;

    // Configure TXD0 pin as alternate function
    GPIOA->MODER &= ~(1 << GPIO_MODER_MODE0_Pos);

    // Select AF6 alternate function for TXD0 pin
    GPIOA->AFR[0] |= GPIO_AF6_USART4 << GPIO_AFRL_AFRL0_Pos;

    // Enable clock for USART4
    RCC->APB1ENR |= RCC_APB1ENR_USART4EN;

    // Invert TXD signal
    USART4->CR2 = USART_CR2_TXINV;

    // Enable transmitter
    USART4->CR1 = USART_CR1_TE | USART_CR1_OVER8;

    // Configure baudrate
    USART4->BRR = 0x10;

    // Enable USART4
    USART4->CR1 |= USART_CR1_UE;
}

void application_init(void)
{
    /*
    uart_init();
    while (true)
    {
        if ((USART4->ISR & USART_ISR_TXE) != 0)
        {
            USART4->TDR = 0xaa;
        }
    }
    */

    bc_module_power_init();
    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_ON);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, NULL);
}

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_BUTTON_EVENT_PRESS)
    {
        bc_led_set_mode(&led, BC_LED_MODE_TOGGLE);
    }
}
