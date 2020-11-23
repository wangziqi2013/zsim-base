

.phony: zsim clean

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


clean:
	@rm ./.sconsign.dblite
	@rm -rf ./build
