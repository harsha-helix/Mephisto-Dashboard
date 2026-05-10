# Laser Dashboard Visualization

`laser_dashboard.html` is a browser-based visualization panel for the laser
controller firmware. It reads the UART stream from the Tiva board through the
Web Serial API, plots live values, shows fault status, and can record samples to
CSV.

## What It Shows

- Diode current limit and actual current
- Diode and crystal temperature setpoints and actual temperatures
- Diode and crystal TEC error signals
- Diode temperature error, calculated as `diode_temp_set - diode_temp_actual`
- Fault state banner and flashing metric cards when fault is active
- Recent incoming frames in the log strip

The dashboard keeps the latest 100 samples in each chart.

## Browser Requirements

Use a Chromium-based browser with Web Serial support:

- Google Chrome
- Microsoft Edge

Serve the file from `localhost`. Opening the file directly may not expose Web
Serial correctly.

## Start the Dashboard

From this project directory, run:

```sh
python -m http.server 8080
```

Then open:

```text
http://localhost:8080/laser_dashboard.html
```

The page loads Chart.js from a CDN, so the browser needs internet access unless
Chart.js is vendored locally.

## Connect to the Controller

1. Flash and run the firmware on the Tiva board.
2. Connect the board to the PC over USB/UART.
3. Open the dashboard in Chrome or Edge.
4. Select the baud rate. The firmware default is `115200`.
5. Click `connect`.
6. Pick the board's serial port when the browser prompts.

Use `simulate` to test the visualization without hardware.

## Expected Serial Frame

The dashboard expects one complete frame wrapped in `/*` and `*/`:

```text
/*805.00,402.00,25.10,24.90,112.00,30.00,29.80,161.00,0.00*/
```

Each frame must contain exactly nine comma-separated numeric fields:

| Index | Field | Unit / Meaning |
| --- | --- | --- |
| 0 | `diode_current_limit` | mA |
| 1 | `diode_current_actual` | mA |
| 2 | `diode_temp_set` | deg C |
| 3 | `diode_temp_actual` | deg C |
| 4 | `diode_tec_err` | mV |
| 5 | `crystal_temp_set` | deg C |
| 6 | `crystal_temp_actual` | deg C |
| 7 | `crystal_tec_err` | mV |
| 8 | `fault` | `0` for nominal, `1` for fault |

The current firmware emits this format from `UART0WriteChannels()` in `main.c`.

## Recording CSV

Click `record` to start collecting rows. Click `stop` to end the recording, then
click `save csv`.

CSV files are downloaded as:

```text
laser_log_YYYY-MM-DDTHH-MM-SS.csv
```

Columns:

```text
timestamp,diode_current_limit_mA,diode_current_actual_mA,diode_temp_set_C,diode_temp_actual_C,diode_tec_err_mV,crystal_temp_set_C,crystal_temp_actual_C,crystal_tec_err_mV,fault
```

## Troubleshooting

- If the Web Serial warning appears, switch to Chrome or Edge and serve the file
  from `localhost`.
- If no serial ports appear, check the USB cable, board power, and OS serial
  driver.
- If the dashboard connects but charts do not move, confirm the baud rate and
  verify that the firmware is sending `/* ... */` frames.
- If frames are rejected, check that each frame has exactly nine numeric values.
