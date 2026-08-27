cd $HOME/catkin_ws/src
git clone https://github.com/MapIV/eagleye.git -b main-ros2
vcs import . < eagleye/eagleye.repos
sudo apt-get install -y libgeographic-dev geographiclib-tools geographiclib-doc
sudo geographiclib-get-geoids best
sudo mkdir /usr/share/GSIGEO
sudo cp llh_converter/data/gsigeo2011_ver2_1.asc /usr/share/GSIGEO/
cd ..
rosdep install --from-paths src --ignore-src -r -y
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release