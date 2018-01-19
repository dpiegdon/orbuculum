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

	reg  [3:0] traceDin   = 4'b0000;
	reg        traceClk   = 1'b0;
	reg        uartrx     = 1'b1;
	wire       uarttx;
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
		.uartrx(uartrx),		//       in
		.uarttx(uarttx),		//       out
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
			traceDin <= data;
			#50;			// setup
			traceClk <= ~traceClk;
			#50;			// hold
			#900;			// remainder of clock cycle
		end
	endtask

	task setTrace1;
		// expects to start with clk low.
		parameter dataWidth = 16;
		input [dataWidth-1:0] data;
		integer i;
		begin
			for(i = 0; i < dataWidth; i = i+2) begin
				setTracePortStimulus({ 1'b0, 1'b0, 1'b0, data[i]   }); // rising edge
				setTracePortStimulus({ 1'b0, 1'b0, 1'b0, data[i+1] }); // falling edge
			end
		end
	endtask

	// task setTrace2; // TODO

	// task setTrace4; // TODO

	initial begin
		traceDin <= 4'b0000;
		traceClk <= 1'b0;
		#1000;

		rst = 1'b1;
		#50;
		traceClk <= 1'b1;
		#1000;

		traceClk <= 1'b0;
		#50;
		rst = 1'b0;
		#50;

		#2000;

		setTrace1(16'haa55);
		setTrace1(16'haa55);
		setTrace1(16'h0123);
		setTrace1(16'h0123);

		setTrace1(16'h7fff);
		setTrace1(16'hffff);
		setTrace1(16'hffff);
		setTrace1(16'hffff);

		setTrace1(16'h0123);
		setTrace1(16'h4567);
		setTrace1(16'h89ab);
		setTrace1(16'hcdef);
		setTrace1(16'h0123);
		setTrace1(16'h4567);
		setTrace1(16'h89ab);
		setTrace1(16'hcdef);

		setTrace1(16'h7fff);
		setTrace1(16'hffff);
		setTrace1(16'hffff);
		setTrace1(16'hffff);

		setTrace1(16'h0123);

		setTrace1(16'h7fff);
		setTrace1(16'hffff);
		setTrace1(16'hffff);
		setTrace1(16'hffff);

		#10000;
		$display("simulation completed");
		$finish;
	end

endmodule

