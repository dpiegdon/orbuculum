// Testbench for Orbtrace

`timescale 1 ns / 1 ps

module testbench();

	initial begin
		$dumpfile("testbench_output.vcd");
		$dumpvars();
	end

	reg  [3:0] traceDin  = 4'b0000;
	reg        traceClk  = 1'b0;
	reg        uartrx    = 1'b0;
	wire       uarttx;
	wire       sync_led;
	wire       rxInd_led;
	wire       txInd_led;
	wire       txOvf_led;
	reg        pll_clk48 = 1'b0;
	reg        pll_locked = 1'b0;
	reg        clk12     = 1'b0;
	reg        rst       = 1'b1;
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
		.rst(rst),			//       in
		.D6(D6),			//       out
		.D5(D5),			//       out
		.D4(D4),			//       out
		.D3(D3),			//       out
		.cts(cts),			//       out
		.yellow(yellow),		//       out
		.green(green)			//       out
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

	task setTrace;
		input [3:0] data;
		begin
			traceDin <= data;
			#50;			// setup
			traceClk <= ~traceClk;
			#50;			// hold
			#900;			// remainder of clock cycle
		end
	endtask

	initial begin
		traceClk <= 1'b0;
		traceDin <= 4'b0000;
		#1000;

		rst = 1'b0;
		#1000;

		setTrace(4'b0000);
		setTrace(4'b0001);
		setTrace(4'b0010);

		// sync
		setTrace(4'b0111);	// 7
		setTrace(4'b1111);	// f
		setTrace(4'b1111);	// f
		setTrace(4'b1111);	// f

		// frame 1
		setTrace(4'b0000);	// byte 0
		setTrace(4'b0001);	// byte 1
		setTrace(4'b0010);	// byte 2
		setTrace(4'b0011);	// byte 3
		setTrace(4'b0100);	// byte 4
		setTrace(4'b0101);	// byte 5
		setTrace(4'b0110);	// byte 6
		setTrace(4'b0111);	// byte 7
		setTrace(4'b1000);	// byte 8
		setTrace(4'b1001);	// byte 9
		setTrace(4'b1010);	// byte 10
		setTrace(4'b1011);	// byte 11
		setTrace(4'b1100);	// byte 12
		setTrace(4'b1101);	// byte 13
		setTrace(4'b1110);	// byte 14
		setTrace(4'b1111);	// byte 15

		// frame 2
		setTrace(4'b0000);	// byte 0
		setTrace(4'b0001);	// byte 1
		setTrace(4'b0010);	// byte 2
		setTrace(4'b0011);	// byte 3
		setTrace(4'b0100);	// byte 4
		setTrace(4'b0101);	// byte 5
		setTrace(4'b0110);	// byte 6
		setTrace(4'b0111);	// byte 7
		setTrace(4'b1000);	// byte 8
		setTrace(4'b1001);	// byte 9
		setTrace(4'b1010);	// byte 10
		setTrace(4'b1011);	// byte 11
		setTrace(4'b1100);	// byte 12
		setTrace(4'b1101);	// byte 13
		setTrace(4'b1110);	// byte 14
		setTrace(4'b1111);	// byte 15

		// sync
		setTrace(4'b0111);	// 7
		setTrace(4'b1111);	// f
		setTrace(4'b1111);	// f
		setTrace(4'b1111);	// f

		// frame 3
		setTrace(4'b0000);	// byte 0
		setTrace(4'b0001);	// byte 1
		setTrace(4'b0010);	// byte 2
		setTrace(4'b0011);	// byte 3
		setTrace(4'b0100);	// byte 4
		setTrace(4'b0101);	// byte 5
		setTrace(4'b0110);	// byte 6
		setTrace(4'b0111);	// byte 7
		setTrace(4'b1000);	// byte 8
		setTrace(4'b1001);	// byte 9
		setTrace(4'b1010);	// byte 10
		setTrace(4'b1011);	// byte 11
		setTrace(4'b1100);	// byte 12
		setTrace(4'b1101);	// byte 13
		setTrace(4'b1110);	// byte 14
		setTrace(4'b1111);	// byte 15

		// frame 4 (broken)
		setTrace(4'b0000);	// byte 0
		setTrace(4'b0001);	// byte 1
		setTrace(4'b0010);	// byte 2
		setTrace(4'b0011);	// byte 3
		setTrace(4'b0100);	// byte 4
		setTrace(4'b0101);	// byte 5

		// sync
		setTrace(4'b0111);	// 7
		setTrace(4'b1111);	// f
		setTrace(4'b1111);	// f
		setTrace(4'b1111);	// f

		// frame 5 (inverted)
		setTrace(4'b1111);	// byte 1
		setTrace(4'b1110);	// byte 0
		setTrace(4'b1101);	// byte 2
		setTrace(4'b1100);	// byte 3
		setTrace(4'b1011);	// byte 4
		setTrace(4'b1010);	// byte 5
		setTrace(4'b1001);	// byte 6
		setTrace(4'b1000);	// byte 7
		setTrace(4'b0111);	// byte 8
		setTrace(4'b0110);	// byte 9
		setTrace(4'b0101);	// byte 01
		setTrace(4'b0100);	// byte 00
		setTrace(4'b0011);	// byte 02
		setTrace(4'b0010);	// byte 03
		setTrace(4'b0001);	// byte 04
		setTrace(4'b0000);	// byte 05

		// frame 6
		setTrace(4'b0000);	// byte 0
		setTrace(4'b0001);	// byte 1
		setTrace(4'b0010);	// byte 2
		setTrace(4'b0011);	// byte 3
		setTrace(4'b0100);	// byte 4
		setTrace(4'b0101);	// byte 5
		setTrace(4'b0110);	// byte 6
		setTrace(4'b0111);	// byte 7
		setTrace(4'b1000);	// byte 8
		setTrace(4'b1001);	// byte 9
		setTrace(4'b1010);	// byte 10
		setTrace(4'b1011);	// byte 11
		setTrace(4'b1100);	// byte 12
		setTrace(4'b1101);	// byte 13
		setTrace(4'b1110);	// byte 14
		setTrace(4'b1111);	// byte 15

		#10000;
		$display("simulation completed");
		$finish;
	end

endmodule

