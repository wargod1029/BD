#!/bin/bash
set -e

echo "Starting Operator User Setup..."

# 1. Create User
if id "operator" &>/dev/null; then
    echo "User 'operator' already exists."
else
    echo "Creating user 'operator'..."
    # Try creating with existing group if it exists, else normal creation
    if getent group operator >/dev/null; then
        useradd -m -s /bin/bash -g operator operator
    else
        useradd -m -s /bin/bash operator
    fi
fi

# 2. Set Password
echo "Setting password..."
echo "operator:operator123" | chpasswd

# 3. Add to Groups
echo "Adding to groups..."
usermod -aG dialout,video operator

# 4. Configure ACLs
echo "Configuring Access Control Lists..."
# Allow traversal of /home/kodifly
setfacl -m u:operator:--x /home/kodifly

# Allow Read/Exec of isds_ws
setfacl -R -m u:operator:r-x /home/kodifly/isds_ws
# Set default ACL for future files in isds_ws
setfacl -R -d -m u:operator:r-x /home/kodifly/isds_ws

# Allow traversal of EMSD-Signboard
setfacl -m u:operator:--x /home/kodifly/EMSD-Signboard

# Allow Read/Exec of data
if [ -d "/home/kodifly/EMSD-Signboard/data" ]; then
    setfacl -R -m u:operator:r-x /home/kodifly/EMSD-Signboard/data
    setfacl -R -d -m u:operator:r-x /home/kodifly/EMSD-Signboard/data
else
    echo "Warning: /home/kodifly/EMSD-Signboard/data does not exist. Skipping data ACLs."
fi

# 5. Setup Service
echo "Setting up systemd service..."
mkdir -p /home/operator/.config/systemd/user
cp /home/kodifly/.config/systemd/user/sensor_manager.service /home/operator/.config/systemd/user/
chown -R operator:operator /home/operator/.config

# 6. Setup Desktop Shortcuts
echo "Setting up Desktop shortcuts..."
mkdir -p /home/operator/Desktop
cp /home/kodifly/Desktop/Start_Sensors.desktop /home/operator/Desktop/
cp /home/kodifly/Desktop/Stop_Sensors.desktop /home/operator/Desktop/
chmod +x /home/operator/Desktop/*.desktop
chown -R operator:operator /home/operator/Desktop

# 7. Setup ROS Environment
echo "Configuring .bashrc..."
if ! grep -q "source /opt/ros/noetic/setup.bash" /home/operator/.bashrc; then
    echo "source /opt/ros/noetic/setup.bash" >> /home/operator/.bashrc
fi

echo "Setup Complete!"
echo "You can now log out and log in as 'operator' with password 'operator123'."

