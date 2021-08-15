# samd21-dma-gpio_high_time_and_period
This is a re-write of my samd21-gpio_high_and_low_durations project using DMA to move the CC0 and CC1 counter values instead of software. I wanted to see if DMA would improve performance (be able to sample faster/shorter pulses). The result is that DMA has about the same overhead as a tight software ISR loop. High pulses as short as 300ns can be sampled as long as the period is 1000ns or greater. This is about identical to the software ISR performance.

DMA looks like it is great at freeing-up the software to do other tasks or to conserve on power, but does not offer really any other performance benefits. I was hoping for DMA to complete in 2-3 clock cycles for a single 32bit data read followed by a 32bit data write (from when the event tiggered). This is definitely not the case... looks like it is 16+ clock cycles.

I wish that the SAMD21 documentation had been more "up front" in describing the time it takes for a single DMA channel to complete. I was expecting a performance gain but measured none.

This project contains a good example of multi-channel DMA on the SAMD21 without using the HAL library (just individual register writes).

PIN PA7 is the input which measures a single pulse's high time and period.
