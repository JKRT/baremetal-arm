#include <stdbool.h>
#include <math.h>
#include "uart_pl011.h"
#include "irq.h"

static uart_registers* uart0 = (uart_registers*)0x10009000u;
static const uint32_t refclock = 24000000u; /* 24 MHz */

uart_error uart_init(void) {

    return UART_OK;
}

uart_error uart_configure(uart_config* config) {
    /* Validate config */
    if (config->data_bits < 5u || config->data_bits > 8u) {
        return UART_INVALID_ARGUMENT_WORDSIZE;
    }
    if (config->stop_bits == 0u || config->stop_bits > 2u) {
        return UART_INVALID_ARGUMENT_STOP_BITS;
    }
    if (config->baudrate < 110u || config->baudrate > 460800u) {
        return UART_INVALID_ARGUMENT_BAUDRATE;
    }
    /* Disable the UART */
    uart0->CR &= ~CR_UARTEN;
    /* Finish any current transmission, and flush the FIFO */
    while (uart0->FR & FR_BUSY);
    uart0->LCRH &= ~LCRH_FEN;

    /* Set baudrate */
    double intpart, fractpart;
    double baudrate_divisor = (double)refclock / (16u * config->baudrate);
    fractpart = modf(baudrate_divisor, &intpart);

    uart0->IBRD = (uint16_t)intpart;
    uart0->FBRD = (uint8_t)((fractpart * 64u) + 0.5);

    uint32_t lcrh = 0u;

    /* Set data word size */
    switch (config->data_bits)
    {
    case 5:
        lcrh |= LCRH_WLEN_5BITS;
        break;
    case 6:
        lcrh |= LCRH_WLEN_6BITS;
        break;
    case 7:
        lcrh |= LCRH_WLEN_7BITS;
        break;
    case 8:
        lcrh |= LCRH_WLEN_8BITS;
        break;
    }

    /* Set parity. If enabled, use even parity */
    if (config->parity) {
        lcrh |= LCRH_PEN;
        lcrh |= LCRH_EPS;
        lcrh |= LCRH_SPS;
    } else {
        lcrh &= ~LCRH_PEN;
        lcrh &= ~LCRH_EPS;
        lcrh &= ~LCRH_SPS;
    }

    /* Set stop bits */
    if (config->stop_bits == 1u) {
        lcrh &= ~LCRH_STP2;
    } else if (config->stop_bits == 2u) {
        lcrh |= LCRH_STP2;
    }

    /* Enable FIFOs */
    lcrh |= LCRH_FEN;

    uart0->LCRH = lcrh;

    uart0->IMSC |= IMSC_RXIM;

    /* Register the interrupt */
    (void)irq_register_isr(UART0_INTERRUPT, uart_isr);

    /* Enable the UART */
    uart0->CR |= CR_UARTEN;

    return UART_OK;
}

void uart_putchar(char c) {
    while (uart0->FR & FR_TXFF);
    uart0->DR = c;
}

void uart_write(const char* data) {
    while (*data) {
        uart_putchar(*data++);
    }
}

void uart_write_uint(uint32_t num) {
    char buf[8];
    int8_t i = 0;
    while (num != 0) {
        uint8_t remainder = num % 10;
        buf[i++] = '0' + remainder;
        num /= 10;
    }
    for (i--; i >= 0; i--) {
        uart_putchar(buf[i]);
    }
}

uart_error uart_getchar(char* c) {
    if (uart0->FR & FR_RXFE) {
        return UART_NO_DATA;
    }

    *c = uart0->DR & DR_DATA_MASK;
    if (uart0->RSRECR & RSRECR_ERR_MASK) {
        /* The character had an error */
        uart0->RSRECR &= RSRECR_ERR_MASK;
        return UART_RECEIVE_ERROR;
    }
    return UART_OK;
}

void uart_isr(void) {
    uint32_t status = uart0->MIS;
    if (status & RX_INTERRUPT) {
        /* Read the received character and print it back*/
        char c = uart0->DR & DR_DATA_MASK;
        uart_putchar(c);
        if (c == '\r') {
            uart_write("\n");
        }
    } else if (status & BE_INTERRUPT) {
        uart_write("Break error detected!\n");
        /* Clear the error flag */
        uart0->RSRECR = ECR_BE;
        /* Clear the interrupt */
        uart0->ICR = BE_INTERRUPT;
    }
}
