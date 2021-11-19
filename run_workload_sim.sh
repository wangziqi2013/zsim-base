#!/bin/bash

echo "%include tests/2DOC/cc_simple_uncomp_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make linked_list_all

echo "%include tests/2DOC/cc_simple_BDI_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make linked_list_all

echo "%include tests/2DOC/cc_simple_FPC_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make linked_list_all

echo "%include tests/2DOC/cc_simple_CPACK_1_4_seg_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make linked_list_all

echo "%include tests/2DOC/cc_simple_uncomp_2x_llc_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make linked_list_all

echo "%include tests/2DOC/cc_simple_DISH_scheme1_1_4_conf.txt" > ./tests/2DOC/scheme_conf.txt
#make linked_list_all

echo "%include tests/2DOC/cc_simple_MBDv4_1_4_seg_opt3_1pt5x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
make linked_list_all

echo "%include tests/2DOC/cc_scache_Thesaurus_conf.txt" > ./tests/2DOC/scheme_conf.txt
make linked_list_all

echo "%include tests/2DOC/cc_simple_MBDv4_4_1_seg_opt3_1pt5x_tag_conf.txt" > ./tests/2DOC/scheme_conf.txt
make linked_list_all




