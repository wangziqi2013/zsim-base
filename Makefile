

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

# Spec 2006

WORKLOAD_PATH_2006=/home/ziqiw/data_disk_2/SPEC2006/benchspec/CPU2006

perlbench_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/perlbench_2006/perlbench_2006.cfg

bzip2_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/bzip2_2006/bzip2_2006.cfg

gcc_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/gcc_2006/gcc_2006.cfg

bwaves_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/bwaves_2006/bwaves_2006.cfg

gamess_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/gamess_2006/gamess_2006.cfg

mcf_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/mcf_2006/mcf_2006.cfg

milc_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/milc_2006/milc_2006.cfg

zeusmp_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/zeusmp_2006/zeusmp_2006.cfg

gromacs_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/gromacs_2006/gromacs_2006.cfg

cactusADM_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/cactusADM_2006/cactusADM_2006.cfg

leslie3d_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/leslie3d_2006/leslie3d_2006.cfg

namd_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/namd_2006/namd_2006.cfg

gobmk_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/gobmk_2006/gobmk_2006.cfg

dealII_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/dealII_2006/dealII_2006.cfg

soplex_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/soplex_2006/soplex_2006.cfg

povray_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/povray_2006/povray_2006.cfg

calculix_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/calculix_2006/calculix_2006.cfg

hmmer_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/hmmer_2006/hmmer_2006.cfg

sjeng_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/sjeng_2006/sjeng_2006.cfg

gemsFDTD_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/gemsFDTD_2006/gemsFDTD_2006.cfg

libquantum_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/libquantum_2006/libquantum_2006.cfg

h264ref_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/h264ref_2006/h264ref_2006.cfg

tonto_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/tonto_2006/tonto_2006.cfg

lbm_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/lbm_2006/lbm_2006.cfg

omnetpp_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/omnetpp_2006/omnetpp_2006.cfg

astar_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/astar_2006/astar_2006.cfg

wrf_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/wrf_2006/wrf_2006.cfg

sphinx3_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/sphinx3_2006/sphinx3_2006.cfg

xalancbmk_2006:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2006) ./build/release/zsim ./tests/2DOC/SPEC2006/xalancbmk_2006/xalancbmk_2006.cfg

# Spec 2017 speed

WORKLOAD_PATH_2017=/home/ziqiw/data_disk_2/SPEC2017/benchspec/CPU/

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
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/exchange2_r/exchange2_r.cfg

gcc_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/gcc_r/gcc_r.cfg

leela_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/leela_r/leela_r.cfg

mcf_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/mcf_r/mcf_r.cfg

omnetpp_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/omnetpp_r/omnetpp_r.cfg

perlbench_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/perlbench_r/perlbench_r.cfg

x264_r:
	$(WORKLOAD_PATH_2017)/525.x264_r/build/build_base_mytest-m64.0000/ldecod_r -i $(WORKLOAD_PATH_2017)/525.x264_r/data/refrate/input/BuckBunny.264 -o $(WORKLOAD_PATH_2017)/525.x264_r/data/refrate/input/BuckBunny.yuv
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/x264_r/x264_r.cfg

xalancbmk_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/xalancbmk_r/xalancbmk_r.cfg

xz_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/xz_r/xz_r.cfg

bwaves_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/bwaves_r/bwaves_r.cfg

cactuBSSN_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/cactuBSSN_r/cactuBSSN_r.cfg

namd_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/namd_r/namd_r.cfg

parest_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/parest_r/parest_r.cfg

povray_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/povray_r/povray_r.cfg

lbm_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/lbm_r/lbm_r.cfg

wrf_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/wrf_r/wrf_r.cfg

blender_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/blender_r/blender_r.cfg

cam4_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/cam4_r/cam4_r.cfg

imagick_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/imagick_r/imagick_r.cfg

nab_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/nab_r/nab_r.cfg

fotonik3d_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/fotonik3d_r/fotonik3d_r.cfg

roms_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/roms_r/roms_r.cfg

deepsjeng_r:
	-WORKLOAD_PATH=$(WORKLOAD_PATH_2017) ./build/release/zsim ./tests/2DOC/SPEC2017_rate/deepsjeng_r/deepsjeng_r.cfg

clean:
	@rm ./.sconsign.dblite
	@rm -rf ./build
