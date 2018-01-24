// Testbench for Orbtrace

`timescale 1 ns / 1 ps

module testbench();

	initial begin
		$dumpfile("testbench_output.vcd");
		$dumpvars();
		//  it is actually possible to just extract the uart output
		//  and decode it in pulseview, the GUI of sigrok. to do so,
		//  use this:
		//$dumpfile("testbench_uart.vcd");
		//$dumpvars(0, uarttx);
		//  instead of the above (sigrok can't handle ALL signals).
		//  then open the output in pulseview:
		// 	pulseview -I vcd testbench_output.vcd
		//  and add a proper UART decoder at 12 MHz.
	end

	reg  [3:0] traceDin   = 4'bxxxx;
	reg        traceClk   = 1'b0;
	wire       spitx;
//	reg        spirx      = 1'b0; // FIXME direction?!
	wire       spirx;
	reg        spics      = 1'b1;
	reg        spiclk     = 1'b0;
//	reg        uartrx     = 1'b1;
//	wire       uarttx;
	wire       sync_led;
	wire       rxInd_led;
	wire       txInd_led;
	wire       txOvf_led;
	reg        pll_clk48  = 1'b0;
	reg        pll_locked = 1'b0;
	reg        clk12      = 1'b0;
	reg        rst        = 1'b0;
	wire       D6;
	wire       D5;
	wire       D4;
	wire       D3;
	wire       cts;
	wire       yellow;
	wire       green;

	topLevel top (
		.traceDin(traceDin),		// [3:0] in
		.traceClk(traceClk),		//       in
		.spitx(spitx),			//       out
		.spirx(spirx),			//       out?!
		.spics(spics),			//       in
		.spiclk(spiclk),		//       in
//		.uartrx(uartrx),		//       in
//		.uarttx(uarttx),		//       out
		.sync_led(sync_led),		//       out
		.rxInd_led(rxInd_led),		//       out
		.txInd_led(txInd_led),		//       out
		.txOvf_led(txOvf_led),		//       out
		.clkIn(clk12),			//       in
		.inject_pll_clk48(pll_clk48),	//       in
		.inject_pll_lock(pll_locked),	//       in
		.rstIn(rst),			//       in
		.D6(D6),			//       out
		.D5(D5),			//       out
		.D4(D4),			//       out
		.D3(D3),			//       out
		.cts(cts)			//       out
	);

	// generate PLL clock and 12MHz system clock
	initial begin
		pll_locked <= 1;
	end
	always begin
		#10.416;
		pll_clk48 <= ~pll_clk48;
		#10.416;
		pll_clk48 <= ~pll_clk48;

		#10.416;
		pll_clk48 <= ~pll_clk48;
		#10.416;
		pll_clk48 <= ~pll_clk48;

		clk12 <= ~clk12;
	end

	task setTracePortStimulus;
		input [3:0] data;
		begin
			// setup
			traceDin <= data;
			#50;
			// clock
			traceClk <= ~traceClk;
			// hold
			#50;
			traceDin <= 4'bzzzz;
			// remainder
			#400;
		end
	endtask

	task setTrace1;
		// expects to start with clk low.
		parameter dataWidth = 8;
		input [dataWidth-1:0] data;
		integer i;
		begin
			for(i = 0; i < dataWidth; i = i+2) begin
				setTracePortStimulus({ 1'b0, 1'b0, 1'b0, data[i+0] }); // rising edge
				setTracePortStimulus({ 1'b0, 1'b0, 1'b0, data[i+1] }); // falling edge
			end
		end
	endtask

	task setTrace2;
		// expects to start with clk low.
		parameter dataWidth = 8;
		input [dataWidth-1:0] data;
		integer i;
		begin
			for(i = 0; i < dataWidth; i = i+4) begin
				setTracePortStimulus({ 1'b0, 1'b0, data[i+1], data[i+0] }); // rising edge
				setTracePortStimulus({ 1'b0, 1'b0, data[i+3], data[i+2] }); // falling edge
			end
		end
	endtask

	task setTrace4;
		// expects to start with clk low.
		parameter dataWidth = 8;
		input [dataWidth-1:0] data;
		integer i;
		begin
			for(i = 0; i < dataWidth; i = i+8) begin
				setTracePortStimulus({ data[i+3], data[i+2], data[i+1], data[i+0] }); // rising edge
				setTracePortStimulus({ data[i+7], data[i+6], data[i+5], data[i+4] }); // falling edge
			end
		end
	endtask

	task sendHalfFrame;
		begin
			setTrace4(8'haa);
			setTrace4(8'h55);
			setTrace4(8'haa);
			setTrace4(8'h55);
			setTrace4(8'h66);
			setTrace4(8'h99);
			setTrace4(8'h66);
			setTrace4(8'h99);
		end
	endtask

	task sendFrame;
		begin
			// valid 128 bits
			setTrace4(8'h01);
			setTrace4(8'h23);
			setTrace4(8'h45);
			setTrace4(8'h67);
			setTrace4(8'h89);
			setTrace4(8'hab);
			setTrace4(8'hcd);
			setTrace4(8'hef);
			setTrace4(8'h01);
			setTrace4(8'h23);
			setTrace4(8'h45);
			setTrace4(8'h67);
			setTrace4(8'h89);
			setTrace4(8'hab);
			setTrace4(8'hcd);
			setTrace4(8'hef);
		end
	endtask

	task sendSync;
		begin
			setTrace4(8'hff);
			setTrace4(8'hff);
			setTrace4(8'hff);
			setTrace4(8'hff);
			setTrace4(8'hff);
			setTrace4(8'hff);
			setTrace4(8'hff);
			setTrace4(8'h7f);
		end
	endtask

	task sendSyncedFrame;
		begin
			sendSync();
			sendFrame();
		end
	endtask

	task initWithoutReset;
		begin
			#500;

			traceClk <= ~traceClk;
			#500;

			traceClk <= ~traceClk;
			#450;

			traceDin <= 0;
			#50;
			traceClk <= ~traceClk;
			#50;
			traceDin <= 4'bzzzz;
			#450;

			traceClk <= ~traceClk;
			#500;

			traceClk <= ~traceClk;
			#450;
		end
	endtask

	task initWithReset;
		begin
			#500;

			traceClk <= ~traceClk;
			#500;

			traceClk <= ~traceClk;
			#450;

			rst = 1'b1;
			traceDin <= 0;
			#50;
			traceClk <= ~traceClk;
			#50;
			traceDin <= 4'bzzzz;
			#450;

			traceClk <= ~traceClk;
			#50;
			rst = 1'b0;
			#450;

			traceClk <= ~traceClk;
			#450;
		end
	endtask

	initial begin
		initWithoutReset();

		sendHalfFrame();

		sendSyncedFrame();
		sendFrame();

		sendSync();
		// bad stimulus
		setTrace4(8'h01);
		setTrace4(8'h23);

		sendSyncedFrame();
		sendFrame();
		sendFrame();
		sendFrame();


		// now *with* a reset
		#50;
		traceClk = ~traceClk;
		initWithReset();

		sendHalfFrame();

		sendSyncedFrame();
		sendFrame();

		sendSync();
		// bad stimulus
		setTrace4(8'h01);
		setTrace4(8'h23);

		sendSyncedFrame();
		sendFrame();
		sendFrame();
		sendFrame();

		#20000;
		$display("simulation completed");
		$finish;
	end

endmodule

