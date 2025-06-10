import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from collections import deque
import re

# --- CONFIGURATION ---
SERIAL_PORT = 'COM13'  # IMPORTANT: Set this to your ESP32's COM port
BAUD_RATE = 115200
MAX_DATA_POINTS = 100  # How many data points to show on the x-axis

# --- REGEX TO PARSE THE SERIAL LINE ---
# This pattern is built to find and capture the numbers from your specific output format.
# Example: "XYZ: -0.12,0.67,10.12 | Raw Red/IR: 5406,4972 | ... | Res: 416 Ohms"
DATA_PATTERN = re.compile(
    r"XYZ: (-?[\d\.]+),(-?[\d\.]+),(-?[\d\.]+)"  # Group 1,2,3: Accel X,Y,Z
    r".*?\| Raw Red/IR: (\d+),(\d+)"              # Group 4,5: Red, IR
    r".*?\| Res: ([\d\.]+)"                        # Group 6: Resistance
)

# --- SERIAL CONNECTION ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    print(f"Successfully connected to {SERIAL_PORT}")
except serial.SerialException as e:
    print(f"Error: Could not open serial port '{SERIAL_PORT}'. {e}")
    exit()

# --- DATA STORAGE ---
# Create double-ended queues to efficiently store a fixed number of data points.
x_data = deque([0] * MAX_DATA_POINTS, maxlen=MAX_DATA_POINTS)
y_data = deque([0] * MAX_DATA_POINTS, maxlen=MAX_DATA_POINTS)
z_data = deque([0] * MAX_DATA_POINTS, maxlen=MAX_DATA_POINTS)
red_data = deque([0] * MAX_DATA_POINTS, maxlen=MAX_DATA_POINTS)
ir_data = deque([0] * MAX_DATA_POINTS, maxlen=MAX_DATA_POINTS)
res_data = deque([0] * MAX_DATA_POINTS, maxlen=MAX_DATA_POINTS)


# --- PLOT SETUP ---
# Create a figure with 4 vertically stacked subplots. `sharex=True` links the x-axis.
fig, (ax1, ax2, ax3, ax4) = plt.subplots(4, 1, figsize=(12, 10), sharex=True)
fig.suptitle('Real-Time Sensor Dashboard')

# Graph 1: Accelerometer
ax1.set_title('Accelerometer')
ax1.set_ylabel('Acceleration (m/s^2)')
ax1.set_ylim(-20, 20) # Set fixed limits for +-16G range
line_x, = ax1.plot([], [], lw=2, label='X')
line_y, = ax1.plot([], [], lw=2, label='Y')
line_z, = ax1.plot([], [], lw=2, label='Z')
ax1.legend(loc='upper left')
ax1.grid()

# Graph 2: IR Sensor
ax2.set_title('Oximeter - IR Signal')
ax2.set_ylabel('Raw IR Value')
line_ir, = ax2.plot([], [], lw=2, color='c', label='IR')
ax2.legend(loc='upper left')
ax2.grid()

# Graph 3: Red Sensor
ax3.set_title('Oximeter - Red Signal')
ax3.set_ylabel('Raw Red Value')
line_red, = ax3.plot([], [], lw=2, color='r', label='Red')
ax3.legend(loc='upper left')
ax3.grid()

# Graph 4: Resistance
ax4.set_title('ADC - Calculated Resistance')
ax4.set_xlabel('Time (Samples)')
ax4.set_ylabel('Resistance (Ohms)')
line_res, = ax4.plot([], [], lw=2, color='m', label='Resistance')
ax4.legend(loc='upper left')
ax4.grid()

plt.tight_layout(rect=[0, 0, 1, 0.96])


def init():
    """Initializes the plot lines with empty data."""
    # Set the data for each line
    for line in [line_x, line_y, line_z, line_ir, line_red, line_res]:
        line.set_data(range(MAX_DATA_POINTS), [0]*MAX_DATA_POINTS)
    return line_x, line_y, line_z, line_ir, line_red, line_res

def update(frame):
    """This function is called by the animation to update the plot."""
    try:
        # Read all available lines from serial to prevent buffer buildup
        while ser.in_waiting > 0:
            line = ser.readline().decode('utf-8').strip()

            # Use the regex to find a match in the line
            match = DATA_PATTERN.search(line)
            if match:
                # If we found a match, extract all data points
                x, y, z, red, ir, res = map(float, match.groups())
                
                # Append the new data to our storage deques
                x_data.append(x)
                y_data.append(y)
                z_data.append(z)
                red_data.append(red)
                ir_data.append(ir)
                res_data.append(res)

    except Exception as e:
        print(f"Error while reading/parsing: {e}")

    # Update the plot data for each line
    line_x.set_ydata(x_data)
    line_y.set_ydata(y_data)
    line_z.set_ydata(z_data)
    line_ir.set_ydata(ir_data)
    line_red.set_ydata(red_data)
    line_res.set_ydata(res_data)

    # Autoscale the y-axis for IR, Red, and Resistance plots
    ax2.relim()
    ax2.autoscale_view()
    ax3.relim()
    ax3.autoscale_view()
    ax4.relim()
    ax4.autoscale_view()

    return line_x, line_y, line_z, line_ir, line_red, line_res

# --- Start Animation ---
# 'blit=True' provides a major performance boost by only redrawing the parts of the plot that have changed.
ani = FuncAnimation(fig, update, init_func=init, blit=True, interval=50)

try:
    plt.show()
finally:
    # Ensure the serial port is closed when the plot window is closed
    if ser.is_open:
        ser.close()
        print("Serial port closed.")