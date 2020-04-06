

.phony: zsim clean sync-nvoverlay release

zsim: 
	@echo "Detecting whether pin directory exists..."
	@if [ -d "./pin-2.14" ]; then echo "Found pin-2.14"; else echo "You need pin-2.14 directory"; fi
	PINPATH=$(CURDIR)/pin-2.14 scons
	@echo "If compilation fails and says hdf5 cannot be found, run make clean and retry"

release:
	@echo "Detecting whether pin directory exists..."
	@if [ -d "./pin-2.14" ]; then echo "Found pin-2.14"; else echo "You need pin-2.14 directory"; fi
	PINPATH=$(CURDIR)/pin-2.14 scons --r
	@echo "If compilation fails and says hdf5 cannot be found, run make clean and retry"


sync-nvoverlay:
	cp ~/data_disk/NVOverlay/src/NVOverlaySim/util.h ./src/nvoverlay
	cp ~/data_disk/NVOverlay/src/NVOverlaySim/util.cpp ./src/nvoverlay
	cp ~/data_disk/NVOverlay/src/NVOverlaySim/nvoverlay.h ./src/nvoverlay
	cp ~/data_disk/NVOverlay/src/NVOverlaySim/nvoverlay.cpp ./src/nvoverlay

clean:
	@rm ./.sconsign.dblite
	@rm -rf ./build
