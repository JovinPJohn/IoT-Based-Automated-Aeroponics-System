# IoT-Based Automated Aeroponics System (ESP32)

Embedded environmental control system for aeroponic cultivation using ESP32 with closed-loop mist scheduling and real-time telemetry monitoring.

## Features

Closed-loop mist activation using humidity and temperature thresholds

Reservoir level monitoring with ultrasonic sensor safety cutoff

Nutrient concentration monitoring using TDS sensing

Optical pH estimation using RGB spectral ratio interpolation

ESP32-hosted HTTP telemetry dashboard

Multipage LCD runtime diagnostics interface

## System Architecture

The controller executes concurrent sensing, actuator scheduling, LCD diagnostics, and network telemetry using non-blocking timing intervals on an ESP32 edge device.

## Hardware

ESP32  
DHT22  
HC-SR04  
TDS Sensor  
TCS34725 RGB Sensor  
16×2 LCD  
Pump Driver Module
