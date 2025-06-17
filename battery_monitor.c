/*
 * 🔋 Cool Little Battery Monitor - Hardcore C Edition 🔋
 * A system tray battery monitor that FORCES you to take care of your battery!
 * 
 * Features:
 * - System tray icon
 * - Impossible to ignore alerts at 20%
 * - Forced suspend at 10% 
 * - Configuration file support
 * - Pop!_OS optimized
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <libnotify/notify.h>
// Configuration structure
typedef struct {
    int warning_level;          // Warning percentage (default 20%)
    int critical_level;         // Critical/suspend percentage (default 10%)
    int check_interval;         // Check interval in seconds (default 30)
    int alert_timeout;          // Alert timeout in seconds (default 30)
    char config_path[512];      // Configuration file path
    char icon_charging[256];    // Charging icon path
    char icon_battery[256];     // Battery icon path
    char icon_low[256];         // Low battery icon path
    int force_suspend;          // Force suspend at critical level (1 = yes, 0 = no)
    int impossible_alerts;      // Show impossible to dismiss alerts (1 = yes, 0 = no)
    int suspend_method;         // Suspend method (0=systemctl, 1=pm-suspend, 2=dbus, 3=kernel)
} BatteryConfig;

// Global variables
static BatteryConfig config;
static GtkStatusIcon *tray_icon;
static GtkWidget *menu;
static guint timer_id;
static int last_percentage = -1;
static int last_charging_state = -1;
static time_t last_alert_time = 0;
static int alert_active = 0;
static GtkWidget *alert_dialog = NULL;

// Battery status structure
typedef struct {
    int percentage;
    int charging;
    int present;
    char status[32];
    int time_remaining;
} BatteryStatus;

// Function prototypes
static void load_config(void);
static void save_config(void);
static BatteryStatus get_battery_status(void);
static void update_tray_icon(BatteryStatus status);
static void show_notification(const char *title, const char *message, const char *urgency);
static void show_impossible_alert(const char *title, const char *message);
static void force_system_suspend(void);
static gboolean check_battery_timer(gpointer data);
static void create_menu(void);
static void on_quit_clicked(GtkMenuItem *item, gpointer data);
static void on_settings_clicked(GtkMenuItem *item, gpointer data);
static void on_status_clicked(GtkMenuItem *item, gpointer data);
static void on_suspend_methods_clicked(GtkMenuItem *item, gpointer data);
static void on_test_suspend_clicked(GtkMenuItem *item, gpointer data);
static gboolean test_suspend_callback(gpointer data);
static void on_tray_popup(GtkStatusIcon *status_icon, guint button, guint32 activate_time, gpointer user_data);
static void setup_signal_handlers(void);
static void signal_handler(int sig);

// Default configuration
static void init_default_config(void) {
    config.warning_level = 20;
    config.critical_level = 10;
    config.check_interval = 30;
    config.alert_timeout = 30;
    config.force_suspend = 1;
    config.impossible_alerts = 1;
    config.suspend_method = 0;  // Default to systemctl
    
    // Set default icon paths
    strcpy(config.icon_charging, "battery-caution-charging");
    strcpy(config.icon_battery, "battery-good");
    strcpy(config.icon_low, "battery-caution");
    
    // Set config file path
    char *home = getenv("HOME");
    if (home) {
        snprintf(config.config_path, sizeof(config.config_path), 
                "%s/.config/cool-little-battery-monitor.conf", home);
    } else {
        strcpy(config.config_path, "/tmp/cool-little-battery-monitor.conf");
    }
}

// Load configuration from file
static void load_config(void) {
    FILE *file = fopen(config.config_path, "r");
    if (!file) {
        printf("🔋 No config file found, using defaults\n");
        return;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\0') continue;
        
        char key[256], value[256];
        if (sscanf(line, "%255[^=]=%255s", key, value) == 2) {
            if (strcmp(key, "warning_level") == 0) {
                config.warning_level = atoi(value);
            } else if (strcmp(key, "critical_level") == 0) {
                config.critical_level = atoi(value);
            } else if (strcmp(key, "check_interval") == 0) {
                config.check_interval = atoi(value);
            } else if (strcmp(key, "alert_timeout") == 0) {
                config.alert_timeout = atoi(value);
            } else if (strcmp(key, "force_suspend") == 0) {
                config.force_suspend = atoi(value);
            } else if (strcmp(key, "impossible_alerts") == 0) {
                config.impossible_alerts = atoi(value);
            } else if (strcmp(key, "suspend_method") == 0) {
                config.suspend_method = atoi(value);
            } else if (strcmp(key, "icon_charging") == 0) {
                strcpy(config.icon_charging, value);
            } else if (strcmp(key, "icon_battery") == 0) {
                strcpy(config.icon_battery, value);
            } else if (strcmp(key, "icon_low") == 0) {
                strcpy(config.icon_low, value);
            }
        }
    }
    
    fclose(file);
    printf("🔋 Configuration loaded from %s\n", config.config_path);
}

// Save configuration to file
static void save_config(void) {
    // Create config directory if it doesn't exist
    char *config_dir = strdup(config.config_path);
    char *last_slash = strrchr(config_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(config_dir, 0755);
    }
    free(config_dir);
    
    FILE *file = fopen(config.config_path, "w");
    if (!file) {
        printf("❌ Failed to save config to %s: %s\n", config.config_path, strerror(errno));
        return;
    }
    
    fprintf(file, "# 🔋 Cool Little Battery Monitor Configuration\n");
    fprintf(file, "# Warning level percentage (when to show alerts)\n");
    fprintf(file, "warning_level=%d\n", config.warning_level);
    fprintf(file, "# Critical level percentage (when to force suspend)\n");
    fprintf(file, "critical_level=%d\n", config.critical_level);
    fprintf(file, "# Check interval in seconds\n");
    fprintf(file, "check_interval=%d\n", config.check_interval);
    fprintf(file, "# Alert timeout in seconds\n");
    fprintf(file, "alert_timeout=%d\n", config.alert_timeout);
    fprintf(file, "# Force suspend at critical level (1=yes, 0=no)\n");
    fprintf(file, "force_suspend=%d\n", config.force_suspend);
    fprintf(file, "# Show impossible to dismiss alerts (1=yes, 0=no)\n");
    fprintf(file, "impossible_alerts=%d\n", config.impossible_alerts);
    fprintf(file, "# Suspend method (0=systemctl, 1=pm-suspend, 2=dbus, 3=kernel)\n");
    fprintf(file, "suspend_method=%d\n", config.suspend_method);
    fprintf(file, "# Icon paths\n");
    fprintf(file, "icon_charging=%s\n", config.icon_charging);
    fprintf(file, "icon_battery=%s\n", config.icon_battery);
    fprintf(file, "icon_low=%s\n", config.icon_low);
    
    fclose(file);
    printf("🔋 Configuration saved to %s\n", config.config_path);
}

// Get current battery status
static BatteryStatus get_battery_status(void) {
    BatteryStatus status = {0};
    
    // Try to read from /sys/class/power_supply/BAT0 or BAT1
    const char *battery_paths[] = {
        "/sys/class/power_supply/BAT0",
        "/sys/class/power_supply/BAT1",
        NULL
    };
    
    for (int i = 0; battery_paths[i]; i++) {
        char path[512];
        FILE *file;
        
        // Check if battery is present
        snprintf(path, sizeof(path), "%s/present", battery_paths[i]);
        file = fopen(path, "r");
        if (!file) continue;
        
        int present;
        if (fscanf(file, "%d", &present) == 1 && present) {
            status.present = 1;
            fclose(file);
            
            // Read capacity
            snprintf(path, sizeof(path), "%s/capacity", battery_paths[i]);
            file = fopen(path, "r");
            if (file) {
                fscanf(file, "%d", &status.percentage);
                fclose(file);
            }
            
            // Read status
            snprintf(path, sizeof(path), "%s/status", battery_paths[i]);
            file = fopen(path, "r");
            if (file) {
                fgets(status.status, sizeof(status.status), file);
                // Remove newline
                status.status[strcspn(status.status, "\n")] = 0;
                status.charging = (strcmp(status.status, "Charging") == 0);
                fclose(file);
            }
            
            break;
        } else {
            fclose(file);
        }
    }
    
    return status;
}

// Update the system tray icon
static void update_tray_icon(BatteryStatus status) {
    if (!tray_icon) return;
    
    char tooltip[256];
    const char *icon;
    
    if (!status.present) {
        icon = "battery-missing";
        strcpy(tooltip, "🔋 No battery detected");
    } else if (status.charging) {
        icon = config.icon_charging;
        snprintf(tooltip, sizeof(tooltip), "🔌 Charging: %d%%", status.percentage);
    } else if (status.percentage <= config.critical_level) {
        icon = config.icon_low;
        snprintf(tooltip, sizeof(tooltip), "🚨 CRITICAL: %d%% - GET A CHARGER NOW!", status.percentage);
    } else if (status.percentage <= config.warning_level) {
        icon = config.icon_low;
        snprintf(tooltip, sizeof(tooltip), "⚠️ Low: %d%% - Consider charging", status.percentage);
    } else {
        icon = config.icon_battery;
        snprintf(tooltip, sizeof(tooltip), "🔋 Battery: %d%%", status.percentage);
    }
    
    gtk_status_icon_set_from_icon_name(tray_icon, icon);
    gtk_status_icon_set_tooltip_text(tray_icon, tooltip);
}

// Show desktop notification
static void show_notification(const char *title, const char *message, const char *urgency) {
    if (!notify_is_initted()) {
        if (!notify_init("Cool Little Battery Monitor")) {
            printf("❌ Failed to initialize libnotify\n");
            return;
        }
    }
    
    NotifyNotification *notification = notify_notification_new(title, message, "battery-caution");
    
    // Set urgency
    if (strcmp(urgency, "critical") == 0) {
        notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);
        notify_notification_set_timeout(notification, config.alert_timeout * 1000);
    } else {
        notify_notification_set_urgency(notification, NOTIFY_URGENCY_NORMAL);
        notify_notification_set_timeout(notification, 5000);
    }
    
    notify_notification_show(notification, NULL);
    g_object_unref(notification);
}

// Show impossible to dismiss alert dialog
static void show_impossible_alert(const char *title, const char *message) {
    if (!config.impossible_alerts) return;
    
    // Close any existing alert
    if (alert_dialog) {
        gtk_widget_destroy(alert_dialog);
        alert_dialog = NULL;
    }
    
    alert_dialog = gtk_message_dialog_new(NULL,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_WARNING,
                                        GTK_BUTTONS_OK,
                                        "%s", title);
    
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(alert_dialog), "%s", message);
    
    // Make it stay on top and grab focus
    gtk_window_set_keep_above(GTK_WINDOW(alert_dialog), TRUE);
    gtk_window_set_urgency_hint(GTK_WINDOW(alert_dialog), TRUE);
    gtk_window_set_position(GTK_WINDOW(alert_dialog), GTK_WIN_POS_CENTER_ALWAYS);
    
    // Show the dialog and wait for response
    gint result = gtk_dialog_run(GTK_DIALOG(alert_dialog));
    gtk_widget_destroy(alert_dialog);
    alert_dialog = NULL;
    
    alert_active = 1;
}

// Force system suspend
static void force_system_suspend(void) {
    printf("🚨 FORCING SYSTEM SUSPEND DUE TO CRITICAL BATTERY! 🚨\n");
    
    // Show final warning
    show_notification("🚨 SYSTEM SUSPENDING NOW! 🚨", 
                     "Battery critically low! Suspending to prevent data loss!", 
                     "critical");
    
    // Suspend using selected method
    const char *suspend_commands[] = {
        "systemctl suspend",           // 0: systemd
        "pm-suspend",                 // 1: pm-utils
        "dbus-send --system --print-reply --dest=org.freedesktop.login1 /org/freedesktop/login1 \"org.freedesktop.login1.Manager.Suspend\" boolean:true",  // 2: dbus
        "echo mem > /sys/power/state", // 3: direct kernel interface
    };
    
    if (config.suspend_method >= 0 && config.suspend_method < 4) {
        printf("🔋 Using suspend method: %s\n", suspend_commands[config.suspend_method]);
        if (system(suspend_commands[config.suspend_method]) != 0) {
            printf("❌ Primary suspend method failed, trying fallbacks...\n");
            // Try other methods as fallback
            for (int i = 0; i < 4; i++) {
                if (i != config.suspend_method) {
                    printf("🔋 Trying fallback: %s\n", suspend_commands[i]);
                    if (system(suspend_commands[i]) == 0) {
                        break;
                    }
                }
            }
        }
    }
}

// Battery check timer callback
static gboolean check_battery_timer(gpointer data) {
    BatteryStatus status = get_battery_status();
    
    if (!status.present) {
        update_tray_icon(status);
        return TRUE;  // Continue timer
    }
    
    time_t current_time = time(NULL);
    
    // Update icon always
    update_tray_icon(status);
    
    // Don't alert if charging
    if (status.charging) {
        alert_active = 0;
        if (alert_dialog) {
            gtk_widget_destroy(alert_dialog);
            alert_dialog = NULL;
        }
        last_percentage = status.percentage;
        last_charging_state = status.charging;
        return TRUE;
    }
    
    // Critical level - FORCE SUSPEND
    if (status.percentage <= config.critical_level) {
        if (current_time - last_alert_time > 30) {  // Don't spam suspend
            char title[256], message[512];
            snprintf(title, sizeof(title), "🚨 CRITICAL BATTERY: %d%% 🚨", status.percentage);
            snprintf(message, sizeof(message), 
                    "Your battery is critically low at %d%%!\n\n"
                    "🔌 PLUG IN YOUR CHARGER IMMEDIATELY!\n\n"
                    "System will suspend in 10 seconds to prevent data loss!",
                    status.percentage);
            
            show_notification(title, message, "critical");
            show_impossible_alert(title, message);
            
            if (config.force_suspend) {
                // Give user 10 seconds to plug in charger
                sleep(10);
                
                // Check again if still critical and not charging
                BatteryStatus final_check = get_battery_status();
                if (final_check.percentage <= config.critical_level && !final_check.charging) {
                    force_system_suspend();
                }
            }
            
            last_alert_time = current_time;
        }
    }
    // Warning level - IMPOSSIBLE TO IGNORE ALERTS
    else if (status.percentage <= config.warning_level) {
        if (current_time - last_alert_time > 120) {  // Alert every 2 minutes
            char title[256], message[512];
            snprintf(title, sizeof(title), "⚠️ LOW BATTERY: %d%% ⚠️", status.percentage);
            snprintf(message, sizeof(message), 
                    "Your battery is getting low at %d%%!\n\n"
                    "🔌 Please plug in your charger soon!\n\n"
                    "System will force suspend at %d%% to protect your data!",
                    status.percentage, config.critical_level);
            
            show_notification(title, message, "critical");
            show_impossible_alert(title, message);
            
            last_alert_time = current_time;
        }
    } else {
        // Battery level is good, clear any active alerts
        alert_active = 0;
        if (alert_dialog) {
            gtk_widget_destroy(alert_dialog);
            alert_dialog = NULL;
        }
    }
    
    last_percentage = status.percentage;
    last_charging_state = status.charging;
    
    return TRUE;  // Continue timer
}

// Create the system tray menu
static void create_menu(void) {
    menu = gtk_menu_new();
    
    // Battery status item
    GtkWidget *status_item = gtk_menu_item_new_with_label("🔋 Cool Little Battery Monitor");
    g_signal_connect(status_item, "activate", G_CALLBACK(on_status_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), status_item);
    
    // Separator
    GtkWidget *separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    

    
    // Settings
    GtkWidget *settings_item = gtk_menu_item_new_with_label("⚙️ Settings");
    g_signal_connect(settings_item, "activate", G_CALLBACK(on_settings_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings_item);
    
    // Suspend Methods submenu
    GtkWidget *suspend_item = gtk_menu_item_new_with_label("💤 Suspend Methods");
    g_signal_connect(suspend_item, "activate", G_CALLBACK(on_suspend_methods_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), suspend_item);
    
    // Test Suspend
    GtkWidget *test_suspend_item = gtk_menu_item_new_with_label("🧪 Test Suspend");
    g_signal_connect(test_suspend_item, "activate", G_CALLBACK(on_test_suspend_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), test_suspend_item);
    
    // Separator
    separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
    
    // Quit
    GtkWidget *quit_item = gtk_menu_item_new_with_label("❌ Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    
    gtk_widget_show_all(menu);
}

// Menu callbacks
static void on_quit_clicked(GtkMenuItem *item, gpointer data) {
    printf("🔋 Thanks for using Cool Little Battery Monitor! Stay charged! 💕\n");
    gtk_main_quit();
}

static void on_settings_clicked(GtkMenuItem *item, gpointer data) {
    // Simple settings dialog
    GtkWidget *dialog = gtk_dialog_new_with_buttons("🔋 Battery Monitor Settings",
                                                   NULL,
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   "Save", GTK_RESPONSE_ACCEPT,
                                                   NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(content), grid);
    
    // Warning level
    GtkWidget *warning_label = gtk_label_new("Warning Level (%):");
    GtkWidget *warning_spin = gtk_spin_button_new_with_range(5, 50, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(warning_spin), config.warning_level);
    gtk_grid_attach(GTK_GRID(grid), warning_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), warning_spin, 1, 0, 1, 1);
    
    // Critical level
    GtkWidget *critical_label = gtk_label_new("Critical Level (%):");
    GtkWidget *critical_spin = gtk_spin_button_new_with_range(1, 25, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(critical_spin), config.critical_level);
    gtk_grid_attach(GTK_GRID(grid), critical_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), critical_spin, 1, 1, 1, 1);
    
    // Check interval
    GtkWidget *interval_label = gtk_label_new("Check Interval (sec):");
    GtkWidget *interval_spin = gtk_spin_button_new_with_range(10, 300, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(interval_spin), config.check_interval);
    gtk_grid_attach(GTK_GRID(grid), interval_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), interval_spin, 1, 2, 1, 1);
    
    // Force suspend checkbox
    GtkWidget *suspend_check = gtk_check_button_new_with_label("Force suspend at critical level");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(suspend_check), config.force_suspend);
    gtk_grid_attach(GTK_GRID(grid), suspend_check, 0, 3, 2, 1);
    
    // Impossible alerts checkbox
    GtkWidget *alerts_check = gtk_check_button_new_with_label("Show impossible to dismiss alerts");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(alerts_check), config.impossible_alerts);
    gtk_grid_attach(GTK_GRID(grid), alerts_check, 0, 4, 2, 1);
    
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        config.warning_level = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(warning_spin));
        config.critical_level = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(critical_spin));
        config.check_interval = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(interval_spin));
        config.force_suspend = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(suspend_check));
        config.impossible_alerts = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(alerts_check));
        
        save_config();
        
        // Restart timer with new interval
        if (timer_id) {
            g_source_remove(timer_id);
        }
        timer_id = g_timeout_add(config.check_interval * 1000, check_battery_timer, NULL);
        
        show_notification("🔋 Settings Saved", "Battery monitor settings have been updated!", "normal");
    }
    
    gtk_widget_destroy(dialog);
}

static void on_status_clicked(GtkMenuItem *item, gpointer data) {
    BatteryStatus status = get_battery_status();
    char info[512];
    
    if (status.present) {
        const char *method_names[] = {
            "systemctl suspend",
            "pm-suspend",
            "D-Bus",
            "Kernel Direct"
        };
        
        snprintf(info, sizeof(info), 
                "🔋 Cool Little Battery Monitor\n\n"
                "Battery: %d%%\n"
                "Status: %s\n"
                "Warning Level: %d%%\n"
                "Critical Level: %d%%\n"
                "Force Suspend: %s\n"
                "Impossible Alerts: %s\n"
                "Suspend Method: %s",
                status.percentage,
                status.status,
                config.warning_level,
                config.critical_level,
                config.force_suspend ? "Enabled" : "Disabled",
                config.impossible_alerts ? "Enabled" : "Disabled",
                (config.suspend_method >= 0 && config.suspend_method < 4) ? 
                    method_names[config.suspend_method] : "Unknown");
    } else {
        strcpy(info, "🔋 Cool Little Battery Monitor\n\nNo battery detected!");
    }
    
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_INFO,
                                             GTK_BUTTONS_OK,
                                             "%s", info);
    
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_suspend_methods_clicked(GtkMenuItem *item, gpointer data) {
    const char *method_names[] = {
        "systemctl suspend (Systemd)",
        "pm-suspend (PM Utils)",
        "D-Bus (Login Manager)",
        "Kernel Direct (/sys/power/state)"
    };
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("💤 Suspend Method Selection",
                                                   NULL,
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   "Select", GTK_RESPONSE_ACCEPT,
                                                   NULL);
    
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(content), vbox);
    
    GtkWidget *label = gtk_label_new("Choose your preferred suspend method:");
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
    
    GSList *radio_group = NULL;
    GtkWidget *radio_buttons[4];
    
    for (int i = 0; i < 4; i++) {
        radio_buttons[i] = gtk_radio_button_new_with_label(radio_group, method_names[i]);
        radio_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(radio_buttons[i]));
        gtk_box_pack_start(GTK_BOX(vbox), radio_buttons[i], FALSE, FALSE, 0);
        
        if (i == config.suspend_method) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_buttons[i]), TRUE);
        }
    }
    
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        for (int i = 0; i < 4; i++) {
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radio_buttons[i]))) {
                config.suspend_method = i;
                save_config();
                show_notification("💤 Suspend Method Updated", 
                                method_names[i], 
                                "normal");
                break;
            }
        }
    }
    
    gtk_widget_destroy(dialog);
}

static void on_test_suspend_clicked(GtkMenuItem *item, gpointer data) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                             GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_QUESTION,
                                             GTK_BUTTONS_YES_NO,
                                             "🧪 Test Suspend\n\n"
                                             "This will test your selected suspend method.\n"
                                             "Your system will suspend immediately!\n\n"
                                             "Are you sure you want to proceed?");
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        gtk_widget_destroy(dialog);
        
        show_notification("🧪 Testing Suspend", 
                         "System will suspend in 3 seconds...", 
                         "normal");
        
        // Give user time to see the notification
        g_timeout_add(3000, (GSourceFunc)test_suspend_callback, NULL);
    } else {
        gtk_widget_destroy(dialog);
    }
}

static gboolean test_suspend_callback(gpointer data) {
    const char *suspend_commands[] = {
        "systemctl suspend",           // 0: systemd
        "pm-suspend",                 // 1: pm-utils
        "dbus-send --system --print-reply --dest=org.freedesktop.login1 /org/freedesktop/login1 \"org.freedesktop.login1.Manager.Suspend\" boolean:true",  // 2: dbus
        "echo mem > /sys/power/state", // 3: direct kernel interface
    };
    
    if (config.suspend_method >= 0 && config.suspend_method < 4) {
        printf("🧪 Testing suspend method: %s\n", suspend_commands[config.suspend_method]);
        system(suspend_commands[config.suspend_method]);
    }
    
    return FALSE; // Don't repeat
}

static void on_tray_popup(GtkStatusIcon *status_icon, guint button, guint32 activate_time, gpointer user_data) {
    if (button == 3) {  // Right click
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL, gtk_status_icon_position_menu, status_icon, button, activate_time);
    }
}

// Signal handlers
static void setup_signal_handlers(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

static void signal_handler(int sig) {
    printf("\n🔋 Received signal %d, shutting down gracefully...\n", sig);
    gtk_main_quit();
}

// Main function
int main(int argc, char *argv[]) {
    printf("🔋 Cool Little Battery Monitor Starting...\n");
    printf("   Made with love for Pop!_OS users who want REAL battery protection! 💕\n");
    
    // Initialize GTK
    gtk_init(&argc, &argv);
    
    // Initialize default config
    init_default_config();
    
    // Load configuration
    load_config();
    
    // Check if battery exists
    BatteryStatus initial_status = get_battery_status();
    if (!initial_status.present) {
        printf("❌ No battery detected! This monitor is for laptops with batteries.\n");
        printf("   If you're on a desktop, you don't need this awesome protection! 🖥️\n");
        return 1;
    }
    
    // Initialize libnotify
    if (!notify_init("Cool Little Battery Monitor")) {
        printf("❌ Failed to initialize libnotify\n");
        return 1;
    }
    
    // Create system tray icon
    tray_icon = gtk_status_icon_new_from_icon_name(config.icon_battery);
    
    if (!tray_icon) {
        printf("❌ Failed to create system tray icon\n");
        return 1;
    }
    
    gtk_status_icon_set_visible(tray_icon, TRUE);
    gtk_status_icon_set_title(tray_icon, "🔋 Cool Little Battery Monitor");
    
    // Create menu
    create_menu();
    
    // Connect signals
    g_signal_connect(G_OBJECT(tray_icon), "activate", G_CALLBACK(on_status_clicked), NULL);
    g_signal_connect(G_OBJECT(tray_icon), "popup-menu", G_CALLBACK(on_tray_popup), NULL);
    
    // Setup signal handlers
    setup_signal_handlers();
    
    // Start battery monitoring timer
    timer_id = g_timeout_add(config.check_interval * 1000, check_battery_timer, NULL);
    
    // Initial check
    check_battery_timer(NULL);
    
    printf("🔋 System tray battery monitor active! Right-click the tray icon for options.\n");
    printf("   Your battery is now under cool little protection! 🛡️\n");
    
    // Start GTK main loop
    gtk_main();
    
    // Cleanup
    if (timer_id) {
        g_source_remove(timer_id);
    }
    
    notify_uninit();
    save_config();
    
    printf("🔋 Cool Little Battery Monitor stopped. Stay safe! 💕\n");
    return 0;
} 
