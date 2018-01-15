
SUPPORTED_HARDWARE=ICE40HX8K_B_EVN ICE40HX1K_STICK_EVN

TRACEIF_SOURCES=toplevel.v uart.v traceIF.v packSend.v flagSync.v
TESTBENCH_SOURCES=testbench.v

.PHONY: all clean $(SUPPORTED_HARDWARE) simulate simulate_and_watch

# supporting rules:

help:
	@echo ""
	@echo "As target please give either:"
	@echo ""
	@echo "    all"
	@echo ""
	@echo "to build all supported platforms,"
	@echo "or one of:"
	@echo ""
	@echo "    $(SUPPORTED_HARDWARE)"
	@echo ""
	@echo "to build and flash this specific platform."
	@echo ""

all:	$(patsubst %,%.bin,$(SUPPORTED_HARDWARE))

clean:
	-rm -f *.blif
	-rm -f *.asc
	-rm -f *.bin
	-rm -f *.vcd
	-rm -f testbench

$(SUPPORTED_HARDWARE):%:%.bin
	iceprog -S $<

# simulation stage:

testbench: $(TESTBENCH_SOURCES) $(TRACEIF_SOURCES)
	iverilog -D NO_GB_IO_AVAILABLE -D SIMULATION -o $@ `yosys-config --datdir/ice40/cells_sim.v` $^

simulate: testbench
	./testbench

simulate_and_watch: simulate
	# reload waveform with CTRL + SHIFT + R
	gtkwave testbench_output.vcd

# bitmap conversion stage:

%.bin: %.asc
	icepack $< $@

# place and route stage:

%.asc:
	arachne-pnr -m 800 -d $(subst hx,,$(subst lp,,$(DEVICE))) -P $(PACKAGE) -p $(subst .asc,.pcf,$@) $< -o $@

ICE40HX1K_STICK_EVN.asc: DEVICE := hx1k
ICE40HX1K_STICK_EVN.asc: PACKAGE := tq144
ICE40HX1K_STICK_EVN.asc: traceIF_GB.blif

ICE40HX8K_B_EVN.asc: DEVICE := hx8k
ICE40HX8K_B_EVN.asc: PACKAGE := ct256
ICE40HX8K_B_EVN.asc: traceIF_GBIO.blif

# verilog synthesis stage:

%.blif:
	yosys -f "verilog $(BUFFER_FLAGS)" -p "synth_ice40 -blif $@" $^

traceIF_GBIO.blif: BUFFER_FLAGS=
traceIF_GBIO.blif: $(TRACEIF_SOURCES)

traceIF_GB.blif: BUFFER_FLAGS=-DNO_GB_IO_AVAILABLE
traceIF_GB.blif: $(TRACEIF_SOURCES)

