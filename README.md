### How to install?

Before build process, copy the sdp_record.xml file to the bluetooth configuration directory on your embedded device.

    cp sdp_record.xml /etc/bluetooth

Command build.

    meson buildir
    ninja -C buildir