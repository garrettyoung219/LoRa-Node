// Step 05 - First place we can TEST our code so far!
// have our uart task just spit out whatever is being pushed to it
// create task and the task's function

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Library includes. */
#include <stdio.h>
#include "pico/stdlib.h" // configures pico clocks, hardware, etc.
#include "hardware/spi.h" // also add to CMakeLists.txt
#include <string.h>
#include "pico/rand.h"
#include <stdlib.h>

/* LoRa Driver Includes. */
#include "sx126x.h"
#include "sx126x_hal_context.h"

// create macros so we can easily change later, put them up top in one spot.
#define UART_RECEIVE_QUEUE_SIZE 256
#define LORA_PENDING_TX_QUEUE_SIZE 256
#define RECEIVE_UART_TASK_SIZE 1024
#define SEND_LORA_TASK_SIZE 1024

#define UART_TX_PIN 16 // ND BOARD
#define UART_RX_PIN 17 // ND BOARD

#define LORA_NSS  10
#define LORA_DIO1 9
#define LORA_BUSY 15
#define LORA_TX   11  // SPI1
#define LORA_RX   12  // SPI1
#define LORA_SCK  14  // SPI1

#define LORA_FREQ_IN_HZ (903*1000*1000) // 903 MHz
#define LORA_ACK_FREQ_IN_HZ (905*1000*1000) // 905 MHz
#define LORA_POWER_IN_DBM 22 // 22 max

#define MY_ID 41
#define TX_SLOT_TIME 46

void init_radio(const uint32_t lorafreq_hz,
                const int8_t power_dbm,
                sx126x_mod_params_lora_t * lora_mod_params);
void init_hardware();

// allocate memory for our FreeRTOS objects. Will be defined later.
StaticQueue_t uartReceiveQueue;
uint8_t uartReceiveQueueStorage[ UART_RECEIVE_QUEUE_SIZE * sizeof( char )  ];
QueueHandle_t uartReceiveQueueHandle = NULL;

StaticQueue_t loraRxAckQueue;
uint8_t loraRxAckQueueStorage[ 1 * sizeof( uint8_t )  ]; // Just 1 uint8_t for 0 or 1
QueueHandle_t loraRxAckQueueHandle = NULL;

StaticQueue_t loraPendingTxQueue;
uint8_t loraPendingTxQueueStorage[ LORA_PENDING_TX_QUEUE_SIZE * sizeof( uint8_t )  ];
QueueHandle_t loraPendingTxQueueHandle = NULL;

StaticTask_t receiveUartTask;
StackType_t receiveUartTaskStack[ RECEIVE_UART_TASK_SIZE ];
TaskHandle_t receiveUartTaskHandle = NULL;

StaticTask_t sendLoRaTask;
StackType_t sendLoRaTaskStack[ SEND_LORA_TASK_SIZE ];
TaskHandle_t sendLoRaTaskHandle = NULL;

//LoRa context is global so it can be accessed in an interrupt
// not volatile since it won't be changed in an interrupt
const lora_spi_context_t lora_context = {
    .spihw = spi1,
    .nss_pin = LORA_NSS,
    .busy_pin = LORA_BUSY};

// RX interrupt handler
// this is pretty much exactly from FreeRTOS's documentation
// see https://www.freertos.org/xQueueSendToBackFromISR.html
void on_uart_rx()
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE; // track if we woke up a task
    while (uart_is_readable(uart0)) {
        char ch = uart_getc(uart0);
        xQueueSendToBackFromISR( uartReceiveQueueHandle, &ch, &xHigherPriorityTaskWoken );
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // tells the scheduler to do a context switch
}

// this is a task function. It takes a pointer to the function's parameters
// most of the time we don't need parameters
void receiveUartTaskCode( void * pvParameters )
{
    // if you need/use parameters, do that here. But often you ignore them.

    // let's just print whatever we get from UART to the console
    char ch;
    uint8_t secondsID = 41;
    float lat = 1.23, longi = -2.34;
    char temp[16];
    char * tempPointer = &temp[0];
    int gpsParsing = 0;
    int fieldNum = 0;
    uint8_t bufferTX[11];

    // variables to store processed data
    float latitude, longitude;
    char eastwest, northsouth;
    int hour, minutes;
    float seconds;

    // arrays to format time data
    char hour_str[3];
    char min_str[3];
    char sec_str[7];
    while(xQueueReceive( uartReceiveQueueHandle, &ch, portMAX_DELAY ))
    {
        // find beginning of message before parsing
        if (gpsParsing == 0) {
            printf("%c", ch);
            if (ch == '$') {
                printf("\n");
                gpsParsing = 1;
            }
        }
        // parse message
        else {
            if (ch != ',') {
                *tempPointer = ch;
                tempPointer++;
                if ((tempPointer - &temp[0]) >= sizeof(temp)/sizeof(temp[0])) {
                    tempPointer = &temp[0];
                }
            } 
            else if (ch == ','){
                *tempPointer = '\0';
                switch(fieldNum) {
                    case 0:
                        /* check if  tempBuffer is "GPGGA" */
                        if (strcmp(temp, "GPGGA") != 0) {
                            gpsParsing = 0; // Wrong message -- stop parsing.
                            fieldNum = 0;
                        }
                        else {
                            fieldNum++;
                        }
                        break;
                    case 1:
                        /* process UTC time */
                        hour_str[0] = temp[0];
                        hour_str[1] = temp[1];
                        hour_str[2] = '\0';
                        hour = atoi(hour_str);

                        min_str[0] = temp[2]; 
                        min_str[1] = temp[3]; 
                        min_str[2] = '\0';
                        minutes = atoi(min_str);

                        sec_str[0] = temp[4];
                        sec_str[1] = temp[5];
                        sec_str[2] = temp[6];
                        sec_str[3] = temp[7];
                        sec_str[4] = temp[8];
                        sec_str[5] = temp[9];
                        sec_str[6] = '\0';
                        seconds = atof(sec_str);

                        fieldNum++;
                        break;
                    case 2:
                        /* process latitude */
                        latitude = atof(temp);
                        fieldNum++;
                        break;
                    case 3:
                        /* N/S */
                        northsouth = temp[0];
                        fieldNum++;
                        break;
                    case 4:
                        /* process longitude */
                        longitude = atof(temp);
                        fieldNum++;
                        break;
                    case 5:
                        /* E/W */
                        /* Last case we care about for HW 4 */
                        eastwest = temp[0];
                        fieldNum = 0;
                        gpsParsing = 0;

                        // load data into TX buffer
                        uint8_t secondsID = (uint8_t) seconds;
                        if (northsouth == 'S'){
                            latitude *= -1;
                        }
                        if (eastwest == 'W'){
                            longitude *= -1;
                        }
                        bufferTX[0] = 100;
                        bufferTX[1] = MY_ID;
                        bufferTX[2] = MY_ID;
                        memcpy(&bufferTX[3], &latitude, 4);
                        memcpy(&bufferTX[7], &longitude, 4);

                        // send to tx queue!
                        for (int i=0; i<11; i++) {
                            xQueueSendToBack(loraPendingTxQueueHandle, &bufferTX[i], portMAX_DELAY);
                        }
                        vTaskDelay(10000); // 1 min
                        xQueueReset(uartReceiveQueueHandle);

                        //printf("Lat: %f, Long: %f, E/W: %c, N/S: %c, Time: %d:%d:%f\n", latitude, longitude, eastwest, northsouth, hour, minutes, seconds);
                        break;       
                }
               tempPointer = &temp[0];
            }  
        }
        //printf("%c", ch); // just print
        // put your GPS stuff here
        // temp[0] = 100;
        // temp[1] = 255;
        // temp[2] = MY_ID;
        // memcpy(&temp[3], &lat, 4);
        // memcpy(&temp[7], &longi, 4);
        // for (int i = 0; i < 11; i++) {
        //     xQueueSendToBack(loraPendingTxQueueHandle, &temp[i], portMAX_DELAY);
        // }
        // vTaskDelay(20000); // 20 seconds
        // xQueueReset(uartReceiveQueueHandle);
    }
}

void sendLoRaTaskCode( void * pvParameters)
{
    char ch;
    uint8_t ack = 0;
    uint8_t temp[11];
    sx126x_pkt_params_lora_t lora_pkt_params = {
            .preamble_len_in_symb = 12, // default is 12
            .header_type          = SX126X_LORA_PKT_EXPLICIT, // maybe use implicit to save bytes
            .pld_len_in_bytes     = 11, // payload to TX or max size for RX (although 0 works...)
            .crc_is_on            = true, // in case anyone wants to check
            .invert_iq_is_on      = false
        };
        
    while (1)
    {
        for (int i = 0; i < 11; i++){
            xQueueReceive(loraPendingTxQueueHandle, &temp[i], portMAX_DELAY);
        }
        printf("Beginning reliable transmission.\n");
        sx126x_write_buffer(&lora_context,0, temp, 11);
        sx126x_set_lora_pkt_params(&lora_context, &lora_pkt_params);
        sx126x_set_tx(&lora_context, 0); 

        xQueueReceive(loraRxAckQueueHandle, &ack, portMAX_DELAY);
        int k = 0;
        
        while(!ack)
        {
            k++;
            printf("Failure number: %d\n", k);
            uint32_t delay = TX_SLOT_TIME * (get_rand_32()>>(32-k)); // 46ms to TX
            vTaskDelay(delay);
            sx126x_set_tx(&lora_context, 0); 
            xQueueReceive(loraRxAckQueueHandle, &ack, portMAX_DELAY);
        }
        printf("Transmission Acknowledged!\n\n");
    }
}

void on_lora_irq(uint gpio, uint32_t events){
    // acknowledge the IRQ
    sx126x_irq_mask_t irqs;
    sx126x_get_and_clear_irq_status( &lora_context, &irqs);
    // update state machine
    // can't have both at same time, either it received or transmitted data or timed out
    if (irqs & SX126X_IRQ_RX_DONE ) {
        uint8_t outBuffer[256];
        uint8_t ack;
        sx126x_set_rf_freq(&lora_context, LORA_FREQ_IN_HZ);
        sx126x_rx_buffer_status_t rx_buffer_status;
        sx126x_get_rx_buffer_status(&lora_context, &rx_buffer_status);
        sx126x_read_buffer( &lora_context,
                            rx_buffer_status.buffer_start_pointer, // start reading here
                            &outBuffer[0],                         // put data here
                            rx_buffer_status.pld_len_in_bytes );   // read this many bytes
                                
        if (outBuffer[0] == MY_ID)
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE; // track if we woke up a task
            ack = 1;
            xQueueSendToBackFromISR( loraRxAckQueueHandle, &ack, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // tells the scheduler to do a context switch
        }
        else
        {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE; // track if we woke up a task
            ack = 0;
            xQueueSendToBackFromISR( loraRxAckQueueHandle, &ack, &xHigherPriorityTaskWoken );
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // tells the scheduler to do a context switch
        }
        printf("Interrupt: RX done\n");
    }
    if (irqs & SX126X_IRQ_TX_DONE) {
        sx126x_set_rf_freq(&lora_context, LORA_ACK_FREQ_IN_HZ);
        sx126x_set_rx(&lora_context, TX_SLOT_TIME*2);
        printf("Interrupt: TX done\n");
    }
    if (irqs & SX126X_IRQ_TIMEOUT ) {
        sx126x_set_rf_freq(&lora_context, LORA_FREQ_IN_HZ);
        BaseType_t xHigherPriorityTaskWoken = pdFALSE; // track if we woke up a task
        int ack = 0;
        xQueueSendToBackFromISR( loraRxAckQueueHandle, &ack, &xHigherPriorityTaskWoken );
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken); // tells the scheduler to do a context switch
        printf("Interrupt: RX timeout\n");
    }
}

void main() {
    // Set up the USB and SPI
    init_hardware();
    sleep_ms(1000);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // initialize uart0 at 9600 baud (with other settings default "8-bit no parity")
    uart_init(uart0, 9600);
    // Set up a RX interrupt
    // We need to set up the handler first
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    // Tell this core to look for uart0 IRQs
    irq_set_enabled(UART0_IRQ, true);
    // Tell UART0 to start sending interrupts - RX only
    uart_set_irq_enables(uart0, true, false);
    
    // Set up the radio
    sx126x_mod_params_lora_t lora_mod_params = {
        .sf = SX126X_LORA_SF7, // spreading factor
        .bw = SX126X_LORA_BW_125, // bandwidth
        .cr = SX126X_LORA_CR_4_5, // coding rate
        .ldro = 0 // low data rate optimization
    };
    init_radio(LORA_FREQ_IN_HZ,     // freq in HZ
               LORA_POWER_IN_DBM,   // power in dBm
               &lora_mod_params);   //  modulation parameters
    
    uartReceiveQueueHandle = xQueueCreateStatic( UART_RECEIVE_QUEUE_SIZE, // how many elements are in the queue
                                                 sizeof( char ),          // what is the data type of an element
                                                 uartReceiveQueueStorage, // where is the storage
                                                 &uartReceiveQueue );     // where to store information about queue's context

    loraRxAckQueueHandle = xQueueCreateStatic( 1,                     // how many elements are in the queue
                                               sizeof( uint8_t ),     // what is the data type of an element
                                               loraRxAckQueueStorage, // where is the storage
                                               &loraRxAckQueue );     // where to store information about queue's context
    
    loraPendingTxQueueHandle = xQueueCreateStatic( LORA_PENDING_TX_QUEUE_SIZE, // how many elements are in the queue
                                                   sizeof( uint8_t ),          // what is the data type of an element
                                                   loraPendingTxQueueStorage,  // where is the storage
                                                   &loraPendingTxQueue );      // where to store information about queue's context
    
    // https://www.freertos.org/xTaskCreateStatic.html
    receiveUartTaskHandle = xTaskCreateStatic( receiveUartTaskCode, // function
                                 "Process UART", // task name (used for debugging)
                                 RECEIVE_UART_TASK_SIZE, // stack size
                                 NULL, // parameters to pass
                                 tskIDLE_PRIORITY+1, // priority one more than the idle task
                                 receiveUartTaskStack, // stack
                                 &receiveUartTask );  // task control buffer

    sendLoRaTaskHandle = xTaskCreateStatic( sendLoRaTaskCode, // function
                                 "Send LoRa", // task name (used for debugging)
                                 SEND_LORA_TASK_SIZE, // stack size
                                 NULL, // parameters to pass
                                 tskIDLE_PRIORITY+1, // priority one more than the idle task
                                 sendLoRaTaskStack, // stack
                                 &sendLoRaTask );  // task control buffer

    vTaskStartScheduler(); // let it rip!

}

void init_radio(const uint32_t lorafreq_hz,
                const int8_t power_dbm,
                sx126x_mod_params_lora_t * lora_mod_params)
{
    sx126x_set_pkt_type(&lora_context, SX126X_PKT_TYPE_LORA);
    sx126x_set_rf_freq(&lora_context, lorafreq_hz);
    
    // Set the Power Amplifier parameters
    // Taken from the datasheet, optimal for sx1262 at 22 dBm
    const sx126x_pa_cfg_params_t pa_params = {
        .pa_duty_cycle = 0x04,
        .hp_max = 0x07,
        .device_sel = 0x00,
        .pa_lut =0x01,
    };
    sx126x_set_pa_cfg(&lora_context, &pa_params);
    sx126x_set_tx_params(&lora_context, power_dbm, SX126X_RAMP_200_US);

    sx126x_set_lora_mod_params(&lora_context, lora_mod_params);
    // Our module uses a radio frequency switch to switch between RX and TX
    sx126x_set_dio2_as_rf_sw_ctrl( &lora_context, true );
    // enable RX boost (10% higher current, maybe better performance)
    // not sure if it's worth it
    uint8_t boost_rx = 0x96;
    sx126x_write_register( &lora_context, 0x08AC, &boost_rx, 1);

    // next, set up an IRQ for when a packet is received
    sx126x_set_dio_irq_params( &lora_context,
                               SX126X_IRQ_ALL, // enable the IRQ_RX_DONE interrupt
                               SX126X_IRQ_RX_DONE | SX126X_IRQ_TX_DONE | SX126X_IRQ_TIMEOUT, // DIO1 will go high when IRQ_RX_DONE is triggered
                               SX126X_IRQ_NONE,    // DIO2 will not go high for any interrupt
                               SX126X_IRQ_NONE);   // DIO3 will not go high for any interrupt
    gpio_init(LORA_DIO1); // sets the pin to GPIO function and sets it to input by default
    gpio_set_irq_enabled_with_callback( LORA_DIO1,          // set up this pin as interrupt pin
                                        GPIO_IRQ_EDGE_RISE, // on rising edge, triger interrupt
                                        true,               // enable it immediately 
                                        &on_lora_irq);        // run this function when irq happens
}

void init_hardware()
{
    stdio_init_all();
    // set up LoRa
    spi_init(spi1, 100 * 1000); // set up SPI at 100,000 kHz clock. Defaults Motorola CPOL=0, CPHA=0
    gpio_set_function(LORA_TX, GPIO_FUNC_SPI);
    gpio_set_function(LORA_RX, GPIO_FUNC_SPI);
    gpio_set_function(LORA_SCK, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_init(LORA_NSS);
    gpio_set_dir(LORA_NSS, GPIO_OUT);
    gpio_put(LORA_NSS, 1);
}