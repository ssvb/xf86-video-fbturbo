AM_CFLAGS = @XORG_CFLAGS@
AM_LDFLAGS = -lpixman-1
SUNXI_DISP = ../src/sunxi_disp.c ../src/sunxi_disp.h ../src/sunxi_disp_ioctl.h

###############################################################################

DEMOS =				\
	sunxi_disp_vsync_demo

sunxi_disp_vsync_demo_SOURCES = sunxi_disp_vsync_demo.c $(SUNXI_DISP)

###############################################################################

BENCHMARKS =			\
	sunxi_g2d_bench

sunxi_g2d_bench_SOURCES = sunxi_g2d_bench.c $(SUNXI_DISP)

###############################################################################

noinst_PROGRAMS = $(DEMOS) $(BENCHMARKS)
