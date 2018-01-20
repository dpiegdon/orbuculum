
# when set to zero arachne_pnr placement will be different each time
# (can be used for timing analysis e.g.)
REPRODUCIBLE_BUILD ?= 1
ifeq ($(REPRODUCIBLE_BUILD),1)
	RANDOMIZE_SEED=
else
	RANDOMIZE_SEED= -r
endif


SUPPORTED_HARDWARE=ICE40HX8K_B_EVN ICE40HX1K_STICK_EVN

TRACEIF_SOURCES=toplevel.v uart.v traceIF.v packSend.v flagSync.v

.PHONY: all clean $(SUPPORTED_HARDWARE) simulate

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
	-rm -f *.vvp
	-rm -f *.vcd

$(SUPPORTED_HARDWARE):%:%.bin
	iceprog -S $<

# simulation stage:

testbench_%.vvp:
	iverilog ${IVERILOG_FLAGS} -D SIMULATION -o $@ $^

testbench_toplevel.vvp: IVERILOG_FLAGS=-D NO_GB_IO_AVAILABLE
testbench_toplevel.vvp: testbench_toplevel.v $(TRACEIF_SOURCES) $(shell yosys-config --datdir/ice40/cells_sim.v)

testbench_uart.vvp: testbench_uart.v uart.v

%.vcd: %.vvp
	@echo ""
	vvp $^
	@echo ""

simulate: testbench_toplevel.vcd testbench_uart.vcd

# bitmap conversion stage:

%.bin: %.asc
	icepack $< $@

# place and route stage:

%.asc:
	arachne-pnr $(RANDOMIZE_SEED) -m 800 -d $(subst hx,,$(subst lp,,$(DEVICE))) -P $(PACKAGE) -p $(subst .asc,.pcf,$@) $< -o $@
	icetime -d $(DEVICE) $@

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


