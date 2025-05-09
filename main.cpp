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
#include <algorithm>
#include <errno.h>

#include <gio/gio.h>
#include <glib.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#define BT_DEV_NAME "Elink Bluetooth Keyboard"
#define HID_PROFILE_UUID "00001124-0000-1000-8000-00805f9b34fb"
#define SDP_RECORD_PATH "/etc/bluetooth/sdp_record.xml"
#define AGENT_PATH "/org/bluez/agent"
#define P_CTRL 0x11
#define P_INTR 0x13

GDBusProxy *proxy = NULL;
GDBusConnection *conn = NULL;
GError *error = NULL;
GMainLoop *loop = g_main_loop_new(NULL, false);

struct BluetoothConnection {
    int control_socket;
    int interrupt_socket;
    int control_client;
    int interrupt_client;
};

void cleanup_connection(BluetoothConnection &conn){

    if (conn.control_client > 0) {
        close(conn.control_client);
        conn.control_client = 0;
    }
    
    if (conn.interrupt_client > 0) {
        close(conn.interrupt_client);
        conn.interrupt_client = 0;
    }

    if (conn.control_socket > 0) {
        close(conn.control_socket);
        conn.control_socket = 0;
    }

    if (conn.interrupt_socket > 0) {
        close(conn.interrupt_socket);
        conn.interrupt_socket = 0;
    }

}

/*
 * The recv() calls are used to receive messages from a socket.
 * They may be used to receive data on both connectionless 
 * and connection-oriented sockets.
 * So, use it to check bluetooth connection between embedded devices
 *                                     with another bluetooth device.
 */

bool is_connected(int sockfd) {
    char buf;
    
    int result = recv(sockfd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);

    if (result == 0) {
        std::cout << "Peer has closed the connection!" << std::endl;
        return false;
    } else if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        } else {
            std::cerr << "Socket error: " << strerror(errno) << std::endl;
            return false;
        }
    }
    return true;
}

std::string getBluetoothDeviceAddress() {
    int device_id = hci_get_route(NULL);
    if (device_id < 0) {
        std::cerr << "[ERROR] No available Bluetooth devices found!" << std::endl;
        return "";
    }

    int sock = hci_open_dev(device_id);
    if (sock < 0) {
        std::cerr << "[ERROR] Failed to open HCI device: " << strerror(errno) << std::endl;
        return "";
    }

    bdaddr_t bdaddr;
    if (hci_read_bd_addr(sock, &bdaddr, 1000) < 0) {
        std::cerr << "[ERROR] hci_read_bd_addr failed: " << strerror(errno) << std::endl;
        close(sock);
        return "";
    }

    char addr_str[18];
    ba2str(&bdaddr, addr_str);

    std::string bt_address(addr_str);
    std::cout << "Local Bluetooth Address: " << bt_address << std::endl;

    close(sock);
    return bt_address;
}

void init_bt_device(){
    std::cout << "Configuring bluetooth device" << std::endl;
    system("hciconfig hci0 up");
    system("hciconfig hci0 class 0x05C0");

    std::string command = "hciconfig hci0 name \"" BT_DEV_NAME "\"";
    system(command.c_str());
    system("hciconfig hci0 piscan");
}

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

BluetoothConnection listen_for_connections(){

    std::string bt_addr_str = getBluetoothDeviceAddress();

    std::cout << "Bluetooth HID L2CAP Server starting..." << std::endl;

    BluetoothConnection conn = {0, 0, 0, 0};

    std::cout << "1. Creating L2CAP server sockets..." << std::endl;

    /* Init socket for Control and Interupt */
    conn.control_socket = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    conn.interrupt_socket = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);

    if (conn.control_socket < 0 || conn.interrupt_socket < 0) {
        std::cerr << "Failed to create L2CAP server sockets!" << std::endl;
        cleanup_connection(conn);
        return conn;
    }

    std::cout << "2. Binding sockets to address and ports..." << std::endl;  

    sockaddr_l2 loc_addr_ctrl{}, loc_addr_intr{};
    memset(&loc_addr_ctrl, 0, sizeof(loc_addr_ctrl));
    memset(&loc_addr_intr, 0, sizeof(loc_addr_intr));

    str2ba(bt_addr_str.c_str(), &loc_addr_ctrl.l2_bdaddr);
    str2ba(bt_addr_str.c_str(), &loc_addr_intr.l2_bdaddr);

    loc_addr_ctrl.l2_family = AF_BLUETOOTH;
    loc_addr_ctrl.l2_psm = htobs(P_CTRL);

    loc_addr_intr.l2_family = AF_BLUETOOTH;
    loc_addr_intr.l2_psm = htobs(P_INTR);

    /* Bind control channel */
    if (bind(conn.control_socket, (struct sockaddr *)&loc_addr_ctrl, sizeof(loc_addr_ctrl)) < 0) {
        std::cerr << "Failed to bind control socket!" << std::endl;
        cleanup_connection(conn);
        return conn;
    }
    /* Bind interupt channel */
    if (bind(conn.interrupt_socket, (struct sockaddr *)&loc_addr_intr, sizeof(loc_addr_intr)) < 0) {
        std::cerr << "Failed to bind interrupt socket!" << std::endl;
        cleanup_connection(conn);
        return conn;
    }

    std::cout << "3. Listening for incoming connections..." << std::endl;

    /* Listen */
    listen(conn.control_socket, 1);
    listen(conn.interrupt_socket, 1);

    /* Accept control connection */
    sockaddr_l2 rem_addr_ctrl{};
    socklen_t opt = sizeof(rem_addr_ctrl);
    conn.control_client = accept(conn.control_socket, (struct sockaddr *)&rem_addr_ctrl, &opt);

    if (conn.control_client < 0) {
        std::cerr << "Failed to accept control connection!" << std::endl;
        cleanup_connection(conn);
        return conn;
    }

    /* Accept interrupt connection */
    sockaddr_l2 rem_addr_intr{};
    opt = sizeof(rem_addr_intr);
    conn.interrupt_client = accept(conn.interrupt_socket, (struct sockaddr *)&rem_addr_intr, &opt);

    if (conn.interrupt_client < 0) {
        std::cerr << "Failed to accept interrupt connection!" << std::endl;
        cleanup_connection(conn);
        return conn;
    }

    char ctrl_bdaddr[18] = { 0 };
    ba2str(&rem_addr_ctrl.l2_bdaddr, ctrl_bdaddr);

    std::cout << "4. Accepted control connection from " << ctrl_bdaddr << std::endl;

    char intr_bdaddr[18] = { 0 };
    ba2str(&rem_addr_intr.l2_bdaddr, intr_bdaddr);

    std::cout << "5. Accepted interrupt connection from " << intr_bdaddr << std::endl;

    std::cout << "6. L2CAP HID channels are ready!" << std::endl;

    return conn;
}

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

int convert(const std::string& evdev_keycode) {
    if (keytable.find(evdev_keycode) != keytable.end()) {
        return keytable[evdev_keycode];
    } else {
        std::cerr << "Key not found: " << evdev_keycode << std::endl;
        return -1; 
    }
}

int modkey(const std::string& evdev_keycode) {
    if (modkeys.find(evdev_keycode) != modkeys.end()) {
        return modkeys[evdev_keycode];
    } else {
        return -1;
    }
}

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
        return false;
    } else {
        return true;
    }
}

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

/*
 * If use normal input, program will wait user type input
 * so program can't do anything else while waiting for import.
 * 
 * Recommend use non_blocking_input, program will not wait user type input
 * 1. when user type input.
 *      processing data input
 * 2. when user dont type input.
 *      checking bluetooth connection.
 */

void non_blocking_input(BluetoothConnection &bt_conn){

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════╗" << std::endl;
    std::cout << "║        HID Report Sender         ║" << std::endl;
    std::cout << "╠══════════════════════════════════╣" << std::endl;
    std::cout << "║  [m] Send mouse input            ║" << std::endl;
    std::cout << "║  [Type] Send keyboard input      ║" << std::endl;
    std::cout << "║  [q] Quit program                ║" << std::endl;
    std::cout << "╚══════════════════════════════════╝" << std::endl;
    std::cout << "Input >>> ";
    std::cout << "\n";

    std::string input;

    bool running = true;
    
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;  // 500ms

        int retval = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);

        if (retval == -1) {
            perror("select()");
            break;
        } else if (retval) { 
            /* Have data into input */
            std::getline(std::cin, input);

            if (input == "q") {

                std::cout << "Quit program!" << std::endl;

                cleanup_connection(bt_conn); /* Clean socket & client */
    
                if (loop) 
                    g_main_loop_unref(loop);
                if (proxy) 
                    g_object_unref(proxy);
                if (conn) 
                    g_object_unref(conn);

                exit(0); /* Exit program */

            } else if (input == "m") {

                std::cout << "Send mouse" << std::endl;
                std::array<int8_t, 3> mouse_move = {10, 30, 1};

                if (!send_mouse(bt_conn, 0, mouse_move)) {
                    std::cerr << "Failed to send mouse report!" << std::endl;
                }

            } else {
                std::cout << "Send messages" << std::endl;

                send_string_input(bt_conn, input);
            }
        } else {
            /* No data input */
            if (!is_connected(bt_conn.control_client) && !is_connected(bt_conn.interrupt_client)) {

                std::cout << "Device disconnected!" << std::endl;
                
                cleanup_connection(bt_conn); 
                
                running = false;
            }
        }
    }
}

static void bluez_agent_method_call (GDBusConnection *con,
                                    const gchar *sender,
                                    const gchar *path,
                                    const gchar *interface,
                                    const gchar *method,
                                    GVariant *params,
                                    GDBusMethodInvocation *invocation,
                                    void *user_data)
    {
        guint32 passkey = 0;
        const gchar *device_path = NULL;

        g_print("[Agent] Agent method call: %s.%s()\n", interface, method);

        if (!strcmp(method, "RequestConfirmation")) {   

            g_variant_get(params, "(&ou)", &device_path, &passkey); /* Get device & passkey */
        
            g_print("[Agent] Confirm passkey %06d for device %s\n", passkey, device_path);

            g_dbus_method_invocation_return_value(invocation, NULL);
        
        }
        else if (!strcmp(method, "AuthorizeService")) {
            const gchar *device;
            const gchar *uuid;

            g_variant_get(params, "(&os)", &device, &uuid);
            g_print("[Agent] Authorize service %s for device %s -> Auto accepting!\n", uuid, device);

            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        else if (!strcmp(method, "RequestAuthorization")) {
            const gchar *device;
            g_variant_get(params, "(&o)", &device);
    
            g_print("[Agent] RequestAuthorization for device %s -> Auto accepting!\n", device);
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        else if (!strcmp(method, "Release")) {
            g_print("[Agent] Release agent\n");
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        else if (!strcmp(method, "Cancel")) {
            g_print("[Agent] Request canceled\n");
            g_dbus_method_invocation_return_value(invocation, NULL);
        }
        else {
            g_print("[Agent] Unhandled method: %s\n", method);
            g_dbus_method_invocation_return_dbus_error(invocation, "org.bluez.Error.Rejected", "Method not implemented");
        }
    }
  
void auto_paring_agent() {
    /* Step 1. Define agent path */
    const gchar *agent_path = "/elink/agent";

    /* Step 2. Define agent interface */
    const gchar *introspection_xml =
    "<node>"
    "  <interface name='org.bluez.Agent1'>"
    "    <method name='Release'/>"
    "    <method name='RequestPinCode'>"
    "      <arg type='o' direction='in'/>"
    "      <arg type='s' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPinCode'>"
    "      <arg type='o' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "    </method>"
    "    <method name='RequestPasskey'>"
    "      <arg type='o' direction='in'/>"
    "      <arg type='u' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPasskey'>"
    "      <arg type='o' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "      <arg type='q' direction='in'/>"
    "    </method>"
    "    <method name='RequestConfirmation'>"
    "      <arg type='o' direction='in'/>"
    "      <arg type='u' direction='in'/>"
    "    </method>"
    "    <method name='RequestAuthorization'>"
    "      <arg type='o' direction='in'/>"
    "    </method>"
    "    <method name='AuthorizeService'>"
    "      <arg type='o' direction='in'/>"
    "      <arg type='s' direction='in'/>"
    "    </method>"
    "    <method name='Cancel'/>"
    "  </interface>"
    "</node>";

    GDBusNodeInfo *introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
    if (!introspection_data) {
        std::cerr << "Unable to parse introspection XML: " << error->message << std::endl;
        g_error_free(error);
        return;
    }

    static GDBusInterfaceVTable agent_vtable = {
        .method_call = bluez_agent_method_call,
        .get_property = NULL,
        .set_property = NULL
    };
    /* Step 3. Register object */
    guint reg_id = g_dbus_connection_register_object(
                                                        conn,
                                                        agent_path,
                                                        introspection_data->interfaces[0],
                                                        &agent_vtable,
                                                        NULL, 
                                                        NULL, 
                                                        &error
                                                    );

    if (!reg_id) {
        std::cerr << "Failed to register agent object: " << error->message << std::endl;
        g_error_free(error);
        g_dbus_node_info_unref(introspection_data);
        return;
    }

    /* Step 4. Create proxy to AgentManager1 for register agent path */
    GDBusProxy *agent_mgr = g_dbus_proxy_new_sync(
                                                    conn,
                                                    G_DBUS_PROXY_FLAGS_NONE,
                                                    NULL,
                                                    "org.bluez",
                                                    "/org/bluez",
                                                    "org.bluez.AgentManager1",
                                                    NULL,
                                                    &error
                                                );
    
    if (!agent_mgr) {
        std::cerr << "Failed to create AgentManager1 proxy: " << error->message << std::endl;
        g_error_free(error);
        return;
    }

    /* Step 5. Register Agent with Capability = KeyboardDisplay */
    /* If use "NoInputOutput".
     * that is the reason which make "Connected: yes" -> "Connected: no" immediately
     * and make error incorrect pin or password on device which send connection request.
    */
    GVariant *res = g_dbus_proxy_call_sync(
                                                agent_mgr,
                                                "RegisterAgent",
                                                g_variant_new("(os)", agent_path, "KeyboardDisplay"),  
                                                G_DBUS_CALL_FLAGS_NONE,
                                                -1,
                                                NULL,
                                                &error
                                            );

    if (error) {
        std::cerr << "Failed to register agent: " << error->message << std::endl;
        g_error_free(error);
    } else {
        g_variant_unref(res);
        std::cout << "Agent registered successfully!" << std::endl;

        /* Step 6. Set default agent */
        res = g_dbus_proxy_call_sync(
                                        agent_mgr,
                                        "RequestDefaultAgent",
                                        g_variant_new("(o)", agent_path),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        NULL,
                                        &error
                                    );

        if (error) {
            std::cerr << "Failed to set default agent: " << error->message << std::endl;
            g_error_free(error);
        } else {
            g_variant_unref(res);
            std::cout << "Agent set as default successfully!" << std::endl;
        }
    }

    g_object_unref(agent_mgr);
    g_dbus_node_info_unref(introspection_data);
}    


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

    /* Optional */                                        
    g_variant_builder_add(&options_builder, "{sv}",
                                            "Trusted",
                                            g_variant_new_boolean(true));     

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

void init_server(){
    std::cout << "Starting HID Profile Server..." << std::endl;

    /* Step 1: Connect to system bus */
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        std::cerr << "Failed to connect to D-Bus: " << error->message << std::endl;
        g_error_free(error);
        return;
    }

    /* Step 2: Start auto pairing agent */
    auto_paring_agent();

    /* Step 3: Create proxy for ProfileManager1 */
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
        g_main_loop_unref(loop);
        return;
    }

    /* Step 4: Init the bluetooth device */
    init_bt_device();

    /* Step 5: Register the HID profile */
    init_bluez_profile(proxy);

    /* Step 6: Start listen connection */

    /* Use Thread for BluetoothConnection listen_for_connection
     * avoid blocking between g_main_loop_run(loop) & bt_server_thread
    */
    std::thread bt_server_thread([](){
        BluetoothConnection bt_conn = {0};

        while (true) {
            cleanup_connection(bt_conn); 
            bt_conn = listen_for_connections(); 

            if (is_connected(bt_conn.control_client) && is_connected(bt_conn.interrupt_client)) {
                non_blocking_input(bt_conn);
            }
        }
    });

    bt_server_thread.detach(); /* Running parallel with main thread */ 

    /* Start the main loop */
    g_main_loop_run(loop);

    /* Cleanup */
    if (loop) 
        g_main_loop_unref(loop);
    if (proxy) 
        g_object_unref(proxy);
    if (conn) 
        g_object_unref(conn);

}

int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        std::cerr << "Only root can run this script" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    std::cout << "Restarting bluetooth service" << std::endl;
    system("service bluetooth stop");
    system("/usr/libexec/bluetooth/bluetoothd -p time&");
    system("hciconfig hci0 up");

    init_server();
    return 0;
}