#include <cstdio>
#include <string>
#include <gio/gio.h>
#include <glib.h>

#include <iostream>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/hci.h>
#include <sys/socket.h>
#include <unistd.h>

#include <fcntl.h>

#define BT_ADDRESS "48:8F:4C:FF:1E:6A" 
#define HID_PROFILE_UUID "00001124-0000-1000-8000-00805f9b34fb"
#define SDP_RECORD_PATH "/etc/bluetooth/sdp_record.xml"
#define P_CTRL 17
#define P_INTR 19

// char* read_sdp_service_record() {
//     FILE *file = fopen(SDP_RECORD_PATH, "r");
//     if (!file) {
//         perror("Could not open SDP record file");
//         exit(EXIT_FAILURE);
//     }

//     fseek(file, 0, SEEK_END);
//     long file_size = ftell(file);
//     rewind(file);

//     char *buffer = new char[file_size + 1];
//     if (!buffer) {
//         perror("Memory allocation failed");
//         fclose(file);
//         exit(EXIT_FAILURE);
//     }

//     fread(buffer, 1, file_size, file);
//     buffer[file_size] = '\0';
//     fclose(file);
//     return buffer;
// }

void restart_bluetooth_service(){
    printf("Setting up Bluetooth device\n");
	system("/etc/init.d/bluetooth stop");
	system("/usr/libexec/bluetooth/bluetoothd -p time&");
    printf("Starting bluetooth service\n");

    printf("Configuring\n");
	system("hciconfig hci0 up");
    system("hciconfig hci0 class 0x05C0");
    system("hciconfig hci0 piscan"); /* Discoverable on */
}
// void bdaddr_to_str(const bdaddr_t *ba, char *str) {
//     sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
//             ba->b[5], ba->b[4], ba->b[3], ba->b[2], ba->b[1], ba->b[0]);
// }

// void listening_for_newconnection( ){
//     int scontrol, sinterrupt, ccontrol;
//     struct sockaddr_l2 addr_control = { 0 }, addr_interrupt = { 0 }, cinfo_control = { 0 };
//     socklen_t cinfo_size = sizeof(cinfo_control);

//     bdaddr_t any_addr = {{0, 0, 0, 0, 0, 0}};

//     std::cout << "Initializing Bluetooth HID Server..." << std::endl;

//     scontrol = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
//     if (scontrol < 0) {
//         std::cerr << "Error: Could not create control socket\n";
//         return;
//     }

//     sinterrupt = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
//     if (sinterrupt < 0) {
//         std::cerr << "Error: Could not create interrupt socket\n";
//         close(scontrol);
//         return;
//     }

//     addr_control.l2_family = AF_BLUETOOTH;
//     bacpy(&addr_control.l2_bdaddr, &any_addr);  // Bind to any address
//     addr_control.l2_psm = htobs(P_CTRL);
    
//     if (bind(scontrol, (struct sockaddr *)&addr_control, sizeof(addr_control)) < 0) {
//         std::cerr << "Error: Failed to bind control socket\n";
//         close(scontrol);
//         close(sinterrupt);
//         return;
//     } else {
//         std::cout << "Control socket bound successfully" << std::endl;
//     }

//     addr_interrupt.l2_family = AF_BLUETOOTH;
//     bacpy(&addr_interrupt.l2_bdaddr, &any_addr);
//     addr_interrupt.l2_psm = htobs(P_INTR);

//     if (bind(sinterrupt, (struct sockaddr *)&addr_interrupt, sizeof(addr_interrupt)) < 0) {
//         std::cerr << "Error: Failed to bind interrupt socket\n";
//         close(scontrol);
//         close(sinterrupt);
//         return;
//     } else {
//         std::cout << "Interrupt socket bound successfully" << std::endl;
//     }

//     if (listen(scontrol, 1) < 0 || listen(sinterrupt, 1) < 0) {
//         std::cerr << "Error: Failed to listen on sockets\n";
//         close(scontrol);
//         close(sinterrupt);
//         return;
//     }
//     std::cout << "Listening for incoming connections..." << std::endl;

//     ccontrol = accept(scontrol, (struct sockaddr *)&cinfo_control, &cinfo_size);
//     if (ccontrol < 0) {
//         std::cerr << "Error: accept() failed: " << strerror(errno) << std::endl;
//         close(scontrol);
//         close(sinterrupt);
//         return;
//     }
    
//     char client_addr[18] = {0};
//     bdaddr_to_str(&cinfo_control.l2_bdaddr, client_addr);
//     std::cout << "Connected to client: " << client_addr << std::endl;

//     char hid_report[] = {0xA1, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
//     send(ccontrol, hid_report, sizeof(hid_report), 0);
//     std::cout << "Sent HID Report" << std::endl;

//     close(ccontrol);
//     close(scontrol);
//     close(sinterrupt);
// }
int register_profile(GDBusProxy *proxy){
    GVariant *profile;
    GVariantBuilder profile_builder;
    GError *error = NULL;

    restart_bluetooth_service(); /* Start service bluetooth first */

    printf("Registering HID Profile...\n");

    /*---------------------------Modify----------------------------------*/
    // char *sdp_service_record = read_sdp_service_record();

    // if (sdp_service_record == NULL || strlen(sdp_service_record) == 0) {
    //     printf("Error: SDP Service Record is empty or NULL!\n");
    //     return 1;
    // }

    g_variant_builder_init(&profile_builder, G_VARIANT_TYPE("(osa{sv})"));
    g_variant_builder_add(&profile_builder, "o", "/org/bluez/bluetoothhidprofile");
    g_variant_builder_add(&profile_builder, "s", HID_PROFILE_UUID);
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("a{sv}"));

    // g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    // g_variant_builder_add(&profile_builder, "s", "ServiceRecord");
    // g_variant_builder_add(&profile_builder, "v", g_variant_new_string(sdp_service_record));
    // g_variant_builder_close(&profile_builder);

    // HID Service UUID
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    g_variant_builder_add(&profile_builder, "s", "Service");
    g_variant_builder_add(&profile_builder, "v", g_variant_new_string(HID_PROFILE_UUID));
    g_variant_builder_close(&profile_builder);

    // Name of the HID profile
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    g_variant_builder_add(&profile_builder, "s", "Name");
    g_variant_builder_add(&profile_builder, "v", g_variant_new_string("Bluetooth HID Device"));
    g_variant_builder_close(&profile_builder);

    // Role (server)
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    g_variant_builder_add(&profile_builder, "s", "Role");
    g_variant_builder_add(&profile_builder, "v", g_variant_new_string("server"));
    g_variant_builder_close(&profile_builder);

    // Require Authentication (HID devices usually require this)
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    g_variant_builder_add(&profile_builder, "s", "RequireAuthentication");
    g_variant_builder_add(&profile_builder, "v", g_variant_new_boolean(false));
    g_variant_builder_close(&profile_builder);

    // Require Authorization
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    g_variant_builder_add(&profile_builder, "s", "RequireAuthorization");
    g_variant_builder_add(&profile_builder, "v", g_variant_new_boolean(false));
    g_variant_builder_close(&profile_builder);

    // AutoConnect
    g_variant_builder_open(&profile_builder, G_VARIANT_TYPE("{sv}"));
    g_variant_builder_add(&profile_builder, "s", "AutoConnect");
    g_variant_builder_add(&profile_builder, "v", g_variant_new_boolean(true));
    g_variant_builder_close(&profile_builder);

    g_variant_builder_close(&profile_builder);
    profile = g_variant_builder_end(&profile_builder);

    GVariant *ret = g_dbus_proxy_call_sync(proxy,
                                           "RegisterProfile",
                                           profile,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           &error);
                                           
                                           
    if (error) {
        printf("Failed to register HID Profile: %s\n", error->message);
        g_error_free(error);
        return 1;
    }
    printf("HID Profile registered successfully!\n");
    return 0;
}

void init_server() {
    GDBusProxy *proxy;
    GDBusConnection *conn;
    GError *error = NULL;
    GMainLoop *loop = g_main_loop_new(NULL, false);

    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        printf("Failed to connect to D-Bus: %s\n", error->message);
        g_error_free(error);
        return;
    }

    proxy = g_dbus_proxy_new_sync(conn,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  "org.bluez",
                                  "/org/bluez",
                                  "org.bluez.ProfileManager1",
                                  NULL,
                                  &error);

    if (error) {
        printf("Failed to create D-Bus proxy: %s\n", error->message);
        g_error_free(error);
        g_object_unref(conn);
        return;
    }
    if (register_profile(proxy)) {
        printf("Profile registration failed.\n");
        g_object_unref(proxy);
        g_object_unref(conn);
        return;
    }
    g_main_loop_run(loop);
    g_object_unref(proxy);
    g_object_unref(conn);
}

int main(int argc, const char **argv) {
    init_server();
    return 0;
}