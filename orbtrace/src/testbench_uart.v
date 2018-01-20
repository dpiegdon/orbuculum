// Testbench for UART block

`timescale 1 ns / 1 ps

module testbench();
	initial begin
		$dumpfile("testbench_output.vcd");
		$dumpvars();
	end

	// simulation lifespan
	initial begin
		#20000000
		$finish;
	end

	reg		clk		= 1'b0;
	reg		rst		= 1'b0;
	reg		rx		= 1'b1;
	wire		tx;
	reg		transmit	= 1'b0;
	reg	[7:0]	tx_byte		= 8'b0;
	wire		received;
	wire		tx_free;
	wire	[7:0]	rx_byte;
	wire		is_receiving;
	wire		is_transmitting;
	wire		recv_error;

	reg		dut_transceive_error = 1'b0;
	reg		dut_final_result_success = 1'b0;
	reg		dut_final_result_valid = 1'b0;


	// device under test
	uart #(.CLOCKFRQ(1_000_000), .BAUDRATE(250_000)) dut (
		.clk(clk),
		.rst(rst),
		.rx(rx),
		.tx(tx),
		.transmit(transmit),
		.tx_byte(tx_byte),
		.received(received),
		.tx_free(tx_free),
		.rx_byte(rx_byte),
		.is_receiving(is_receiving),
		.is_transmitting(is_transmitting),
		.recv_error(recv_error)
	);

	// system clock
	always begin
		clk = ~clk;
		#500;
	end

	// transmitter
	integer i;
	initial begin
		#10000;
		for(i = 0; i < 256; i = i+1) begin
			while(~tx_free) begin
				#5000; // #1000
			end
			tx_byte <= i[7:0];
			transmit <= 1;
			#500;
			transmit <= 0;
			#1000;
		end
		#1000;
		i = 0;
	end

	// delay for uart signal
	always @(posedge clk) begin
		rx <= #10000000 tx;
	end

	// receiver
	always @(posedge received) begin
		if( i != rx_byte ) begin
			dut_transceive_error <= 1'b1;
		end
		i = i+1;
		if( i > 255 ) begin
			dut_final_result_success <= ~dut_transceive_error;
			dut_final_result_valid <= 1'b1;
		end
	end

endmodule

