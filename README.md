# 🔋 Cool Little Battery Monitor

A C-based system tray battery monitor that helps you to take care of your laptop battery.

## ✨ Features

- **System Tray Integration**: Clean system tray icon with battery status
- **Aggressive Battery Protection**: Impossible-to-ignore alerts when battery gets low
- **Forced Suspend**: Automatically suspends your system at critical battery levels to prevent data loss
- **Multiple Suspend Methods**: Supports systemctl, pm-suspend, D-Bus, and direct kernel interface
- **Configurable Thresholds**: Customize warning and critical battery levels
- **Desktop Notifications**: Beautiful libnotify notifications with urgency levels
- **Pop!_OS Optimized**: Designed specifically for Pop!_OS but works on any Linux system

## 🚨 Protection Levels

### Warning Level (Default: 20%)
- Shows desktop notifications every 2 minutes
- Displays "impossible to dismiss" alert dialogs
- Updates system tray icon to warning state

### Critical Level (Default: 10%)
- **IMMEDIATE ACTION REQUIRED**
- Shows critical notifications and alert dialogs
- Gives you 10 seconds to plug in charger
- **FORCES SYSTEM SUSPEND** if still critical after 10 seconds

## 🛠️ Dependencies

```bash
# Ubuntu/Debian/Pop!_OS
sudo apt install libgtk-3-dev libnotify-dev build-essential

# Fedora
sudo dnf install gtk3-devel libnotify-devel gcc

# Arch Linux
sudo pacman -S gtk3 libnotify gcc
```

## 🚀 Installation

```bash
# Compile
gcc -o battery_monitor battery_monitor.c `pkg-config --cflags --libs gtk+-3.0 libnotify`

# Make executable
chmod +x battery_monitor

# Run
./battery_monitor
```

## 🎯 Usage

### Basic Usage
```bash
# Run the battery monitor
./battery_monitor

# Run in background
./battery_monitor &
```

### System Tray Interaction
- **Right-click** the battery icon for menu options
- **Left-click** to view current battery status
- Menu options include:
  - 🔋 Battery status and configuration info
  - ⚙️ Settings (configure thresholds and behavior)
  - 💤 Suspend method selection
  - 🧪 Test suspend functionality
  - ❌ Quit application

## ⚙️ Configuration

Configuration file: `~/.config/cool-little-battery-monitor.conf`

```ini
# Warning level percentage (when to show alerts)
warning_level=20

# Critical level percentage (when to force suspend)
critical_level=10

# Check interval in seconds
check_interval=30

# Force suspend at critical level (1=yes, 0=no)
force_suspend=1

# Show impossible to dismiss alerts (1=yes, 0=no)
impossible_alerts=1

# Suspend method (0=systemctl, 1=pm-suspend, 2=dbus, 3=kernel)
suspend_method=0
```

### Suspend Methods
- **0**: `systemctl suspend` (Systemd - recommended)
- **1**: `pm-suspend` (PM Utils - legacy)
- **2**: D-Bus Login Manager
- **3**: Direct kernel interface

## 🐛 Troubleshooting

### Battery Not Detected
```bash
# Check if battery files exist
ls /sys/class/power_supply/BAT*

# Check battery status manually
cat /sys/class/power_supply/BAT0/capacity
```

### Run as a Service (auto on login)

```bash
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
mkdir -p "$CONFIG_DIR"

cat > "$CONFIG_DIR/battery-monitor.service" << 'EOF'
[Unit]
Description=Battery Monitor Service
After=graphical-session.target

[Service]
Type=simple
ExecStart=%h/battery_monitor
Restart=on-failure
RestartSec=5
Environment=DISPLAY=:0

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable battery-monitor.service
systemctl --user start battery-monitor.service
```

### System Tray Not Showing
- Ensure you have a system tray in your desktop environment
- Check if GTK3 is properly installed

### Suspend Not Working
- Test different suspend methods in the settings menu
- Check system permissions for suspend commands

## 🔒 Security Notes

- The program requires permission to execute suspend commands
- Critical battery suspend is designed to prevent data loss
- No network access or external dependencies beyond system libraries

## 💡 Why This Exists

Too many laptops die from neglected batteries! This monitor ensures you'll never accidentally drain your battery to dangerous levels. Perfect for developers who get lost in code!

---

**Remember**: A protected battery is a happy battery! 🔋💕
