sudo apt-get install gfortran
cd $HOME
git clone -b rtklib_ros_bridge_b34 https://github.com/MapIV/RTKLIB.git
cd $HOME/RTKLIB/lib/iers/gcc/
make
cd $HOME/RTKLIB/app/consapp
make