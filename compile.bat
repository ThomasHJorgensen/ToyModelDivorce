cd /d "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/"
call vcvarsall.bat x64
cd /d "C:\Users\bnl429\Dropbox\Projects\2022_Divorce\ToyModel\ToyModelDivorce"
cl /LD /EHsc /Ox /openmp cppfuncs/solve.cpp setup_omp.cpp cppfuncs/nlopt-2.4.2-dll64/libnlopt-0.lib  
