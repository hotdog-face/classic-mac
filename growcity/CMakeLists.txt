cmake_minimum_required(VERSION 3.9)
project(GrowCity C)

# Produces GrowCity.dsk (drag onto Mini vMac), plus .bin / .APPL.
add_application(GrowCity growcity.c)
