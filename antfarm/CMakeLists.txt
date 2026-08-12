cmake_minimum_required(VERSION 3.9)
project(AntFarm C)

# Produces AntFarm.dsk (drag onto Mini vMac), plus .bin / .APPL.
add_application(AntFarm antfarm.c)
