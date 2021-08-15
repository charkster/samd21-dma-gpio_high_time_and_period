
#define PIN 7               // D2 is PA7, which is odd

uint32_t high_count;
uint32_t period_count;
uint32_t high_ns;   // high time in nano seconds
uint32_t period_ns; // period in nano seconds

typedef struct // DMAC descriptor structure
{
  uint16_t btctrl;
  uint16_t btcnt;
  uint32_t srcaddr;
  uint32_t dstaddr;
  uint32_t descaddr;
} dmacdescriptor ;

volatile dmacdescriptor wrb[DMAC_CH_NUM] __attribute__ ((aligned (16)));      // Write-back DMAC descriptors
dmacdescriptor descriptor_section[DMAC_CH_NUM] __attribute__ ((aligned (16)));// DMAC channel descriptors
dmacdescriptor descriptor __attribute__ ((aligned (16)));                     // Place holder descriptor

void setup()
{
  Serial.begin(115200);                          // Send data back on the native (USB) port
  while (!Serial);                               // Wait for the Serial port to be ready

  REG_PM_APBBMASK |= PM_APBBMASK_DMAC;           // Switch on the DMAC system peripheral

  REG_PM_APBCMASK |= PM_APBCMASK_EVSYS |         // Switch on the event system peripheral
                     PM_APBCMASK_TC4   |         // Switch on the TC4 peripheral
                     PM_APBCMASK_TC5;            // Switch on the TC5 peripheral

  // use gclk1 for TC & EIC & EVSYS
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(0) |         // Divide the 48MHz system clock by 1 = 48MHz
                    GCLK_GENDIV_ID(1);           // Set division on Generic Clock Generator (GCLK) 1
  while (GCLK->STATUS.bit.SYNCBUSY);             // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |          // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |        // Enable GCLK
                     GCLK_GENCTRL_SRC_DFLL48M |  // Set the clock source to 48MHz
                     GCLK_GENCTRL_ID(1);         // Set clock source on GCLK 1
  while (GCLK->STATUS.bit.SYNCBUSY);             // Wait for synchronization

  // second write to CLKCTRL, different ID
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN      |   // Enable the generic clock
                     GCLK_CLKCTRL_GEN_GCLK1  |   // on GCLK1
                     GCLK_CLKCTRL_ID_TC4_TC5;    // Feed the GCLK1 also to TC4
  while (GCLK->STATUS.bit.SYNCBUSY);             // Wait for synchronization

  DMAC->BASEADDR.reg = (uint32_t)descriptor_section;               // Set the descriptor section base address
  DMAC->WRBADDR.reg  = (uint32_t)wrb;                              // Set the write-back descriptor base adddress
  DMAC->CTRL.reg     = DMAC_CTRL_DMAENABLE | DMAC_CTRL_LVLEN(0xF); // Enable the DMAC and priority levels

  // Set DMAC channel 0 to trigger on TC4 match compare 1, this is the only way to get the first high time
  DMAC->CHID.reg    = 0; // select channel 0
  DMAC->CHCTRLB.reg = DMAC_CHCTRLB_TRIGSRC(TC4_DMAC_ID_MC_1);
  
  descriptor.descaddr = (uint32_t)&descriptor_section[0];              // Set up a circular descriptor
  descriptor.srcaddr  = (uint32_t)&REG_TC4_COUNT32_CC0;                // Take the contents of the TC4 counter comapare 0 register
  descriptor.dstaddr  = (uint32_t)&high_count;                         // Copy it to the "dmac_val" array
  descriptor.btcnt    = 1;                                             // This takes 1 beat for CC0 Dword
  descriptor.btctrl   = DMAC_BTCTRL_BEATSIZE_WORD | DMAC_BTCTRL_VALID; // Copy 32-bits (WORD) and flag discriptor as valid
  memcpy(&descriptor_section[0], &descriptor, sizeof(dmacdescriptor)); // Copy to the channel 0 descriptor

  // Set DMAC channel 1 to trigger on TC4 match compare 1
  DMAC->CHID.reg    = 1; // select channel 1
  DMAC->CHCTRLB.reg = DMAC_CHCTRLB_TRIGSRC(TC4_DMAC_ID_MC_1);

  descriptor.descaddr = (uint32_t)&descriptor_section[1];              // Set up a circular descriptor
  descriptor.srcaddr  = (uint32_t)&REG_TC4_COUNT32_CC1;                // Take the contents of the TC4 counter comapare 0 register
  descriptor.dstaddr  = (uint32_t)&period_count;                       // Copy it to the "dmac_val" array
  descriptor.btcnt    = 1;                                             // This takes 1 beat for CC1 Dwords
  descriptor.btctrl   = DMAC_BTCTRL_BEATSIZE_WORD | DMAC_BTCTRL_VALID; // Copy 32-bits (WORD) and flag discriptor as valid
  memcpy(&descriptor_section[1], &descriptor, sizeof(dmacdescriptor)); // Copy to the channel 1 descriptor

  // Enable the port multiplexer on pin number "PIN"
  PORT->Group[g_APinDescription[PIN].ulPort].PINCFG[g_APinDescription[PIN].ulPin].bit.PULLEN = 1; // out is default low so pull-down
  PORT->Group[g_APinDescription[PIN].ulPort].PINCFG[g_APinDescription[PIN].ulPin].bit.INEN   = 1;
  PORT->Group[g_APinDescription[PIN].ulPort].PINCFG[g_APinDescription[PIN].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[PIN].ulPort].PMUX[g_APinDescription[PIN].ulPin >> 1].reg |= PORT_PMUX_PMUXO_A; // 0 is Peripheral "A" EIC/EXTINT, PA07 is odd

  EIC->EVCTRL.reg     = EIC_EVCTRL_EXTINTEO7;      // Enable event output on external interr
  EIC->CONFIG[0].reg  = EIC_CONFIG_SENSE7_HIGH;    // Set event detecting a high (config 0, #7 is 7)
  EIC->INTENCLR.reg   = EIC_INTENCLR_EXTINT7;      // Clear the interrupt flag on channel 7
  EIC->CTRL.reg       = EIC_CTRL_ENABLE;           // Enable EIC peripheral
  while (EIC->STATUS.bit.SYNCBUSY);                // Wait for synchronization

  REG_EVSYS_CHANNEL = EVSYS_CHANNEL_EDGSEL_NO_EVT_OUTPUT |              // No event edge detection, we already have it on the EIC
                      EVSYS_CHANNEL_PATH_ASYNCHRONOUS    |              // Set event path as asynchronous
                      EVSYS_CHANNEL_EVGEN(EVSYS_ID_GEN_EIC_EXTINT_7) |  // Set event generator (sender) as external interrupt 7
                      EVSYS_CHANNEL_CHANNEL(0);                         // Attach the generator (sender) to channel 0

  REG_EVSYS_USER = EVSYS_USER_CHANNEL(1) |                              // Attach the event user (receiver) to channel 0 (n + 1)
                   EVSYS_USER_USER(EVSYS_ID_USER_TC4_EVU);              // Set the event user (receiver) as timer TC4

  TC4->COUNT32.EVCTRL.reg = TC_EVCTRL_TCEI    |             // Enable the TCC event input
                            TC_EVCTRL_EVACT_PWP;            // Set up the timer for capture: CC0 pulsewidth, CC1 period
  
  TC4->COUNT32.CTRLC.reg |= TC_CTRLC_CPTEN1 |               // Enable capture on CC1
                            TC_CTRLC_CPTEN0;                // Enable capture on CC0

  TC4->COUNT32.CTRLA.reg = TC_CTRLA_PRESCALER_DIV1 |        // Set prescaler to 1
                           TC_CTRLA_MODE_COUNT32   |        // Set the TC4 timer to 32-bit mode in conjuction with timer TC5
                           TC_CTRLA_ENABLE;                 // Enable TC4
  while (TC4->COUNT32.STATUS.bit.SYNCBUSY);                 // Wait for synchronization
  

  DMAC->CHID.reg     = 0;
  DMAC->CHCTRLA.reg |= DMAC_CHCTRLA_ENABLE;      // Enable DMAC channel 0
  DMAC->CHID.reg     = 1;
  DMAC->CHCTRLA.reg |= DMAC_CHCTRLA_ENABLE;      // Enable DMAC channel 1
}

void loop()
{
  if (high_count > 0) {
     high_ns = high_count * (1000.0 / 48.0) + 21; // 48MHz plus add 1 clock period for accuracy
  } 
  else {
    high_ns = 0;
  }
  Serial.print(high_ns);
  // Output the results: pulsewidth   period
  Serial.print(F("   "));
  if (period_count > 0) {
    period_ns = period_count * (1000.0 /48.0) + 21; // 48MHz plus add 1 clock period for accuracy
  }
  else {
    period_ns = 0;
  }
  Serial.println(period_ns);
  delay(1000);
}
