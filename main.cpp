#include <iostream>
#include <unistd.h>
#include <cstdio>

#include <fstream>
#include <sstream>

#include <map>
#include <string>
#include <array>
#include <thread>
#include <cctype> 
#include <chrono>

#include <gio/gio.h>
#include <glib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <sys/socket.h>

#define BT_DEV_NAME "Elink_Bluetooth_Keyboard"
#define BT_DEV_ADDR "48:8F:4C:FF:1E:6A"
#define HID_PROFILE_UUID "00001124-0000-1000-8000-00805f9b34fb"
#define SDP_RECORD_PATH "/etc/bluetooth/sdp_record.xml"
#define P_CTRL 0x11
#define P_INTR 0x13

struct BluetoothConnection {
    int control_socket;
    int interrupt_socket;
    int control_client;
    int interrupt_client;
};

/* === INIT BLUETOOTH DEVICE === */
void init_bt_device(){
    std::cout << "Configuring bluetooth device" << std::endl;
    system("hciconfig hci0 up");
    system("hciconfig hci0 class 0x05C0");

    std::string command = "hciconfig hci0 name \"" BT_DEV_NAME "\"";
    system(command.c_str());
}

/* === LOAD SDP RECORD === */
std::string load_sdp_service_record(const char* filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open XML file: " << filename << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/* === LISTEN_FOR_CONNECTION === */
BluetoothConnection listen_for_connections(){
    std::cout << "Bluetooth HID L2CAP Server starting..." << std::endl;

    BluetoothConnection conn = {0, 0, 0, 0};

    std::cout << "1. Creating L2CAP server sockets..." << std::endl;

    /* Init socket for Control and Interupt */
    conn.control_socket = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    conn.interrupt_socket = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

    if (conn.control_socket < 0 || conn.interrupt_socket < 0) {
        std::cerr << "Failed to create L2CAP server sockets!" << std::endl;
        return conn;
    }

    std::cout << "2. Binding sockets to address and ports..." << std::endl;

    /* Local address - server */
    sockaddr_l2 loc_addr_ctrl{}, loc_addr_intr{};
    memset(&loc_addr_ctrl, 0, sizeof(loc_addr_ctrl));
    memset(&loc_addr_intr, 0, sizeof(loc_addr_intr));

    str2ba(BT_DEV_ADDR, &loc_addr_ctrl.l2_bdaddr);
    str2ba(BT_DEV_ADDR, &loc_addr_intr.l2_bdaddr);

    loc_addr_ctrl.l2_family = AF_BLUETOOTH;
    loc_addr_ctrl.l2_psm = htobs(P_CTRL);

    loc_addr_intr.l2_family = AF_BLUETOOTH;
    loc_addr_intr.l2_psm = htobs(P_INTR);

    /* Bind control channel */
    if (bind(conn.control_socket, (struct sockaddr *)&loc_addr_ctrl, sizeof(loc_addr_ctrl)) < 0) {
        std::cerr << "Failed to bind control socket!" << std::endl;
        return conn;
    }
    /* Bind interupt channel */
    if (bind(conn.interrupt_socket, (struct sockaddr *)&loc_addr_intr, sizeof(loc_addr_intr)) < 0) {
        std::cerr << "Failed to bind interrupt socket!" << std::endl;
        return conn;
    }

    std::cout << "3. Listening for incoming connections..." << std::endl;

    /* Listen */
    listen(conn.control_socket, 1);
    listen(conn.interrupt_socket, 1);

    std::cout << "4. Waiting for Control channel connection..." << std::endl;

    /* Accept control connection */
    sockaddr_l2 rem_addr_ctrl{};
    socklen_t opt = sizeof(rem_addr_ctrl);
    conn.control_client = accept(conn.control_socket, (struct sockaddr *)&rem_addr_ctrl, &opt);

    if (conn.control_client < 0) {
        std::cerr << "Failed to accept control connection!" << std::endl;
        return conn;
    }

    char ctrl_bdaddr[18] = { 0 };
    ba2str(&rem_addr_ctrl.l2_bdaddr, ctrl_bdaddr);

    std::cout << "5. Accepted control connection from " << ctrl_bdaddr << std::endl;

    std::cout << "6. Waiting for Interrupt channel connection..." << std::endl;

    /* Accept interrupt connection */
    sockaddr_l2 rem_addr_intr{};
    opt = sizeof(rem_addr_intr);
    conn.interrupt_client = accept(conn.interrupt_socket, (struct sockaddr *)&rem_addr_intr, &opt);

    if (conn.interrupt_client < 0) {
        std::cerr << "Failed to accept interrupt connection!" << std::endl;
        return conn;
    }

    char intr_bdaddr[18] = { 0 };
    ba2str(&rem_addr_intr.l2_bdaddr, intr_bdaddr);
    std::cout << "7. Accepted interrupt connection from " << intr_bdaddr << std::endl;

    std::cout << "8. L2CAP HID channels are ready!" << std::endl;

    return conn;
}

/* === KEY TABLE === */
std::map<std::string, uint8_t> keytable = {
    {"KEY_RESERVED", 0 },
    {"KEY_ESC", 41 },
    {"KEY_1", 30},
    {"KEY_2", 31},
    {"KEY_3", 32},
    {"KEY_4", 33},
    {"KEY_5", 34},
    {"KEY_6", 35},
    {"KEY_7", 36},
    {"KEY_8", 37},
    {"KEY_9", 38},
    {"KEY_0", 39},
    {"KEY_MINUS", 45},
    {"KEY_EQUAL", 46},
    {"KEY_BACKSPACE", 42},
    {"KEY_TAB", 43},
    {"KEY_Q", 20},
    {"KEY_W", 26},
    {"KEY_E", 8},
    {"KEY_R", 21},
    {"KEY_T", 23},
    {"KEY_Y", 28},
    {"KEY_U", 24},
    {"KEY_I", 12},
    {"KEY_O", 18},
    {"KEY_P", 19},
    {"KEY_LEFTBRACE", 47},
    {"KEY_RIGHTBRACE", 48},
    {"KEY_ENTER", 40},
    {"KEY_LEFTCTRL", 224},
    {"KEY_A", 4},
    {"KEY_S", 22},
    {"KEY_D", 7},
    {"KEY_F", 9},
    {"KEY_G", 10},
    {"KEY_H", 11},
    {"KEY_J", 13},
    {"KEY_K", 14},
    {"KEY_L", 15},
    {"KEY_SEMICOLON", 51},
    {"KEY_APOSTROPHE", 52},
    {"KEY_GRAVE", 53},
    {"KEY_LEFTSHIFT", 225},
    {"KEY_BACKSLASH", 50},
    {"KEY_Z", 29},
    {"KEY_X", 27},
    {"KEY_C", 6},
    {"KEY_V", 25},
    {"KEY_B", 5},
    {"KEY_N", 17},
    {"KEY_M", 16},
    {"KEY_COMMA", 54},
    {"KEY_DOT", 55},
    {"KEY_SLASH", 56},
    {"KEY_RIGHTSHIFT", 229},
    {"KEY_KPASTERISK", 85},
    {"KEY_LEFTALT", 226},
    {"KEY_SPACE", 44},
    {"KEY_CAPSLOCK", 57},
    {"KEY_F1", 58},
    {"KEY_F2", 59},
    {"KEY_F3", 60},
    {"KEY_F4", 61},
    {"KEY_F5", 62},
    {"KEY_F6", 63},
    {"KEY_F7", 64},
    {"KEY_F8", 65},
    {"KEY_F9", 66},
    {"KEY_F10", 67},
    {"KEY_NUMLOCK", 83},
    {"KEY_SCROLLLOCK", 71},
    {"KEY_KP7", 95},
    {"KEY_KP8", 96},
    {"KEY_KP9", 97},
    {"KEY_KPMINUS", 86},
    {"KEY_KP4", 92},
    {"KEY_KP5", 93},
    {"KEY_KP6", 94},
    {"KEY_KPPLUS", 87},
    {"KEY_KP1", 89},
    {"KEY_KP2", 90},
    {"KEY_KP3", 91},
    {"KEY_KP0", 98},
    {"KEY_KPDOT", 99},
    {"KEY_ZENKAKUHANKAKU", 148},
    {"KEY_102ND", 100},
    {"KEY_F11", 68},
    {"KEY_F12", 69},
    {"KEY_RO", 135},
    {"KEY_KATAKANA", 146},
    {"KEY_HIRAGANA", 147},
    {"KEY_HENKAN", 138},
    {"KEY_KATAKANAHIRAGANA", 136},
    {"KEY_MUHENKAN", 139},
    {"KEY_KPJPCOMMA", 140},
    {"KEY_KPENTER", 88},
    {"KEY_RIGHTCTRL", 228},
    {"KEY_KPSLASH", 84},
    {"KEY_SYSRQ", 70},
    {"KEY_RIGHTALT", 230},
    {"KEY_HOME", 74},
    {"KEY_UP", 82},
    {"KEY_PAGEUP", 75},
    {"KEY_LEFT", 80},
    {"KEY_RIGHT", 79},
    {"KEY_END", 77},
    {"KEY_DOWN", 81},
    {"KEY_PAGEDOWN", 78},
    {"KEY_INSERT", 73},
    {"KEY_DELETE", 76},
    {"KEY_MUTE", 239},
    {"KEY_VOLUMEDOWN", 238},
    {"KEY_VOLUMEUP", 237},
    {"KEY_POWER", 102},
    {"KEY_KPEQUAL", 103},
    {"KEY_PAUSE", 72},
    {"KEY_KPCOMMA", 133},
    {"KEY_HANGEUL", 144},
    {"KEY_HANJA", 145},
    {"KEY_YEN", 137},
    {"KEY_LEFTMETA", 227},
    {"KEY_RIGHTMETA", 231},
    {"KEY_COMPOSE", 101},
    {"KEY_STOP", 243},
    {"KEY_AGAIN", 121},
    {"KEY_PROPS", 118},
    {"KEY_UNDO", 122},
    {"KEY_FRONT", 119},
    {"KEY_COPY", 124},
    {"KEY_OPEN", 116},
    {"KEY_PASTE", 125},
    {"KEY_FIND", 244},
    {"KEY_CUT", 123},
    {"KEY_HELP", 117},
    {"KEY_CALC", 251},
    {"KEY_SLEEP", 248},
    {"KEY_WWW", 240},
    {"KEY_COFFEE", 249},
    {"KEY_BACK", 241},
    {"KEY_FORWARD", 242},
    {"KEY_EJECTCD", 236},
    {"KEY_NEXTSONG", 235},
    {"KEY_PLAYPAUSE", 232},
    {"KEY_PREVIOUSSONG", 234},
    {"KEY_STOPCD", 233},
    {"KEY_REFRESH", 250},
    {"KEY_EDIT", 247},
    {"KEY_SCROLLUP", 245},
    {"KEY_SCROLLDOWN", 246},
    {"KEY_F13", 104},
    {"KEY_F14", 105},
    {"KEY_F15", 106},
    {"KEY_F16", 107},
    {"KEY_F17", 108},
    {"KEY_F18", 109},
    {"KEY_F19", 110},
    {"KEY_F20", 111},
    {"KEY_F21", 112},
    {"KEY_F22", 113},
    {"KEY_F23", 114},
    {"KEY_F24", 115}
};

/* === MOD KEY === */
std::map<std::string, int> modkeys = {
    {"KEY_RIGHTMETA", 0},
    {"KEY_RIGHTALT", 1},
    {"KEY_RIGHTSHIFT", 2},
    {"KEY_RIGHTCTRL", 3},
    {"KEY_LEFTMETA", 4},
    {"KEY_LEFTALT", 5},
    {"KEY_LEFTSHIFT", 6},
    {"KEY_LEFTCTRL", 7}
};

/* === FUNCTION CONVER EVDEV KEYCODE INTO HID KEYCODE === */
int convert(const std::string& evdev_keycode) {
    if (keytable.find(evdev_keycode) != keytable.end()) {
        return keytable[evdev_keycode];
    } else {
        std::cerr << "Key not found: " << evdev_keycode << std::endl;
        return -1; 
    }
}

/* === FUNCTION MODIFY KEY CHECKER === */
int modkey(const std::string& evdev_keycode) {
    if (modkeys.find(evdev_keycode) != modkeys.end()) {
        return modkeys[evdev_keycode];
    } else {
        return -1;
    }
}

/* === SEND STRING === */
bool send_string(const BluetoothConnection &conn, const std::string &message) {
    if (conn.interrupt_client <= 0) {
        std::cerr << "Interrupt client socket not connected!" << std::endl;
        return false;
    }

    ssize_t bytes_sent = send(conn.interrupt_client, message.c_str(), message.length(), 0);

    if (bytes_sent < 0) {
        perror("Failed to send data on interrupt channel");
        return false;
    }

    // std::cout << "Sent " << bytes_sent << " bytes: " << message << std::endl;
    return true;
}

/* === SEND KEY === */
bool send_keys(const BluetoothConnection &conn, uint8_t modifier_byte, const std::array<uint8_t, 6> &keys) {
    /* HID input report: 10 bytes
     *   0  : Button states (buttons 1-8)
     *   1  : Additional buttons
     * 2 - 3: X-axis of the joystick
     * 4 - 5: Y-axis of the joystick
     * 6 - 7: Z-axis or trigger
     * 8 - 9: Other information / padding
     */
    uint8_t cmd_bytes[10];

    /* 1. Prefix (Bluetooth HID) */
    cmd_bytes[0] = 0xA1;            // HID input report prefix 
    cmd_bytes[1] = 0x01;            // Report ID 
    cmd_bytes[2] = modifier_byte;   // Modifier byte (Ctrl, Shift, Alt...)
    cmd_bytes[3] = 0x00;            // Reserved byte (always zero)

    size_t num_keys = keys.size() < 6 ? keys.size() : 6;
    
    /* 2. Copy key_array into cmd_bytes */
    memcpy(&cmd_bytes[4], keys.data(), num_keys);

    /* 3. Send HID Report through interupt channel */
    ssize_t bytes_sent = write(conn.interrupt_client, cmd_bytes, sizeof(cmd_bytes));
    if (bytes_sent < 0) {
        perror("Error sending HID report to interrupt channel");
        return false;
    } else {
        printf("Successfully sent %zd bytes to interrupt channel\n", bytes_sent);
        return true;
    }
}

/* === SEND MOUSE === */
bool send_mouse(const BluetoothConnection &conn, uint8_t buttons, const std::array<int8_t, 3> &rel_move) {
    /* 
     * Mouse HID Report Format
     * Byte 0: Button
     * Byte 1: X movement (relative)
     * Byte 2: Y movement (relative)
     * Byte 2: Y movement (relative)
     * Byte 3: Wheel movement 
     */

    /* Check connection is available? */
    if (conn.interrupt_client <= 0) {
        std::cerr << "Interrupt client socket not connected!" << std::endl;
        return false;
    }

    uint8_t cmd_bytes[6];

    cmd_bytes[0] = 0xA1;
    cmd_bytes[1] = 0x02;
    cmd_bytes[2] = buttons;
    cmd_bytes[3] = rel_move[0];
    cmd_bytes[4] = rel_move[1];
    cmd_bytes[5] = rel_move[2];

    /* Send data report through interrupt socket */
    ssize_t bytes_sent = write(conn.interrupt_client, cmd_bytes, sizeof(cmd_bytes));

    if (bytes_sent < 0) {
        perror("Error sending mouse report to interrupt channel");
        return false;
    }

    /* Debug - optional */
    std::cout << "Sending mouse report -> "
              << "Buttons: " << (int)buttons
              << ", X: " << (int)rel_move[0]
              << ", Y: " << (int)rel_move[1]
              << ", Wheel: " << (int)rel_move[2]
              << std::endl;

    return true;
}

/* === SEND STRING UPGRADE === */
void send_string_input(const BluetoothConnection &conn, const std::string &text, float key_down_time = 0.01, float key_delay = 0.05) {
    for (const char &c : text) {
        if (c < 32 || c > 126) {
            std::cerr << "Skipping unsupported character: " << c << std::endl;
            continue;
        }

        std::string key_name = "KEY_"; 
        key_name += static_cast<char>(std::toupper(c)); // a -> A

        int hid_code = convert(key_name);
        if (hid_code < 0) {
            std::cerr << "Key not found in keytable for character: " << c << std::endl;
            continue;
        }

        uint8_t modifier = 0;

        /* CAPITAL characters */
        if (std::isupper(c)) {
            modifier |= (1 << modkeys["KEY_LEFTSHIFT"]);
        }

        /* SPECIAL characters */
        const std::string special_chars = "!@#$%^&*()_+{}|:\"<>?";
        if (special_chars.find(c) != std::string::npos) {
            modifier |= (1 << modkeys["KEY_LEFTSHIFT"]);
        }

        std::array<uint8_t, 6> keys = { static_cast<uint8_t>(hid_code), 0, 0, 0, 0, 0 };

        /* Press key */
        send_keys(conn, modifier, keys);
        std::this_thread::sleep_for(std::chrono::duration<float>(key_down_time));

        /* Release key */
        std::array<uint8_t, 6> empty_keys = { 0, 0, 0, 0, 0, 0 };
        send_keys(conn, 0, empty_keys);
        std::this_thread::sleep_for(std::chrono::duration<float>(key_delay));
    }
}

/* === CLOSE CONNECTION === */
void close_connection(BluetoothConnection &conn){
    std::cout << "Bluetooth HID connection closed!" << std::endl; 

    close(conn.control_client);
    close(conn.interrupt_client);
}

/* === INIT BLUEZ PROFILE === */
void init_bluez_profile(GDBusProxy *proxy){
    if (!proxy) {
        std::cerr << "Proxy is null! Cannot register profile." << std::endl;
        return;
    }
    
    GVariant *ret;
    GError *error = NULL;

    std::cout << "Registering HID profile" << std::endl;
    std::string sdp_service_record = load_sdp_service_record(SDP_RECORD_PATH);
    if (sdp_service_record.empty()) {
        std::cerr << "SDP Service Record is empty!" << std::endl;
        return;
    }

    /* Build options */
    GVariantBuilder options_builder;
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE("a{sv}"));

    g_variant_builder_add(&options_builder, "{sv}",
                                            "ServiceRecord",
                                            g_variant_new_string(sdp_service_record.c_str()));
    g_variant_builder_add(&options_builder, "{sv}",
                                            "Service",
                                            g_variant_new_string(HID_PROFILE_UUID));
    g_variant_builder_add(&options_builder, "{sv}",
                                            "Name",
                                            g_variant_new_string(BT_DEV_NAME));
    g_variant_builder_add(&options_builder, "{sv}",
                                            "Role",
                                            g_variant_new_string("server"));                                     
    g_variant_builder_add(&options_builder, "{sv}",
                                            "RequireAuthentication",
                                            g_variant_new_boolean(false));
    g_variant_builder_add(&options_builder, "{sv}",
                                            "RequireAuthorization",
                                            g_variant_new_boolean(false));  

    GVariant *profile = g_variant_new("(osa{sv})",
                                      "/org/bluez/bluetoothhidprofile", 
                                      HID_PROFILE_UUID,                
                                      &options_builder);                
    
    ret = g_dbus_proxy_call_sync(proxy,
                                 "RegisterProfile",
                                 profile,
                                 G_DBUS_CALL_FLAGS_NONE,
                                 -1,
                                 NULL,
                                 &error);

    if (error) {
        std::cerr << "Failed to register HID profile: " << error->message << std::endl;
        g_error_free(error);
        return;
    }

    std::cout << "HID profile registered successfully!" << std::endl;
    if (ret) g_variant_unref(ret);  
}

/* === INIT SERVER === */
void init_server(){
    GDBusProxy *proxy = NULL;
    GDBusConnection *conn = NULL;
    GError *error = NULL;
    GMainLoop *loop = g_main_loop_new(NULL, false);

    std::cout << "Starting HID Profile Server..." << std::endl;

    /* Step 1: Connect to system bus */
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        std::cerr << "Failed to connect to D-Bus: " << error->message << std::endl;
        g_error_free(error);
        return;
    }

    /* Step 2: Create proxy for ProfileManager1 */
    proxy = g_dbus_proxy_new_sync(conn,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  "org.bluez",
                                  "/org/bluez",
                                  "org.bluez.ProfileManager1",
                                  NULL,
                                  &error);

    if (error) {
        std::cerr << "Failed to create D-Bus proxy: " << error->message << std::endl;
        g_error_free(error);
        g_object_unref(conn);
        return;
    }

    /* Step 3: Init the bluetooth device */
    init_bt_device();

    /* Step 4: Register the HID profile */
    init_bluez_profile(proxy);

    /* Step 5: Listen new connection */
    BluetoothConnection bt_conn = listen_for_connections();
    if (bt_conn.control_client > 0 && bt_conn.interrupt_client > 0) {
        std::cout << "Ready to send HID reports!" << std::endl;

        std::string v;

        while (true) {

            std::cout << "input str>>> ";
            std::getline(std::cin, v);
            
            if (v == "q") {
                std::cout << "Exiting loop..." << std::endl;
                close_connection(bt_conn);

                /* Cleanup */
                g_main_loop_unref(loop);
                g_object_unref(proxy);
                g_object_unref(conn);

                return; /* Exit init_server */ 
            } else if (v == "m") {
                std::cout << "Send mouse" << std::endl;

                std::array<int8_t, 3> mouse_move = {10, 30, 1};
                if (!send_mouse(bt_conn, 0, mouse_move)) {
                    std::cerr << "Failed to send mouse report!" << std::endl;
                }
            } else {
                std::cout << "Send: " << v << std::endl;
                send_string_input(bt_conn, v);
            }
        }
    }

    /* Start the main loop */
    std::cout << "Entering main loop. Press Ctrl+C to exit." << std::endl;
    g_main_loop_run(loop);

    /* Cleanup */
    g_main_loop_unref(loop);
    g_object_unref(proxy);
    g_object_unref(conn);
}

/* === MAIN === */
int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        std::cerr << "Only root can run this script" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    std::cout << "Restarting bluetooth service" << std::endl;
    system("service bluetooth stop");
    system("/usr/libexec/bluetooth/bluetoothd -p time&");
    system("hciconfig hci0 down");
    system("hciconfig hci0 up");
    system("hciconfig hci0 piscan");

    init_server();
    return 0;
}