# LoRa GPS Tracker Node (RP2040 + FreeRTOS + SX1262)

Embedded C firmware for a battery-style GPS tracker node. It reads NMEA sentences from a GPS module over UART, extracts latitude and longitude, and sends them to a gateway over LoRa (903 MHz). Delivery is reliable: the node waits for an acknowledgment on a separate frequency and retries with randomized exponential backoff if none arrives.

**Stack:** C, Raspberry Pi Pico SDK (RP2040), FreeRTOS (static allocation only), Semtech SX126x driver, SPI, UART, GPIO interrupts.

---

## What it demonstrates

- **RTOS design:** two tasks communicating through statically allocated queues, with ISRs feeding tasks using the `FromISR` APIs and `portYIELD_FROM_ISR`.
- **Interrupt-driven I/O:** UART RX interrupt for GPS bytes, and a GPIO interrupt on the radio's DIO1 pin for TX done, RX done and RX timeout.
- **Driver/HAL layering:** Semtech's portable SX126x driver on top of a small RP2040 HAL (`sx126x_hal.c`) that handles SPI transfers, chip select and the BUSY handshake.
- **A simple MAC-layer protocol:** ACK-based reliable delivery, frequency-split data/ACK channels and slotted random backoff for collisions.
- **A hand-written NMEA parser** that runs as a byte-at-a-time state machine, with no `malloc` and no `sscanf`.

---

## Architecture

```
 GPS module ──UART0 (9600 8N1)──► on_uart_rx() ISR
                                        │ xQueueSendToBackFromISR
                                        ▼
                               uartReceiveQueue (256 × char)
                                        │
                                        ▼
                        ┌─────────────────────────────┐
                        │ Task: "Process UART"        │
                        │  NMEA state machine ($GPGGA)│
                        │  → 11-byte packet           │
                        └──────────────┬──────────────┘
                                       │ 11 × xQueueSendToBack
                                       ▼
                             loraPendingTxQueue (256 × uint8_t)
                                       │
                                       ▼
                        ┌─────────────────────────────┐
                        │ Task: "Send LoRa"           │
                        │  write buffer, set_tx       │
                        │  block on ACK queue         │
                        │  retry w/ random backoff    │
                        └──────────────┬──────────────┘
                                       │ SPI1
                                       ▼
                                   SX1262 radio ──DIO1──► on_lora_irq() ISR
                                                              │
                              loraRxAckQueue (1 × uint8_t) ◄──┘
                              (1 = ACK received, 0 = timeout / not for me)
```

### Tasks and ISRs

| Component | Role |
|---|---|
| `on_uart_rx` (ISR) | Drains the UART FIFO into `uartReceiveQueue`. |
| `receiveUartTaskCode` | Parses NMEA, builds the packet, enqueues it for TX, then sleeps for 10 s (`vTaskDelay(10000)` at a 1 kHz tick) and flushes stale UART bytes so the node reports at a fixed rate. |
| `sendLoRaTaskCode` | Dequeues an 11-byte packet, loads it into the radio, transmits, then blocks until the ISR reports an ACK or a timeout. |
| `on_lora_irq` (ISR) | Handles `TX_DONE` (switch to the ACK frequency and listen), `RX_DONE` (read the payload and check whether it is addressed to this node) and `TIMEOUT` (report failure). |

### Radio state machine (all driven from the DIO1 interrupt)

```
 set_tx @ 903 MHz ──► TX_DONE ──► retune to 905 MHz, set_rx (2 slots)
                                        │
                  ┌─────────────────────┼─────────────────────┐
               RX_DONE               TIMEOUT
          first byte == MY_ID?       retune to 903 MHz
            yes → ACK=1              ACK=0
            no  → ACK=0
```

Data goes out on **903 MHz** and ACKs come back on **905 MHz**, so the node never has to receive on the channel it just transmitted on.

### Retry / backoff

On a failed attempt `k` (1, 2, 3, ...), the task waits `46 ms × random(0 … 2^k − 1)` before retransmitting. This is binary exponential backoff using the RP2040 hardware random source (`get_rand_32()`). The 46 ms slot time is roughly one packet airtime at SF7 / 125 kHz, so each random slot is long enough for one transmission to finish.

---

## Packet format (11 bytes)

| Byte(s) | Content |
|---|---|
| 0 | `100`, the gateway address |
| 1 | Source node ID (`MY_ID`) |
| 2 | Source node ID (`MY_ID`) |
| 3–6 | Latitude, `float` (little-endian), signed (negative = S) |
| 7–10 | Longitude, `float` (little-endian), signed (negative = W) |

Latitude and longitude are copied in raw from the NMEA fields, so they are in `ddmm.mmmm` format rather than decimal degrees. Converting to decimal degrees is left to the receiver.

---

## Radio configuration

| Parameter | Value |
|---|---|
| Modulation | LoRa, SF7, BW 125 kHz, CR 4/5 |
| TX frequency | 903 MHz |
| ACK frequency | 905 MHz |
| TX power | 22 dBm (SX1262 high-power PA, datasheet-optimal PA config) |
| Preamble | 12 symbols |
| Header | Explicit, CRC on |
| RF switch | Controlled by DIO2 |
| RX boost | Enabled (register `0x08AC` = `0x96`) |

---

## Hardware and pinout

| Signal | RP2040 GPIO |
|---|---|
| GPS UART TX / RX | 16 / 17 (UART0, 9600 baud) |
| LoRa SPI1 TX (MOSI) | 11 |
| LoRa SPI1 RX (MISO) | 12 |
| LoRa SPI1 SCK | 14 |
| LoRa NSS (chip select) | 10 |
| LoRa DIO1 (interrupt) | 9 |
| LoRa BUSY | 15 |

SPI runs at 100 kHz, Mode 0. Debug output is printed over USB CDC (`pico_enable_stdio_usb`).

---

## Project layout

| File | Purpose |
|---|---|
| `main.c` | Application: tasks, queues, ISRs, radio init. |
| `sx126x_hal.c/.h`, `sx126x_hal_context.h` | RP2040 SPI/GPIO glue for the SX126x driver (BUSY wait, NSS control). |
| `sx126x.c/.h`, `sx126x_regs.h` | Semtech SX126x driver (vendor code, BSD-3-Clause; see file headers). |
| `FreeRTOSConfig.h` | Kernel config: static allocation only, 1 kHz tick, preemptive. |
| `CMakeLists.txt` and `*_import.cmake` | Build setup for the Pico SDK and FreeRTOS. |

---

## Building

Prerequisites: Pico SDK, ARM GNU toolchain, CMake and a copy of the FreeRTOS kernel with the RP2040 port.

1. Edit `FREERTOS_KERNEL_PATH` in `CMakeLists.txt` to point at your FreeRTOS `Source` directory. It is currently a hard-coded Windows path.
2. Set `PICO_SDK_PATH` (or let `pico_sdk_import.cmake` find it).
3. Build:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

4. Hold BOOTSEL, plug in the Pico and copy `project3_starter.uf2` to the drive that appears.
5. Open a serial terminal on the USB CDC port to see logs such as `Beginning reliable transmission.`, `Interrupt: TX done` and `Transmission Acknowledged!`.

To change this node's identity, edit `MY_ID`. The gateway must ACK by sending a packet whose first byte equals that ID, on 905 MHz.

---

## Design decisions worth discussing

- **Static allocation only** (`configSUPPORT_DYNAMIC_ALLOCATION 0`). All queues, tasks and stacks are known at compile time. There is no heap fragmentation and no allocation failure at runtime.
- **ISR does minimal work, tasks do the thinking.** The UART ISR only moves bytes into a queue. The radio ISR owns the radio state machine because TX/RX turnaround timing matters. It communicates with the sender task through a queue of length 1.
- **Separate ACK frequency** avoids self-interference and simplifies the state machine.
- **Randomized exponential backoff** spreads out retries when several nodes collide, without a coordinator.
- **Parser as a state machine** keeps RAM use constant and lets parsing run one byte at a time as data arrives.

---

## Known limitations and what I'd improve

I'm listing these deliberately. They are the first things I'd fix with more time.

1. **SPI and `printf` inside an ISR.** `on_lora_irq` does blocking SPI transfers (with a BUSY spin-wait) and `printf` over USB. That is fine for a class project, but a production design would have the ISR give a semaphore or task notification and handle the radio in a dedicated task.
2. **ISR RX buffer size.** `outBuffer[256]` lives on the ISR stack, and the payload length is not checked against any expected size or address layout before use.
3. **NMEA robustness.** There is no checksum verification and no fix-quality check (the GGA fix-quality field is skipped), so a "no fix" sentence with empty fields would still be transmitted. Only `$GPGGA` is handled, not `$GNGGA`.
4. **Parser buffer wrap.** A field longer than 16 characters silently wraps the temp buffer instead of rejecting the sentence.
5. **ACK matching.** An ACK is accepted if the first byte equals `MY_ID`, with no sequence number. A late ACK for an old packet could be mistaken for the current one. Adding a sequence number would fix this.
6. **Unbounded retries.** The send loop retries forever. A retry cap with a drop/log policy would be better, and backoff growth should be capped (`k` is also used as a shift amount of `32 - k`).
7. **Coordinates are sent as raw NMEA `ddmm.mmmm`** floats rather than decimal degrees or fixed-point integers, which would be more compact and precise.
8. **Duty cycle and power.** The node polls on a fixed 10 s timer and never sleeps the radio or MCU. Low-power operation (radio sleep, tickless idle) is not implemented.
9. **Minor cleanup.** There is a leftover unused `secondsID` variable, and commented-out test code remains in the UART task.

---

## Credits and licenses

- SX126x driver: © Semtech Corporation, BSD-3-Clause (see headers in `sx126x.c` / `sx126x.h`).
- FreeRTOS kernel: MIT License.
- Raspberry Pi Pico SDK: BSD-3-Clause.
- Application code: written as the University of Notre Dame EE30132 final project.
