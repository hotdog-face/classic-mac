cmake_minimum_required(VERSION 3.9)
project(Aquarium C)

# Produces Aquarium.dsk (drag onto Mini vMac), plus .bin / .APPL.
add_application(Aquarium aquarium.c)
