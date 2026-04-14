# Toll Path Optimization (Dijkstra)

## Overview

This project implements a shortest-path algorithm (Dijkstra) to optimize routes based on toll costs.

It uses real toll pricing data extracted from a PDF source and processes it into a usable format for graph-based computation.

The application includes:
- a C++ implementation of Dijkstra
- a Python script to extract and preprocess data
- a minimal Win32 interface for user interaction

---

## Features

- Graph-based modeling of toll network
- Shortest path computation using Dijkstra algorithm
- Cost optimization based on toll pricing
- CSV data parsing
- Lightweight Win32 interface to:
  - select entry point
  - select destination
  - display computed route and cost

---

## Tech Stack

- C++ (C++17)
- STL (Standard Library)
- Win32 API (UI)
- Python (data extraction)
- pandas / pdfplumber (data processing)

---

## Data Pipeline

The project follows a simple pipeline:

1. Raw data from `TARIF_AREA.pdf`
2. Extraction using Python script (`scripts/extract_area_classe1.py`)
3. Conversion into structured CSV (`data/tarifs_area.csv`)
4. Loading in C++ application
5. Graph construction and shortest-path computation

---

## Build

g++ -std=c++17 -O2 -municode -mwindows src\dijkstra_peage.cpp -o dijkstra_peage.exe -lgdi32 -luser32