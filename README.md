# Toll Optimization using Dijkstra

## Description
This project implements a shortest-path algorithm (Dijkstra) applied to toll cost optimization.

The goal is to compute the cheapest route between two points considering toll pricing.

## Features
- Graph-based modeling of toll network
- Custom cost function (distance + toll price)
- Efficient shortest path computation (Dijkstra)

## Tech stack
- C++
- Graph algorithms
- File parsing (CSV)

## Project structure
- `src/` → main algorithm
- `data/` → toll pricing dataset
- `scripts/` → preprocessing scripts

## How to build

```bash
g++ src/dijkstra_peage.cpp -o dijkstra