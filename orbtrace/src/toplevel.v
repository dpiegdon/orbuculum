`default_nettype none

module topLevel(
		input [3:0] traceDin, // Port is always 4 bits wide, even if we use less
		input 	    traceClk, // Supporting clock for input - must be on a global clock pin

		output 	    spitx,
		output 	    spirx,   // FIXME: should this not be an input?
		input 	    spics,
		input 	    spiclk,
		
//		input 	    uartrx, // Receive data into UART
//		output 	    uarttx, // Transmit data from UART 

		// Leds....
		output 	    sync_led,
		output 	    rxInd_led, // Received UART Data indication
		output 	    txInd_led, // Transmitted UART Data indication
		output 	    txOvf_led,
		
		// Config and housekeeping
		input 	    clkIn,
		`ifdef SIMULATION
		// simulation of PLL is not possible,
		// so we have to inject the clock here.
		// FIXME: split orbtrace code and hardware interfacing more
		// cleanly, so simulation layer can just replace hardware.
		input 	    inject_pll_clk48,
		input 	    inject_pll_lock,
		`endif // SIMULATION
		input 	    rstIn,

		// Other indicators
		output reg  D6,
		output reg  D5,
		output reg  D4,
		output reg  D3,
		output reg  cts
		);      
	    
   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for...not more than 4!! 
//   parameter WIDTH_SET=2;
   //    
   // Internals =============================================================================


   wire 		   lock; // Indicator that PLL has locked
   wire 		   rst;
   wire 		   clk;
   wire 		   clkOut;
   wire 		   BtraceClk;
  
`ifdef NO_GB_IO_AVAILABLE
// standard input pin for trace clock,
// then route it into an internal global buffer.
SB_GB BtraceClk0 (
 .USER_SIGNAL_TO_GLOBAL_BUFFER(traceClk),
 .GLOBAL_BUFFER_OUTPUT(BtraceClk)
 );
`else
// Buffer for trace input clock
SB_GB_IO #(.PIN_TYPE(6'b000000)) BtraceClk0
(
  .PACKAGE_PIN(traceClk),
  .GLOBAL_BUFFER_OUTPUT(BtraceClk)
);
`endif

// Trace input pins config   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn0
(
 .PACKAGE_PIN (traceDin[0]),
 .LATCH_INPUT_VALUE (1'b1),
 .CLOCK_ENABLE (1'b1),
 .INPUT_CLK (BtraceClk),
 .OUTPUT_CLK (1'b0),
 .OUTPUT_ENABLE (1'b0),
 .D_OUT_0 (1'bz),
 .D_OUT_1 (1'bz),
 .D_IN_0 (tTraceDina[0]),
 .D_IN_1 (tTraceDinb[0])
 );
   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn1
(
 .PACKAGE_PIN (traceDin[1]),
 .LATCH_INPUT_VALUE (1'b1),
 .CLOCK_ENABLE (1'b1),
 .INPUT_CLK (BtraceClk),
 .OUTPUT_CLK (1'b0),
 .OUTPUT_ENABLE (1'b0),
 .D_OUT_0 (1'bz),
 .D_OUT_1 (1'bz),
 .D_IN_0 (tTraceDina[1]),
 .D_IN_1 (tTraceDinb[1])
  );
   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn2
(
 .PACKAGE_PIN (traceDin[2]),
 .LATCH_INPUT_VALUE (1'b1),
 .CLOCK_ENABLE (1'b1),
 .INPUT_CLK (BtraceClk),
 .OUTPUT_CLK (1'b0),
 .OUTPUT_ENABLE (1'b0),
 .D_OUT_0 (1'bz),
 .D_OUT_1 (1'bz),
 .D_IN_0 (tTraceDina[2]),
 .D_IN_1 (tTraceDinb[2])
 );
   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn3 
(
 .PACKAGE_PIN (traceDin[3]),
 .LATCH_INPUT_VALUE (1'b1),
 .CLOCK_ENABLE (1'b1),
 .INPUT_CLK (BtraceClk),
 .OUTPUT_CLK (1'b0),
 .OUTPUT_ENABLE (1'b0),
 .D_OUT_0 (1'bz),
 .D_OUT_1 (1'bz),
 .D_IN_0 (tTraceDina[3]),
 .D_IN_1 (tTraceDinb[3])
 );

SB_IO #(.PULLUP(1), .PIN_TYPE(6'b000001)) SpiClkIn
(
 .PACKAGE_PIN (spiclk),
 .D_IN_0 (spiclkIn)
 );

SB_IO #(.PULLUP(1), .PIN_TYPE(6'b000001)) SpiRxIn
(
 .PACKAGE_PIN (spirx),
 .INPUT_CLK (spiclkIn),
 .D_IN_0 (spirxIn)
 );

   // DDR input data
   wire [MAX_BUS_WIDTH-1:0] tTraceDina;
   wire [MAX_BUS_WIDTH-1:0] tTraceDinb;

   wire 		    spiclkIn;
   wire 		    spirxIn;
   
 		    
   wire 		    wclk;
   wire 		    wdavail;
   wire [15:0] 		    packetwd;
   wire 		    packetr;
   
  // -----------------------------------------------------------------------------------------
  traceIF #(.BUSWIDTH(MAX_BUS_WIDTH)) traceif (
                   .clk(clkOut), 
                   .rst(rst), 

		   // Downwards interface to trace pins
                   .traceDina(tTraceDina),       // Tracedata rising edge ... 1-n bits
                   .traceDinb(tTraceDinb),       // Tracedata falling edge (LSB) ... 1-n bits		   
                   .traceClkin(BtraceClk),       // Tracedata clock
		   .width(widthSet),             // Current trace buffer width 

		   // Upwards interface to packet processor
		   .WdAvail(wdavail),            // Flag indicating word is available
		   .PacketWd(packetwd),          // The next packet word
		   .PacketReset(packetr),        // Flag indicating to start again

   		   .sync(sync_led)               // Indicator that we are in sync
		);		  
   
  // -----------------------------------------------------------------------------------------

   wire [15:0] 		    filter_data;

   wire 		    dataAvail;
   wire 		    dataReady;
   
   wire 		    txFree;
   
   wire [7:0] 		    rx_byte_tl;
   wire 		    rxTrig_tl;
   wire 		    rxErr_tl;
   wire 		    frameReset;
   wire [2:0] 		    widthSet;
   
   packSend marshall (
		      .clk(clkOut), 
		      .rst(rst), 

		      .sync(sync_led), // Indicator of if we are in sync

		      // Downwards interface to target interface
		      .wrClk(BtraceClk),             // Clock for write side operations to fifo
		      .WdAvail(wdavail),             // Flag indicating word is available
		      .PacketReset(packetr),         // Flag indicating to start again
		      .PacketWd(packetwd),           // The next packet word
		      
		      // Upwards interface to serial (or other) handler
		      .rdClk(spiclkIn),
                      .FrameReady(dataReady),
		      .DataVal(filter_data),         // Output data value
		      .DataNext(txFree),             // Request for data
		      .DataFrameReset(frameReset),   // Reset to start of output frame
                      .DataOverf(txOvf_led)          // Too much data in buffer
 		      );

 spi transmitter (
		  .clk(clkOut), // The master clock for this module
		  .rst(rst), // Synchronous reset.

		  .tx(spitx), // Outgoing serial line
		  .rx(spirxIn), // Incoming serial line
		  .dClk(spiclkIn),
		  .cs(spics),
		  .transmitIn(dataReady), // Signal to transmit
		  .tx_word(filter_data), // Byte to transmit
		  .tx_free(txFree), // Indicator that transmit register is available
		  .is_transmitting(txInd_led), // Low when transmit line is idle.
		  .sync(sync_led),
		  .widthEnc(widthSet),
		  .rxFrameReset(frameReset)
		  );
   
   // -----------------------------------------------------------------------------------------   
//   uart #(.CLOCKFRQ(48_000_000), .BAUDRATE(12_000_000))  receiver (
//	.clk(clkOut),                 // System Clock
///	.rst(rst),                 // System Reset
//	.rx(uartrx),               // Uart TX pin
//	.tx(uarttx),               // Uart RX pin
//					  
//	.transmit(dataReady),
  //      .tx_byte(filter_data),
    //    .tx_free(txFree),
//					  
//	.received(rxTrig_tl),
//	.rx_byte(rx_byte_tl),
//					 
//	.recv_error(rxErr_tl),
//
//	.is_receiving(rxInd_led),
//	.is_transmitting(txInd_led)
//  );
//
  // -----------------------------------------------------------------------------------------   
 // Set up clock for 48Mhz with input of 12MHz
   `ifndef SIMULATION
   SB_PLL40_CORE #(
		   .FEEDBACK_PATH("SIMPLE"),
		   .PLLOUT_SELECT("GENCLK"),
		   .DIVR(4'b0000),
		   .DIVF(7'b0111111),
		   .DIVQ(3'b100),
		   .FILTER_RANGE(3'b001)
		   ) uut (
			  .LOCK(lock),
			  .RESETB(1'b1),
			  .BYPASS(1'b0),
			  .REFERENCECLK(clkIn),
			  .PLLOUTCORE(clkOut)
			  );
   `else // ifndef SIMULATION
   assign lock = inject_pll_lock;
   assign clkOut = inject_pll_clk48;
   `endif // ifndef SIMULATION

   reg [25:0] 		   clkCount;

   // We don't want anything awake until the clocks are stable
   assign rst=(lock&rstIn);
   
   always @(posedge clkOut)
     begin
	if (rst)
	  begin
	     D6<=1'b0;
	     D5<=1'b0;
	     D4<=1'b0;
	     D3<=1'b0;
	     cts<=1'b0;
	     clkCount <= 0;
	  end
	else
	  begin	  
	     clkCount <= clkCount + 1;
	     D6<=clkCount[25];
	  end // else: !if(rst)
     end
endmodule // topLevel
