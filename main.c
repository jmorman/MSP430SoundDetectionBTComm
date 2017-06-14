/* main.c for btsounddetectionv3
 * This software was created for an MSP430F5529. Whenever a loud sound is detected from the
 * ADC, it sends a UART message which can then be transmitted via a Bluetooth modem
 * to a receiver (e.g. a phone).
 * Detection is done using threshold values for different windows of measurements from the ADCs
 * Shorter windows can respond more quickly to a loud noise, but are more noisy and therefore
 * cannot pick up quiet sounds as consistently as longer windows.
 */

#include <msp430.h> 
#include <string.h>

#define OUTPUT_LEN 1024 //Useful when debugging to hold as many output values as memory permits
#define OUTPUT_32_THRESHOLD 940 //Threshold value for an element of output_64 to indicate that a sufficiently loud sound was recorded
#define OUTPUT_128_THRESHOLD 3100
#define OUTPUT_512_THRESHOLD 11550
#define OUTPUT_2048_THRESHOLD 43000
#define ADC_AVERAGE 1749

volatile unsigned short ADC_value;
unsigned short output_buffer[2048]; //Holds (up to) the last 2048 output values
unsigned int output_32[OUTPUT_LEN]; //Each element is the sum of 32 consecutive elements from output_buffer
unsigned int output_128[OUTPUT_LEN >> 2]; //Each element is the sum of 128 consecutive elements from output_buffer
unsigned int output_512[OUTPUT_LEN >> 4]; //Each element is the sum of 512 consecutive elements from output_buffer
unsigned int output_2048[OUTPUT_LEN >> 6]; //Each element is the sum of 2048 consecutive elements from output_buffer
float adc_average = ADC_AVERAGE;
volatile unsigned int timer_val = 0;


void uart_write(const char *str){
    unsigned int i;
    for(i = 0; i < strlen(str); i++){
        while (!(UCA0IFG&UCTXIFG));
        UCA0TXBUF = str[i];
    }
}

//Convert int to string
void intstr(long unsigned int value, char* result){
      char* reversed_valstr = result;
      char* valstr = result;
      char tempchar;
      int tempval;

      do {
        tempval = value;
        value /= 10;
        *reversed_valstr++ = "0123456789" [tempval - value * 10];
      } while ( value );
      *reversed_valstr-- = '\0'; //null terminate
      while(valstr < reversed_valstr) {//need to reverse
        tempchar = *reversed_valstr;
        *reversed_valstr--= *valstr;
        *valstr++ = tempchar;
      }

}

//Returns sum of output_buffer elements i-n to i-1
//Similar to an averaging filter
unsigned short calc_out_sum(unsigned short i, unsigned short n){
    if(i < n){
        return 0;
    }
    else{
        unsigned short sum = output_buffer[i-n];
        unsigned short j;
        for(j = i-1; j > i-n; j--){
            sum += output_buffer[j];
        }
        return sum;
    }
}

int main(void) {
      //ACLK = REFO = ~32768Hz, MCLK = SMCLK = default DCO = 32 x ACLK = 1048576Hz

      WDTCTL = WDTPW | WDTHOLD;   // Stop watchdog timer

//----------------------------UART-------------------------
      P3SEL |= BIT3+BIT4;                       // P3.3,4 = USCI_A0 TXD/RXD
      UCA0CTL1 |= UCSWRST;                      // Reset state machine
      UCA0CTL1 |= UCSSEL_2;                     // SMCLK
      UCA0BR0 = 9;                              // Used to get 115200 baud rate from a 1 MHz clock
      UCA0BR1 = 0;                              // 1MHz 115200 (pg951)
      UCA0MCTL |= UCBRS_1 + UCBRF_0;            // Modulation UCBRSx=1, UCBRFx=0 (pg951)
      UCA0CTL1 &= ~UCSWRST;                     // Initialize USCI state machine
      UCA0IE |= UCRXIE;                         // Enable USCI_A0 RX interrupt

//-----------------------------ADC-------------------------
      P1DIR |= 0x01;                            // Set P1.0 to output direction (blinking LED)
      P6SEL |= 0x01;                            // Enable pin for ADC
      REFCTL0 &= ~REFMSTR;                      // Reference control reset

      ADC12CTL0 = ADC12SHT0_8 + ADC12ON + ADC12REFON + ADC12REF2_5V;  // Sample and hold for 64 Clock cycles, ADC on, ADC interrupt enable

      ADC12CTL1 = ADC12SHP;                     // Channel 3, ADC12CLK/3
      ADC12MCTL0 = ADC12INCH_0 + ADC12SREF_1;   //0 to 2.5 V range
      ADC12IE = 0x001;

      __delay_cycles(100);

      ADC12CTL0 |= ADC12ENC;

//---------------------------TIMER-------------------------
      TA0CCTL0 = CCIE; //CCR0 interrupt enabled
      TA0CTL = TASSEL_1 + MC_1 + ID_0; //ACLK, upmode
      TA0CCR0 = 32768; //This will cause an interrupt once every second, used for keeping time
//---------------------------------------------------------
      unsigned int i = 0;
      unsigned int last_loud_time = 0;
      float output_value;

      while(1){
          char result[33];
          ADC12CTL0 |= ADC12SC;         // Begin analog to digital conversion

          __bis_SR_register(LPM0_bits + GIE);    // Low Power Mode 0 with interrupts enabled
          __no_operation();

          if((i & 31) == 0){ //just to reduce number of divide operations
              adc_average = (adc_average*i+ADC_value)/(i+1);
          }

          output_value = (ADC_value > ADC_AVERAGE) ? (ADC_value - ADC_AVERAGE) : (ADC_AVERAGE - ADC_value);
          output_buffer[i & 2047] = output_value;

          //Since the MSP430 does not have a native divide instruction,
          //Bitwise operations are used on numbers that are powers of 2 instead of modulo and divide operations
          //E.g. i % 8 == (i & 7), and i / 8 == (i >> 3)
          if(i > 0){
              if((i & 31) == 0){
                  output_32[i >> 5] = calc_out_sum(((i-1) & 2047) + 1, 32);
              }
              if((i & 127) == 0){
                  output_128[i >> 7] = calc_out_sum(((i-1) & 2047) + 1, 128);
              }
              if((i & 511) == 0){
                  output_512[i >> 9] = calc_out_sum(((i-1) & 2047) + 1, 512);
              }
              if((i & 2047) == 0){
                  output_2048[i >> 11] = calc_out_sum(((i-1) & 2047) + 1, 2048);
              }
          }

          //char result[33];
          unsigned short current_val_32 = output_32[i >> 5];
          unsigned int current_val_128 = output_128[i >> 7];
          unsigned int current_val_512 = output_512[i >> 9];
          unsigned int current_val_2048 = output_2048[i >> 11];
          if(((i & 31) == 0) && (timer_val - last_loud_time > 0) && (current_val_32 > OUTPUT_32_THRESHOLD)){
              P1OUT = 0x01;
              last_loud_time = timer_val;
              intstr(timer_val, result);
              uart_write(result);
              uart_write(" sec, window ");
              uart_write("32: ");
              intstr(current_val_32, result);
              uart_write(result);
              uart_write("\n");
          }
          else if(((i & 127) == 0) && (timer_val - last_loud_time > 0) && (current_val_128 > OUTPUT_128_THRESHOLD)){
              P1OUT = 0x01; //Turn on LED
              last_loud_time = timer_val;
              intstr(timer_val, result);
              uart_write(result);
              uart_write(" sec, window ");
              uart_write("128: ");
              intstr(current_val_128, result);
              uart_write(result);
              uart_write("\n");
          }
          else if(((i & 511) == 0) && (timer_val - last_loud_time > 0) && (current_val_512 > OUTPUT_512_THRESHOLD)){
              P1OUT = 0x01;
              last_loud_time = timer_val;
              intstr(timer_val, result);
              uart_write(result);
              uart_write(" sec, window ");
              uart_write("512: ");
              intstr(current_val_512, result);
              uart_write(result);
              uart_write("\n");
          }
          else if(((i & 2047) == 0) && (timer_val - last_loud_time > 0) && (current_val_2048 > OUTPUT_2048_THRESHOLD)){
              P1OUT = 0x01;
              last_loud_time = timer_val;
              intstr(timer_val, result);
              uart_write(result);
              uart_write(" sec, window ");
              uart_write("2048: ");
              intstr(current_val_2048, result);
              uart_write(result);
              uart_write("\n");
          }
          else{
              P1OUT = 0x00; // Turn LED off
          }

          i++;

          if(i == OUTPUT_LEN * 8){ // Loop over
              i = 0;
          }

          __no_operation();
      }

}

// UART interrupt service routine
#pragma vector=USCI_A0_VECTOR
__interrupt void USCI_A0_ISR(void)
{
    switch(__even_in_range(UCA0IV,4))
    {
    case 0:break;                               // no interrupt
    case 2:                                     // RXIFG
        while (!(UCA0IFG&UCTXIFG));             // Wait for USCI_A0 buffer to be ready
        if(UCA0RXBUF == 'a' || UCA0RXBUF == 'e' || UCA0RXBUF == 'i' || UCA0RXBUF == 'o' || UCA0RXBUF == 'u' || UCA0RXBUF == '\n')
            UCA0TXBUF = UCA0RXBUF;
        else
            UCA0TXBUF = UCA0RXBUF+1;
        break;
    case 4:break;                               // TXIFG
    default:break;
    }
}

// ADC12 interrupt service routine
#pragma vector=ADC12_VECTOR
__interrupt void ADC12_ISR (void)
{
    switch(__even_in_range(ADC12IV,34)) {
    case  6:                                  // Vector  6:  ADC12IFG0
      ADC_value = ADC12MEM0;                  // Move results, IFG is cleared
      __bic_SR_register_on_exit(LPM0_bits);   // Exit active CPU
      break;
    default: break;
    }
}

// Timer interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A0_ISR(void)
{
    timer_val += 1;
}
