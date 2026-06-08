CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

all: gtultra2raster

gtultra2raster: gtultra2raster.cpp gtultra_sng.h raster_assets.h
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	$(RM) gtultra2raster

.PHONY: all clean
