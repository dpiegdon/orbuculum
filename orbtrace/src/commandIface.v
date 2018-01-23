`default_nettype none

module commandIface(
	// command interface
	input			clk,
	input			rst,

	input			latchCommand,
	input [7:0]		command,

//	output reg		latchResult	= 1'b0,
//	output reg [7:0]	result		= 8'b0,

	// things that we command
	output reg [2:0]	traceWidth	= 3'h4
);

parameter CMD_TRACEWIDTH_1 = 8'h31;	// character '1'
parameter CMD_TRACEWIDTH_2 = 8'h32;	// character '2'
parameter CMD_TRACEWIDTH_4 = 8'h34;	// character '4'

always @(posedge clk) begin
	if( rst ) begin
//		latchResult <= 0;
//		result <= 0;
		traceWidth <= 3'h4;
	end else begin
		if( latchCommand ) begin
			case(command)
				CMD_TRACEWIDTH_1: begin
					traceWidth <= 1;
				end
				CMD_TRACEWIDTH_2: begin
					traceWidth <= 2;
				end
				CMD_TRACEWIDTH_4: begin
					traceWidth <= 4;
				end
			endcase
		end
	end
	
end

endmodule
