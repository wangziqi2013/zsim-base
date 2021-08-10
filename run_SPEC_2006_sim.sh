#!/bin/bash

# Uncompressed workload
echo "%include tests/2DOC/cc_simple_uncomp_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make perlbench_2006
#make bzip2_2006
#make gcc_2006
#make bwaves_2006
#make gamess_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make gromacs_2006
#make cactusADM_2006
#make leslie3d_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make soplex_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make gemsFDTD_2006
#make libquantum_2006
#make h264ref_2006
#make tonto_2006
#make lbm_2006
#make omnetpp_2006
#make astar_2006
#make wrf_2006
#make sphinx3_2006
#make xalancbmk_2006

#make exchange2_r
#make gcc_r
#make leela_r
#make mcf_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xalancbmk_r
#make xz_r
#make bwaves_r
#make cactuBSSN_r
#make namd_r
#make parest_r
#make povray_r
#make lbm_r
#make wrf_r
#make blender_r
#make cam4_r
#make imagick_r
#make nab_r
#make fotonik3d_r
#make roms_r
#make deepsjeng_r

# 2x LLC workload - only uses those that achieves low L1 miss
echo "%include tests/2DOC/cc_simple_uncomp_2x_llc_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006 # Start from here and below to continue SPEC 2006 low L1 miss sim
#make astar_2006
#make wrf_2006

#make exchange2_r
#make gcc_r
#make leela_r
#make mcf_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xalancbmk_r
#make xz_r
#make bwaves_r
#make cactuBSSN_r
#make namd_r
#make parest_r
#make povray_r
#make lbm_r
#make wrf_r
#make blender_r
#make cam4_r
#make imagick_r
#make nab_r
#make fotonik3d_r
#make roms_r
#make deepsjeng_r

# MBDv4 workload - only uses those that achieves low L1 miss
echo "%include tests/2DOC/cc_simple_MBDv4_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

echo "%include tests/2DOC/cc_simple_FPC_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006 
#make tonto_2006 
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

echo "%include tests/2DOC/cc_simple_CPACK_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

echo "%include tests/2DOC/cc_simple_BDI_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

echo "%include tests/2DOC/cc_scache_BDI_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

echo "%include tests/2DOC/cc_simple_MBDv0_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt

#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

echo "%include tests/2DOC/cc_simple_MBD_Thesaurus_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

echo "%include tests/2DOC/cc_simple_MBD_Thesaurus_1_4_seg_1pt5x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
make bzip2_2006
make gcc_2006
make mcf_2006
make milc_2006
make zeusmp_2006
make leslie3d_2006
make soplex_2006
make gemsFDTD_2006
make libquantum_2006
make lbm_2006
make omnetpp_2006
make sphinx3_2006
make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

make cactuBSSN_r
make fotonik3d_r
make imagick_r
make lbm_r
make mcf_r
make parest_r
make roms_r
make wrf_r
make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

echo "%include tests/2DOC/cc_simple_MBD_Thesaurus_1_4_seg_2x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
make bzip2_2006
make gcc_2006
make mcf_2006
make milc_2006
make zeusmp_2006
make leslie3d_2006
make soplex_2006
make gemsFDTD_2006
make libquantum_2006
make lbm_2006
make omnetpp_2006
make sphinx3_2006
make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

make cactuBSSN_r
make fotonik3d_r
make imagick_r
make lbm_r
make mcf_r
make parest_r
make roms_r
make wrf_r
make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

echo "%include tests/2DOC/cc_simple_MBDv4_1_4_seg_opt1_1pt5x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

echo "%include tests/2DOC/cc_simple_MBDv4_1_4_seg_opt3_1pt5x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r

echo "%include tests/2DOC/cc_simple_MBDv4_1_4_seg_opt3_evict_opt0_1pt5x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make bzip2_2006
#make gcc_2006
#make mcf_2006
#make milc_2006
#make zeusmp_2006
#make leslie3d_2006
#make soplex_2006
#make gemsFDTD_2006
#make libquantum_2006
#make lbm_2006
#make omnetpp_2006
#make sphinx3_2006
#make xalancbmk_2006

#make perlbench_2006
#make bwaves_2006
#make gamess_2006
#make gromacs_2006
#make cactusADM_2006
#make namd_2006
#make gobmk_2006
#make dealII_2006
#make calculix_2006
#make hmmer_2006
#make sjeng_2006
#make h264ref_2006
#make tonto_2006
#make astar_2006
#make wrf_2006

#make cactuBSSN_r
#make fotonik3d_r
#make imagick_r
#make lbm_r
#make mcf_r
#make parest_r
#make roms_r
#make wrf_r
#make xalancbmk_r

#make exchange2_r
#make gcc_r
#make leela_r
#make omnetpp_r
#make perlbench_r
#make x264_r
#make xz_r
#make bwaves_r
#make namd_r
#make povray_r
#make blender_r
#make cam4_r
#make nab_r
#make deepsjeng_r
