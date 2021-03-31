#!/bin/bash

str="Re  	 Pr\n
1e5 	.72\n
nx	ny	nz\n
63	63	64\n
CFL	nuim	 nuex2 	nuex4\n
2.0 	0.1	 0.1	 0.05\n
Method: explicit(0)  Implicit(1)\n
1\n
Configuration: cubic(0)  sphere(1)\n
0\n
Number of Time Steps\n
   80\n
refrate1_resid.log\n
refrate1_dqnrm.log"

cd /home/ziqiw/data_disk_2/zsim-2DOC/
echo -e $str | ./build/release/zsim ./tests/2DOC/bwaves_r/bwaves_r.cfg