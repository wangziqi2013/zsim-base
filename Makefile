

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

deepsjeng_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/deepsjeng_r/deepsjeng_r.cfg

# Spec 2006

perlbench_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/perlbench_2006/perlbench_2006.cfg

libquantum_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/libquantum_2006/libquantum_2006.cfg

bzip2_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/bzip2_2006/bzip2_2006.cfg

gcc_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/gcc_2006/gcc_2006.cfg

bwaves_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/bwaves_2006/bwaves_2006.cfg

gamess_2006:
	-./tests/2DOC/SPEC2006/gamess_2006/run.sh

mcf_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/mcf_2006/mcf_2006.cfg

milc_2006:
	-./tests/2DOC/SPEC2006/milc_2006/run.sh

zeusmp_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/zeusmp_2006/zeusmp_2006.cfg

gromacs_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/gromacs_2006/gromacs_2006.cfg

cactusADM_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/cactusADM_2006/cactusADM_2006.cfg

leslie3d_2006:
	-./tests/2DOC/SPEC2006/leslie3d_2006/run.sh

namd_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/namd_2006/namd_2006.cfg

gobmk_2006:
	-./tests/2DOC/SPEC2006/gobmk_2006/run.sh

dealII_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/dealII_2006/dealII_2006.cfg

soplex_2006:
	-./build/release/zsim ./tests/2DOC/SPEC2006/soplex_2006/soplex_2006.cfg

# Spec 2017 speed

gcc_s:
	-./build/release/zsim ./tests/2DOC/SPEC2017_speed/gcc_s/gcc_s.cfg

mcf_s:
	-./build/release/zsim ./tests/2DOC/SPEC2017_speed/mcf_s/mcf_s.cfg

wrf_s:
	-./build/release/zsim ./tests/2DOC/SPEC2017_speed/wrf_s/wrf_s.cfg

cam4_s:
	-./build/release/zsim ./tests/2DOC/SPEC2017_speed/cam4_s/cam4_s.cfg

imagick_s:
	-./build/release/zsim ./tests/2DOC/SPEC2017_speed/imagick_s/imagick_s.cfg

# Spec 2017 rate

exchange2_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/exchange2_r/exchange2_r.cfg

gcc_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/gcc_r/gcc_r.cfg

leela_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/leela_r/leela_r.cfg

mcf_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/mcf_r/mcf_r.cfg

omnetpp_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/omnetpp_r/omnetpp_r.cfg

perlbench_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/perlbench_r/perlbench_r.cfg

x264_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/x264_r/x264_r.cfg

xalancbmk_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/xalancbmk_r/xalancbmk_r.cfg

xz_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/xz_r/xz_r.cfg

bwaves_r:
	-./tests/2DOC/SPEC2017_rate/bwaves_r/run.sh

cactuBSSN_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/cactuBSSN_r/cactuBSSN_r.cfg

namd_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/namd_r/namd_r.cfg

parest_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/parest_r/parest_r.cfg

povray_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/povray_r/povray_r.cfg

lbm_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/lbm_r/lbm_r.cfg

wrf_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/wrf_r/wrf_r.cfg

blender_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/blender_r/blender_r.cfg

cam4_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/cam4_r/cam4_r.cfg

imagick_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/imagick_r/imagick_r.cfg

nab_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/nab_r/nab_r.cfg

fotonik3d_r:
	-./build/release/zsim ./tests/2DOC/SPEC2017_rate/fotonik3d_r/fotonik3d_r.cfg

roms_r:
	-./tests/2DOC/SPEC2017_rate/roms_r/run.sh

clean:
	@rm ./.sconsign.dblite
	@rm -rf ./build
